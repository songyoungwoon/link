#pragma once
#include "runtime.hpp"
#include "model.hpp"
#include <cstddef>
#include <memory>

namespace linkworld {
// Owns context-local GLB GPU resources. clear() must run before the WGL context
// is destroyed; draw() expects the Element transform on the current modelview.
struct ModelGraphicsStats {std::size_t bytes=0,materialMaps=0,shadowPasses=0;unsigned shadowMaps=0;bool hdrEnvironment=false;};
class NativeModels {
public:
  NativeModels();
  ~NativeModels();
  NativeModels(const NativeModels&)=delete;
  NativeModels& operator=(const NativeModels&)=delete;
  void load(const World&);
  void clear() noexcept;
  void prepareScene(const Runtime&);
  void drawSky(const World&,const ModelMatrix& view,const ModelMatrix& projection);
  void draw(const RuntimeNode&,const World&,const Material&,const ModelMatrix& model,const ModelMatrix& view);
  ModelGraphicsStats stats() const noexcept;
  std::size_t count() const noexcept;
  std::size_t triangles() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace linkworld
