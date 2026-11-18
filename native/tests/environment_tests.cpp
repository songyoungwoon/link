#include "link/environment.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace linkworld;
namespace {
constexpr double pi=3.14159265358979323846;
using Color=std::array<double,3>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance,const char* message){if(std::abs(a-b)>tolerance)throw std::runtime_error(std::string(message)+": "+std::to_string(a)+" != "+std::to_string(b));}
void fails(const std::function<void()>& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
struct Temporary {
  std::filesystem::path path;
  Temporary(){
    auto base=std::filesystem::temp_directory_path();
    const auto seed=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<100;++attempt){auto candidate=base/("link-environment-tests-"+std::to_string(seed)+"-"+std::to_string(attempt));if(std::filesystem::create_directory(candidate)){path=std::move(candidate);return;}}
    throw std::runtime_error("Cannot allocate environment test directory");
  }
  ~Temporary(){if(!path.empty()){std::error_code error;std::filesystem::remove_all(path,error);}}
};
std::array<std::uint8_t,4> rgbe(Color value){
  const auto peak=*std::max_element(value.begin(),value.end());if(peak<1e-30)return {};
  int exponent=0;const double mantissa=std::frexp(peak,&exponent),scale=mantissa*256/peak;
  return {static_cast<std::uint8_t>(std::clamp(value[0]*scale,0.,255.)),static_cast<std::uint8_t>(std::clamp(value[1]*scale,0.,255.)),static_cast<std::uint8_t>(std::clamp(value[2]*scale,0.,255.)),static_cast<std::uint8_t>(exponent+128)};
}
std::vector<std::uint8_t> image(unsigned width,unsigned height,const std::function<Color(unsigned,unsigned)>& color){
  const auto header=std::string("#?RADIANCE\n# Original LINK test fixture\nFORMAT=32-bit_rle_rgbe\n\n-Y ")+std::to_string(height)+" +X "+std::to_string(width)+"\n";
  std::vector<std::uint8_t> result(header.begin(),header.end());
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto pixel=rgbe(color(x,y));result.insert(result.end(),pixel.begin(),pixel.end());}return result;
}
std::filesystem::path save(const std::filesystem::path& directory,const std::string& name,const std::vector<std::uint8_t>& bytes){
  auto path=directory/name;std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(file),"Could not write test HDR");return path;
}
Color at(const EnvironmentImage& image,unsigned x,unsigned y){const auto i=(static_cast<std::size_t>(y)*image.width+x)*3;return {image.rgb.at(i),image.rgb.at(i+1),image.rgb.at(i+2)};}
void constant(const std::filesystem::path& directory){
  const Color color{1,.5,2};const auto source=image(4,2,[&](unsigned,unsigned){return color;});
  auto environment=loadEnvironmentLighting(save(directory,"constant.hdr",source));
  check(environment.radiance.width==4&&environment.radiance.height==2&&environment.radiance.rgb.size()==24,"Original linear RGB radiance dimensions must be retained");
  check(environment.irradiance.width==64&&environment.irradiance.height==32,"Diffuse irradiance has a fixed bounded size");
  check(environment.specularMips.size()==9,"GGX prefilter contains nine physical mip levels");
  const auto unchanged=[&](const EnvironmentImage& img){check(img.rgb.size()==static_cast<std::size_t>(img.width)*img.height*3,"Every environment level is tightly packed RGB");for(std::size_t i=0;i<img.rgb.size();++i)near(img.rgb[i],color[i%3],.00005,"Constant environment must stay constant through convolution");};
  unchanged(environment.radiance);unchanged(environment.irradiance);
  unsigned w=256,h=128;for(const auto& mip:environment.specularMips){check(mip.width==w&&mip.height==h,"GGX mip dimensions halve to 1x1");unchanged(mip);w=std::max(1u,w/2);h=std::max(1u,h/2);}
  auto repeated=loadEnvironmentLighting(save(directory,"repeat.HDR",source));
  check(repeated.radiance.rgb==environment.radiance.rgb&&repeated.irradiance.rgb==environment.irradiance.rgb,"Repeated preprocessing must be deterministic");
  for(std::size_t level=0;level<repeated.specularMips.size();++level)check(repeated.specularMips[level].rgb==environment.specularMips[level].rgb,"Repeated GGX mip preprocessing must be deterministic");

  const Color bright{131072,98304,65536};
  auto brightEnvironment=loadEnvironmentLighting(save(directory,"bright-linear.hdr",image(4,2,[&](unsigned,unsigned){return bright;})));
  check(brightEnvironment.radiance.rgb.front()>65504&&brightEnvironment.specularMips.back().rgb.front()>65504,"HDR radiance above half-float range must be preserved for RGB32F upload");
  for(const auto* img:{&brightEnvironment.radiance,&brightEnvironment.irradiance})for(std::size_t i=0;i<img->rgb.size();++i)near(img->rgb[i],bright[i%3],.1,"High HDR radiance stays linear and unclipped");
  for(const auto& img:brightEnvironment.specularMips)for(std::size_t i=0;i<img.rgb.size();++i)near(img.rgb[i],bright[i%3],.1,"GGX filtering preserves high HDR radiance");

  // Original modern scanline RLE fixture: repeated RGBE components.
  const std::string hdr="#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 8 +X 16\n";std::vector<std::uint8_t> encoded(hdr.begin(),hdr.end());const auto pixel=rgbe(color);
  for(unsigned row=0;row<8;++row){encoded.insert(encoded.end(),{2,2,0,16});for(const auto value:pixel)encoded.insert(encoded.end(),{144,value});}
  auto rle=loadEnvironmentLighting(save(directory,"rle.hdr",encoded));check(rle.radiance.width==16&&rle.radiance.height==8,"RLE Radiance dimensions survive decode");unchanged(rle.radiance);
}
void directions(const std::filesystem::path& directory){
  constexpr unsigned width=32,height=16;
  auto bytes=image(width,height,[](unsigned x,unsigned y){const double phi=((double(x)+.5)/width-.5)*2*pi,theta=(double(y)+.5)/height*pi;return Color{.875+.75*std::cos(phi)*std::sin(theta),.625+.5*std::cos(theta),.875+.75*std::sin(phi)*std::sin(theta)};});
  const auto environment=loadEnvironmentLighting(save(directory,"directions.hdr",bytes));const auto& sharp=environment.specularMips.front();
  near(at(sharp,128,64)[0],1.625,.03,"+X points toward equirectangular u=.5");
  near(at(sharp,192,64)[2],1.625,.03,"+Z points toward equirectangular u=.75");
  near(at(sharp,64,64)[2],.125,.03,"-Z points toward equirectangular u=.25");
  near(at(sharp,128,0)[1],1.125,.02,"+Y/north is the first row");
  near(at(sharp,128,127)[1],.125,.02,"-Y/south is the last row");
  for(unsigned y=0;y<sharp.height;++y){const auto left=at(sharp,0,y),right=at(sharp,sharp.width-1,y);for(unsigned c=0;c<3;++c)near(left[c],right[c],.025,"Longitude interpolation remains continuous across the wrapping seam");}
  near(at(environment.irradiance,32,16)[0],1.375,.065,"Cosine-weighted irradiance is radiance average without an extra pi factor");
  const auto& rough=environment.specularMips[4];const double sharpContrast=at(sharp,128,64)[0]-at(sharp,0,64)[0];const double roughContrast=at(rough,rough.width/2,rough.height/2)[0]-at(rough,0,rough.height/2)[0];
  check(roughContrast>0&&roughContrast<sharpContrast,"Rough GGX environments reduce angular contrast");
  for(const auto* img:{&environment.radiance,&environment.irradiance})for(const auto v:img->rgb)check(std::isfinite(v)&&v>=0&&v<=1000000,"All imported and diffuse radiance is finite and bounded");
  for(const auto& img:environment.specularMips)for(const auto v:img.rgb)check(std::isfinite(v)&&v>=0&&v<=1000000,"All filtered specular radiance is finite and bounded");
}
double sphericalMean(const EnvironmentImage& img){
 double total=0,weight=0;for(unsigned y=0;y<img.height;++y){const double area=std::cos(pi*y/img.height)-std::cos(pi*(y+1)/img.height);for(unsigned x=0;x<img.width;++x){total+=at(img,x,y)[0]*area;weight+=area;}}return total/weight;
}
void brightSun(const std::filesystem::path& directory){
 // Align an original one-pixel sun with an old 64-sample diffuse ray. Sampling
 // the raw high-resolution image directly makes a bright irradiance outlier.
 constexpr unsigned width=1024,height=512;
 const double phi=((32.5/64)-.5)*2*pi,theta=16.5/32*pi;
 const Color normal{std::cos(phi)*std::sin(theta),std::cos(theta),std::sin(phi)*std::sin(theta)};
 Color tangent{normal[2],0,-normal[0]};const auto length=std::hypot(tangent[0],tangent[2]);for(auto& n:tangent)n/=length;
 Color ray{};for(unsigned c=0;c<3;++c)ray[c]=normal[c]*std::sqrt(1-.5/64)+tangent[c]*std::sqrt(.5/64);
 const auto sx=static_cast<unsigned>((std::atan2(ray[2],ray[0])/(2*pi)+.5)*width),sy=static_cast<unsigned>(std::acos(ray[1])/pi*height);
 const auto environment=loadEnvironmentLighting(save(directory,"small-bright-sun.hdr",image(width,height,[&](unsigned x,unsigned y){return x==sx&&y==sy?Color{86528,86528,86528}:Color{.2,.2,.2};})));
 near(*std::max_element(environment.radiance.rgb.begin(),environment.radiance.rgb.end()),86528,0,"Filtering must preserve original high dynamic range data");
 const auto peak=*std::max_element(environment.irradiance.rgb.begin(),environment.irradiance.rgb.end());
 check(peak<16,"A small bright sun must integrate over its footprint, without diffuse fireflies");
 const auto sourceMean=sphericalMean(environment.radiance),diffuseMean=sphericalMean(environment.irradiance);
 near(diffuseMean,sourceMean,sourceMean*.5,"Diffuse convolution should approximately conserve spherical radiance energy");
 for(std::size_t level=5;level<environment.specularMips.size();++level){const auto& mip=environment.specularMips[level];const auto maximum=*std::max_element(mip.rgb.begin(),mip.rgb.end());check(maximum<16,"Rough environment reflection must filter an isolated bright texel");}
}
void invalid(const std::filesystem::path& directory){
  auto valid=image(4,2,[](unsigned,unsigned){return Color{1,1,1};});
  const auto rejected=[&](const std::string& name,const std::vector<std::uint8_t>& bytes){const auto path=save(directory,name,bytes);fails([&]{loadEnvironmentLighting(path);},"Malformed or unsupported HDR must fail before preprocessing");};
  rejected("empty.hdr",{});rejected("not-hdr.hdr",{'P','N','G'});rejected("wrong.png",valid);
  auto truncated=valid;truncated.pop_back();rejected("truncated.hdr",truncated);
  auto trailing=valid;trailing.push_back(99);rejected("trailing.hdr",trailing);
  auto explosive=valid;for(std::size_t i=explosive.size()-32;i<explosive.size();i+=4)explosive[i+3]=255;rejected("huge-exponent.hdr",explosive);
  const auto header=[&](const std::string& values){return std::vector<std::uint8_t>(values.begin(),values.end());};
  for(const auto& text:{std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2049\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2048 +X 2048\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 0 +X 4\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=nan\n\n-Y 1 +X 1\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nGAMMA=inf\n\n-Y 1 +X 1\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 1 +X 1\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n+Y 1 +X 1\n"),std::string("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1 junk\n")})rejected("invalid-header.hdr",header(text));
  rejected("oversized-header.hdr",header("#?RADIANCE\n#"+std::string(1100,'x')+"\n"));
  auto rle=header("#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n");rle.insert(rle.end(),{2,2,0,8,0});rejected("zero-rle.hdr",rle);rle.back()=137;rejected("overflow-rle.hdr",rle);rle.back()=136;rejected("truncated-rle.hdr",rle);
  auto big=directory/"oversized.hdr";{std::ofstream output(big,std::ios::binary);output.seekp(8*1024*1024);output.put('x');}fails([&]{loadEnvironmentLighting(big);},"Encoded resource is limited to 8 MiB");
  fails([&]{loadEnvironmentLighting(directory/"missing.hdr");},"Missing HDR resource must fail");
  // Decode failure must not retain the private allocator's state.
  const auto okay=loadEnvironmentLighting(save(directory,"after-errors.hdr",valid));near(okay.radiance.rgb.front(),1,0,"Valid decode succeeds after malformed files");
}
}
int main(){try{Temporary temporary;constant(temporary.path);directions(temporary.path);brightSun(temporary.path);invalid(temporary.path);std::cout<<"HDR environment decode, deterministic cosine/GGX filtering, seam and budgets PASS\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
