#include "link/loader.hpp"
#include "link/textures.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <chrono>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(){
  try{
    const auto root=std::filesystem::path(u8"" LINK_SOURCE_DIR)/"worlds";
    const auto gallery=linkworld::loadWorld("link://local/heukryong",root);
    std::size_t decoded=0;
    for(const auto& [key,path]:gallery->images){
      const auto image=linkworld::decodeImage(path,64);
      check(image.width&&image.height&&image.width<=64&&image.height<=64,"Decoded photo dimensions must respect target bounds");
      check(image.rgba.size()==static_cast<std::size_t>(image.width)*image.height*4,"WIC returns an exact RGBA pixel buffer");
      check(image.originalWidth&&image.originalHeight,"Original display-copy dimensions are retained for aspect ratio");
      const auto& nodes=gallery->nodes;
      for(const auto& node:nodes)if(node.image==key){
        const double photoAspect=double(image.originalWidth)/image.originalHeight;
        const double planeAspect=node.geometry.size[0]/node.geometry.size[1];
        check(std::abs(photoAspect-planeAspect)<.002,"Image planes preserve each photograph aspect without cropping or stretching");
      }
      ++decoded;
    }
    check(decoded==gallery->images.size()&&decoded>0,"All currently selected photographs decode successfully in native WIC");
    const auto text=linkworld::rasterText("사진 보기",5,"#F5F1E8");
    check(text.width&&text.height&&text.rgba.size()==static_cast<std::size_t>(text.width)*text.height*4,"Korean plaque raster is valid RGBA");
    std::size_t different=0;
    for(std::size_t i=0;i<text.rgba.size();i+=4){
      check(text.rgba[i+3]==255,"Opaque text plaques have initialized alpha");
      if(text.rgba[i]<150&&text.rgba[i+1]<150&&text.rgba[i+2]<150)++different;
    }
    check(different>100,"Korean plaque contains visible dark glyphs on the light background");
    const auto hologram=linkworld::rasterText("LINK - 새로운 탐험",5,"#A3F9F2",false);
    check(hologram.width==text.width&&hologram.height==text.height,"Transparent text keeps the same plane raster dimensions");
    std::size_t transparent=0,covered=0,antialiased=0;
    for(std::size_t i=0;i<hologram.rgba.size();i+=4){
      check(hologram.rgba[i]==0xA3&&hologram.rgba[i+1]==0xF9&&hologram.rgba[i+2]==0xF2,"Transparent glyphs retain the requested cyan RGB without a dark filtering fringe");
      const auto alpha=hologram.rgba[i+3];
      if(alpha==0)++transparent;
      if(alpha>0)++covered;
      if(alpha>0&&alpha<255)++antialiased;
    }
    check(transparent>hologram.width*hologram.height/2,"Hologram background is fully transparent");
    check(covered>100&&antialiased>20,"Hologram contains visible glyph coverage and antialiased alpha edges");
    const auto explicitPlaque=linkworld::rasterText("사진 보기",5,"#F5F1E8",true);
    check(explicitPlaque.rgba==text.rgba,"Explicit textBackground true preserves the legacy plaque raster exactly");
    const auto base=std::filesystem::canonical(std::filesystem::current_path());
    const auto bad=base/("link-corrupt-image-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jpg");
    struct RemoveFile{std::filesystem::path path;~RemoveFile(){std::error_code ec;std::filesystem::remove(path,ec);}} remove{bad};
    {std::ofstream out(bad,std::ios::binary);out<<"not a photograph";}
    bool rejected=false;try{(void)linkworld::decodeImage(bad,64);}catch(const std::exception&){rejected=true;}
    check(rejected,"A corrupt image must produce a recoverable decoder exception");
    std::cout<<"PASS native WIC decoding ("<<decoded<<" photographs), aspect fit, opaque and transparent Korean text, malformed image rejection\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL native images: "<<error.what()<<'\n';return 1;}
}
