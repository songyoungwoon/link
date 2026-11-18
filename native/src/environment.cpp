#include "link/environment.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::size_t allocationLimit=32ull*1024*1024;
thread_local std::size_t allocatedBytes=0;
struct alignas(std::max_align_t) EnvironmentAllocation { std::size_t bytes; };
void* environmentAllocate(std::size_t bytes){
  if(bytes>allocationLimit||allocatedBytes>allocationLimit-bytes||bytes>std::numeric_limits<std::size_t>::max()-sizeof(EnvironmentAllocation))return nullptr;
  auto* block=static_cast<EnvironmentAllocation*>(std::malloc(sizeof(EnvironmentAllocation)+bytes));
  if(!block)return nullptr;block->bytes=bytes;allocatedBytes+=bytes;return block+1;
}
void environmentFree(void* pointer){
  if(!pointer)return;auto* block=static_cast<EnvironmentAllocation*>(pointer)-1;
  allocatedBytes-=block->bytes;std::free(block);
}
void* environmentReallocate(void* pointer,std::size_t bytes){
  if(!pointer)return environmentAllocate(bytes);
  if(!bytes){environmentFree(pointer);return nullptr;}
  auto* replacement=environmentAllocate(bytes);if(!replacement)return nullptr;
  auto* block=static_cast<EnvironmentAllocation*>(pointer)-1;
  std::memcpy(replacement,pointer,std::min(bytes,block->bytes));environmentFree(pointer);return replacement;
}
}
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#define STBI_MALLOC(bytes) environmentAllocate(bytes)
#define STBI_REALLOC(pointer,bytes) environmentReallocate(pointer,bytes)
#define STBI_FREE(pointer) environmentFree(pointer)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4244 4267 4456 4505 4996)
#endif
#include "../../third_party/stb_image/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace linkworld {
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr std::size_t fileLimit=8ull*1024*1024,pixelLimit=2ull*1024*1024;
constexpr unsigned samples=256;
constexpr double radianceLimit=1000000.;
using Direction=std::array<double,3>;
[[noreturn]] void reject(const std::string& message){throw std::runtime_error("HDR environment: "+message);}
void require(bool condition,const char* message){if(!condition)reject(message);}
Direction add(Direction a,const Direction& b){for(unsigned i=0;i<3;++i)a[i]+=b[i];return a;}
Direction scale(Direction a,double b){for(auto& x:a)x*=b;return a;}
double dot(const Direction& a,const Direction& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Direction cross(const Direction& a,const Direction& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Direction unit(Direction value){const double length=std::hypot(value[0],value[1],value[2]);require(std::isfinite(length)&&length>1e-15,"invalid sampling direction");return scale(value,1/length);}

struct Header { unsigned width=0,height=0;std::size_t pixels=0; };
Header preflight(const std::vector<stbi_uc>& bytes){
  // stb's flat RGBE decoder does not report every short read. Check the complete
  // bounded byte stream ourselves so truncated files never expose uninitialized data.
  std::size_t cursor=0;
  auto line=[&](){
    const auto start=cursor;while(cursor<bytes.size()&&bytes[cursor]!='\n'){
      const auto value=bytes[cursor];require(value=='\r'||value=='\t'||(value>=32&&value<=126),"non-ASCII Radiance header");
      ++cursor;require(cursor-start<=1024&&cursor<=8192,"header byte budget exceeded");
    }
    require(cursor<bytes.size(),"truncated Radiance header");
    std::string_view result(reinterpret_cast<const char*>(bytes.data()+start),cursor-start);++cursor;
    if(!result.empty()&&result.back()=='\r')result.remove_suffix(1);return result;
  };
  const auto signature=line();require(signature=="#?RADIANCE"||signature=="#?RGBE","expected Radiance RGBE signature");
  bool format=false;
  for(;;){const auto value=line();if(value.empty())break;
    if(value.starts_with("FORMAT=")){require(value=="FORMAT=32-bit_rle_rgbe"&&!format,"unsupported/duplicate Radiance format");format=true;}
    for(const auto prefix:{std::string_view("EXPOSURE="),std::string_view("GAMMA=")})if(value.starts_with(prefix)){
      const auto number=value.substr(prefix.size());double parsed=0;const auto converted=std::from_chars(number.data(),number.data()+number.size(),parsed);
      require(converted.ec==std::errc{}&&converted.ptr==number.data()+number.size()&&std::isfinite(parsed)&&parsed>0,"invalid numeric Radiance metadata");
    }
  }
  require(format,"missing FORMAT=32-bit_rle_rgbe");
  auto dimensions=line();Header header;
  const auto integer=[&](std::string_view& text){
    while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
    unsigned result=0;const auto converted=std::from_chars(text.data(),text.data()+text.size(),result);
    require(converted.ec==std::errc{}&&converted.ptr!=text.data(),"invalid Radiance dimensions");
    text.remove_prefix(static_cast<std::size_t>(converted.ptr-text.data()));return result;
  };
  require(dimensions.starts_with("-Y "),"only -Y +X equirectangular Radiance layout is supported");dimensions.remove_prefix(3);header.height=integer(dimensions);
  while(!dimensions.empty()&&dimensions.front()==' ')dimensions.remove_prefix(1);
  require(dimensions.starts_with("+X "),"only -Y +X equirectangular Radiance layout is supported");dimensions.remove_prefix(3);header.width=integer(dimensions);
  while(!dimensions.empty()&&dimensions.front()==' ')dimensions.remove_prefix(1);
  require(dimensions.empty()&&header.width>0&&header.height>0&&header.width<=2048&&header.height<=2048,"dimensions must be 1..2048 on each axis");
  require(static_cast<std::size_t>(header.width)*header.height<=pixelLimit,"source pixel budget exceeds 2097152");
  header.pixels=cursor;
  const auto available=[&](std::size_t count){require(cursor<=bytes.size()&&count<=bytes.size()-cursor,"truncated Radiance pixel data");};
  available(4);
  const bool rle=header.width>=8&&bytes[cursor]==2&&bytes[cursor+1]==2&&(bytes[cursor+2]&128)==0;
  if(!rle){const auto count=static_cast<std::size_t>(header.width)*header.height*4;available(count);cursor+=count;}
  else for(unsigned row=0;row<header.height;++row){
    available(4);require(bytes[cursor]==2&&bytes[cursor+1]==2&&((unsigned(bytes[cursor+2])<<8)|bytes[cursor+3])==header.width,"invalid Radiance scanline length/layout");cursor+=4;
    for(unsigned channel=0;channel<4;++channel){unsigned decoded=0;while(decoded<header.width){
      available(1);const auto code=bytes[cursor++];require(code!=0,"zero-length Radiance RLE packet");
      const unsigned length=code>128?unsigned(code)-128:unsigned(code);
      require(length<=header.width-decoded,"Radiance RLE packet exceeds scanline");
      const std::size_t consumed=code>128?1:length;available(consumed);cursor+=consumed;decoded+=length;
    }}
  }
  // A few writers end files with a newline; arbitrary trailing packets are rejected.
  while(cursor<bytes.size()&&(bytes[cursor]=='\n'||bytes[cursor]=='\r'))++cursor;
  require(cursor==bytes.size(),"trailing Radiance pixel data");return header;
}

EnvironmentImage sourceImage(const std::filesystem::path& path){
  auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c+32:c);});
  require(extension==".hdr","expected a self-contained .hdr resource");
  std::ifstream input(path,std::ios::binary|std::ios::ate);require(bool(input),"cannot open resource");const auto length=input.tellg();
  require(length>0&&length<=static_cast<std::streamoff>(fileLimit),"file byte budget exceeded (maximum 8 MiB)");
  std::vector<stbi_uc> bytes(static_cast<std::size_t>(length));input.seekg(0);
  require(bool(input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))),"failed while reading resource");
  const auto header=preflight(bytes);int width=0,height=0,components=0;
  require(stbi_is_hdr_from_memory(bytes.data(),static_cast<int>(bytes.size()))!=0,"invalid Radiance resource");
  struct AllocationPhase {AllocationPhase(){require(allocatedBytes==0,"unexpected nested image allocation");}~AllocationPhase(){}} phase;
  std::unique_ptr<float,decltype(&stbi_image_free)> decoded(stbi_loadf_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&components,3),stbi_image_free);
  if(!decoded)reject(std::string("decode failed: ")+(stbi_failure_reason()?stbi_failure_reason():"unknown error"));
  require(width==static_cast<int>(header.width)&&height==static_cast<int>(header.height)&&components==3,"decoded dimensions disagree with header");
  EnvironmentImage result;result.width=header.width;result.height=header.height;
  const auto count=static_cast<std::size_t>(result.width)*result.height*3;
  for(std::size_t i=0;i<count;++i)require(std::isfinite(decoded.get()[i])&&decoded.get()[i]>=0&&decoded.get()[i]<=radianceLimit,"radiance must be finite and within [0,1000000]");
  result.rgb.assign(decoded.get(),decoded.get()+count);return result;
}

Direction direction(unsigned x,unsigned y,unsigned width,unsigned height){
  const double phi=((double(x)+.5)/width-.5)*(2*pi),theta=(double(y)+.5)/height*pi;
  const double sine=std::sin(theta);return {std::cos(phi)*sine,std::cos(theta),std::sin(phi)*sine};
}
Direction sampleUv(const EnvironmentImage& image,double u,double v){
  const double sx=u*image.width-.5,sy=std::clamp(v*image.height-.5,0.,double(image.height-1));
  const auto x=static_cast<int>(std::floor(sx));const auto y=static_cast<unsigned>(std::floor(sy));
  const double fx=sx-x,fy=sy-y;const auto x0=static_cast<unsigned>((x%static_cast<int>(image.width)+static_cast<int>(image.width))%static_cast<int>(image.width));
  const unsigned x1=(x0+1)%image.width,y1=std::min(y+1,image.height-1);
  Direction result{};for(unsigned c=0;c<3;++c){
    const auto at=[&](unsigned px,unsigned py){return double(image.rgb[(static_cast<std::size_t>(py)*image.width+px)*3+c]);};
    result[c]=(at(x0,y)*(1-fx)+at(x1,y)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy;
  }return result;
}
// Box-filter each longitude/latitude cell with its exact spherical area. A flat
// lat-long average would overweight polar pixels and change environment energy.
EnvironmentImage downsample(const EnvironmentImage& source){
  EnvironmentImage target;target.width=std::max(1u,source.width/2);target.height=std::max(1u,source.height/2);
  target.rgb.resize(static_cast<std::size_t>(target.width)*target.height*3);
  for(unsigned y=0;y<target.height;++y)for(unsigned x=0;x<target.width;++x){
    const double x0=double(x)*source.width/target.width,x1=double(x+1)*source.width/target.width;
    const double y0=double(y)*source.height/target.height,y1=double(y+1)*source.height/target.height;
    Direction total{};double weight=0;
    const auto firstY=static_cast<unsigned>(std::floor(y0)),lastY=std::min(source.height,static_cast<unsigned>(std::ceil(y1)));
    const auto firstX=static_cast<unsigned>(std::floor(x0)),lastX=std::min(source.width,static_cast<unsigned>(std::ceil(x1)));
    for(unsigned sy=firstY;sy<lastY;++sy){
      const double lo=std::max(y0,double(sy)),hi=std::min(y1,double(sy+1));
      const double row=std::cos(pi*lo/source.height)-std::cos(pi*hi/source.height);
      for(unsigned sx=firstX;sx<lastX;++sx){
        const double area=row*(std::min(x1,double(sx+1))-std::max(x0,double(sx)));weight+=area;
        const auto offset=(static_cast<std::size_t>(sy)*source.width+sx)*3;
        for(unsigned c=0;c<3;++c)total[c]+=source.rgb[offset+c]*area;
      }
    }
    require(weight>0,"empty spherical mip footprint");const auto offset=(static_cast<std::size_t>(y)*target.width+x)*3;
    for(unsigned c=0;c<3;++c)target.rgb[offset+c]=static_cast<float>(total[c]/weight);
  }return target;
}
struct SourcePyramid {
  const EnvironmentImage& original;std::vector<EnvironmentImage> mips;
  explicit SourcePyramid(const EnvironmentImage& image):original(image){
    mips.reserve(12);const EnvironmentImage* previous=&original;
    while(previous->width>1||previous->height>1){auto next=downsample(*previous);mips.push_back(std::move(next));previous=&mips.back();}
  }
  const EnvironmentImage& level(unsigned index)const{return index?mips.at(index-1):original;}
  double minimumLod(unsigned outputWidth,unsigned outputHeight)const{
    return std::max({0.,std::log2(double(original.width)/outputWidth),std::log2(double(original.height)/outputHeight)});
  }
  Direction lookup(const Direction& source,double lod)const{
    const auto d=unit(source);const double u=std::atan2(d[2],d[0])/(2*pi)+.5,v=std::acos(std::clamp(d[1],-1.,1.))/pi;
    lod=std::clamp(lod,0.,double(mips.size()));const auto lower=static_cast<unsigned>(std::floor(lod)),upper=std::min(lower+1,static_cast<unsigned>(mips.size()));const double blend=lod-lower;
    const auto a=sampleUv(level(lower),u,v);if(lower==upper||blend<=1e-12)return a;
    const auto b=sampleUv(level(upper),u,v);return add(scale(a,1-blend),scale(b,blend));
  }
  Direction footprint(const Direction& d,double pdf,double minimum=0)const{
    require(std::isfinite(pdf)&&pdf>0,"invalid environment sample PDF");
    const double sine=std::sqrt(std::max(0.,1-d[1]*d[1]));
    const double solidAngleTexel=(2*pi/original.width)*(pi/original.height)*std::max(sine,.001);
    const double solidAngleSample=1/(samples*pdf);
    return lookup(d,std::max(minimum,.5*std::log2(solidAngleSample/solidAngleTexel)));
  }
};
std::array<double,2> hammersley(unsigned index){
  std::uint32_t bits=index;bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xAAAAAAAAu)>>1);bits=((bits&0x33333333u)<<2)|((bits&0xCCCCCCCCu)>>2);bits=((bits&0x0F0F0F0Fu)<<4)|((bits&0xF0F0F0F0u)>>4);bits=((bits&0x00FF00FFu)<<8)|((bits&0xFF00FF00u)>>8);
  return {(double(index)+.5)/samples,double(bits)*2.3283064365386962890625e-10};
}
struct Basis {
  Direction tangent,bitangent,normal;
  explicit Basis(const Direction& n):normal(n){tangent=unit(cross(std::abs(n[1])<.999?Direction{0,1,0}:Direction{1,0,0},n));bitangent=cross(n,tangent);}
  Direction toWorld(const Direction& value)const{return add(add(scale(tangent,value[0]),scale(bitangent,value[1])),scale(normal,value[2]));}
};
void store(EnvironmentImage& image,unsigned x,unsigned y,const Direction& color){
  const auto offset=(static_cast<std::size_t>(y)*image.width+x)*3;
  for(unsigned c=0;c<3;++c){require(std::isfinite(color[c])&&color[c]>=0&&color[c]<=radianceLimit+.001,"nonfinite filtered radiance");image.rgb[offset+c]=static_cast<float>(std::clamp(color[c],0.,radianceLimit));}
}
EnvironmentImage diffuse(const SourcePyramid& source){
  EnvironmentImage image;image.width=64;image.height=32;image.rgb.resize(static_cast<std::size_t>(image.width)*image.height*3);
  std::array<Direction,samples> local{};
  for(unsigned i=0;i<samples;++i){const auto xi=hammersley(i);const double r=std::sqrt(xi[0]),phi=2*pi*xi[1];local[i]={r*std::cos(phi),r*std::sin(phi),std::sqrt(1-xi[0])};}
  for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
    const Basis basis(direction(x,y,image.width,image.height));Direction total{};
    for(const auto& value:local){const auto light=basis.toWorld(value);total=add(total,source.footprint(light,value[2]/pi));}
    store(image,x,y,scale(total,1./samples));
  }return image;
}
EnvironmentImage specular(const SourcePyramid& source,unsigned width,unsigned height,double roughness){
  EnvironmentImage image;image.width=width;image.height=height;image.rgb.resize(static_cast<std::size_t>(width)*height*3);
  std::array<Direction,samples> halfvectors{};const double a=roughness*roughness;
  const double minimumLod=source.minimumLod(width,height);
  for(unsigned i=0;i<samples;++i){const auto xi=hammersley(i);const double phi=2*pi*xi[0],cosine=std::sqrt((1-xi[1])/(1+(a*a-1)*xi[1])),sine=std::sqrt(std::max(0.,1-cosine*cosine));halfvectors[i]={std::cos(phi)*sine,std::sin(phi)*sine,cosine};}
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
    const auto normal=direction(x,y,width,height);if(roughness==0){store(image,x,y,source.lookup(normal,minimumLod));continue;}
    const Basis basis(normal);Direction total{};double weight=0;
    for(const auto& local:halfvectors){const auto half=basis.toWorld(local),light=unit(add(scale(half,2*dot(normal,half)),scale(normal,-1)));
      const double cosine=std::max(0.,dot(normal,light));if(cosine>0){
        const double nh=std::max(dot(normal,half),0.),vh=nh;
        const double denominator=nh*nh*(a*a-1)+1;
        const double distribution=(a*a)/(pi*denominator*denominator);
        const double pdf=distribution*nh/(4*std::max(vh,1e-12));
        total=add(total,scale(source.footprint(light,pdf),cosine));weight+=cosine;
      }
    }
    require(weight>0,"empty GGX sampling lobe");store(image,x,y,scale(total,1/weight));
  }return image;
}
} // namespace

EnvironmentLighting loadEnvironmentLighting(const std::filesystem::path& path){
  EnvironmentLighting result;result.radiance=sourceImage(path);const SourcePyramid source(result.radiance);result.irradiance=diffuse(source);
  constexpr unsigned levels=9;result.specularMips.reserve(levels);
  unsigned width=256,height=128;
  for(unsigned level=0;level<levels;++level){result.specularMips.push_back(specular(source,width,height,double(level)/(levels-1)));width=std::max(1u,width/2);height=std::max(1u,height/2);}
  return result;
}
} // namespace linkworld
