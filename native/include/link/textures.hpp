#pragma once
#include "core.hpp"
#include <cstdint>

namespace linkworld {
// Pixel rows are top to bottom; UV (0,0) is the photograph's upper left.
struct CpuImage {
  unsigned width=0,height=0,originalWidth=0,originalHeight=0;
  std::vector<std::uint8_t> rgba;
};
CpuImage decodeImage(const std::filesystem::path&,unsigned maxDimension=512);
// Opaque plaques use color for the background; transparent text uses it for glyphs.
CpuImage rasterText(const std::string&,double aspect,const std::string& color,bool textBackground=true);
struct Texture {
  unsigned id=0,width=0,height=0;double aspect=1,u0=0,v0=0,u1=1,v1=1;
  std::size_t bytes=0;bool textBackground=true; // Includes every RGBA8 mip level.
};
class NativeTextures {
public:
  ~NativeTextures();
  NativeTextures()=default;NativeTextures(const NativeTextures&)=delete;NativeTextures& operator=(const NativeTextures&)=delete;
  void load(const World&,LogFn={});
  void begin(const World&);void step(unsigned maxItems=1);
  bool loading() const {return completed_<jobs_.size();}
  std::size_t completed() const {return completed_;}
  std::size_t total() const {return jobs_.size();}
  void clear();void clearViewer();
  const Texture* image(const std::string&) const;
  const Texture* text(const std::string& nodeId) const;
  const Texture& viewer(const std::filesystem::path&);
  std::size_t count() const {return images_.size()+texts_.size()+(viewer_.id?1:0);}
  std::size_t imageCount() const {return images_.size();}
  std::size_t textCount() const {return texts_.size();}
  std::size_t bytes() const {return liveBytes_;}
  std::size_t created() const {return created_;}
  std::size_t deleted() const {return deleted_;}
private:
  std::map<std::string,Texture> images_,texts_;Texture viewer_;
  struct Job {std::string key,text,color;std::filesystem::path path;double aspect=1;bool textBackground=true;unsigned maxDimension=2048;};
  std::vector<Job> jobs_;std::size_t completed_=0;
  std::size_t liveBytes_=0,created_=0,deleted_=0;
  Texture upload(const CpuImage&);void release(Texture&);
};
} // namespace linkworld
