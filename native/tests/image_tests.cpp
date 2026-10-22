#include "link/loader.hpp"
#include "link/textures.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <GL/gl.h>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void checkHr(HRESULT result,const char* message){check(SUCCEEDED(result),message);}
struct HiddenGl {
  HWND window=nullptr;HDC dc=nullptr;HGLRC context=nullptr;ATOM windowClass=0;
  HiddenGl(){
    try{
      WNDCLASSW type{};type.style=CS_OWNDC;type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"LINK.ImageTests.Hidden";
      windowClass=RegisterClassW(&type);check(windowClass!=0,"Register hidden texture-test window class");
      window=CreateWindowExW(0,type.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,type.hInstance,nullptr);check(window!=nullptr,"Create hidden texture-test window");
      dc=GetDC(window);check(dc!=nullptr,"Acquire hidden texture-test device context");
      PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;
      const auto selected=ChoosePixelFormat(dc,&format);check(selected&&SetPixelFormat(dc,selected,&format),"Choose hidden OpenGL pixel format");
      context=wglCreateContext(dc);check(context&&wglMakeCurrent(dc,context),"Create hidden OpenGL texture-test context");
    }catch(...){clear();throw;}
  }
  void clear(){
    if(context){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);context=nullptr;}
    if(dc&&window){ReleaseDC(window,dc);dc=nullptr;}if(window){DestroyWindow(window);window=nullptr;}
    if(windowClass){UnregisterClassW(L"LINK.ImageTests.Hidden",GetModuleHandleW(nullptr));windowClass=0;}
  }
  ~HiddenGl(){clear();}
};
struct TemporaryImages {
  std::filesystem::path directory=std::filesystem::temp_directory_path()/("link-mip-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryImages(){std::filesystem::create_directory(directory);}
  ~TemporaryImages(){std::error_code error;std::filesystem::remove(directory/"alpha.png",error);std::filesystem::remove(directory/"large.png",error);std::filesystem::remove(directory,error);}
};
void writePng(const std::filesystem::path& path,unsigned width,unsigned height,const std::vector<std::uint8_t>& pixels){
  using Microsoft::WRL::ComPtr;
  const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);check(apartment==RPC_E_CHANGED_MODE||SUCCEEDED(apartment),"Initialize WIC test encoder apartment");
  struct FinishApartment {HRESULT result;~FinishApartment(){if(SUCCEEDED(result))CoUninitialize();}} finish{apartment};
  ComPtr<IWICImagingFactory> factory;checkHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create WIC test encoder factory");
  ComPtr<IWICStream> stream;checkHr(factory->CreateStream(&stream),"Create WIC test image stream");checkHr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open WIC test PNG");
  ComPtr<IWICBitmapEncoder> encoder;checkHr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create PNG test encoder");checkHr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize PNG test encoder");
  ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;checkHr(encoder->CreateNewFrame(&frame,&properties),"Create PNG test frame");checkHr(frame->Initialize(properties.Get()),"Initialize PNG test frame");
  checkHr(frame->SetSize(width,height),"Set PNG test dimensions");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;checkHr(frame->SetPixelFormat(&format),"Set PNG test pixel format");check(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA),"PNG encoder preserves straight BGRA");
  auto bgra=pixels;for(std::size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
  checkHr(frame->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),bgra.data()),"Write PNG test pixels");checkHr(frame->Commit(),"Commit PNG test frame");checkHr(encoder->Commit(),"Commit PNG test image");
}
std::size_t inspectMipChain(const linkworld::Texture& texture){
  glBindTexture(GL_TEXTURE_2D,texture.id);GLint filter=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&filter);
  check(filter==GL_LINEAR_MIPMAP_LINEAR,"Scene textures use trilinear minification");std::size_t bytes=0;GLint previousWidth=0,previousHeight=0;
  for(GLint level=0;level<14;++level){
    GLint width=0,height=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&height);
    check(width>0&&height>0,"Every required mip level is uploaded");
    if(level)check(width==std::max(1,previousWidth/2)&&height==std::max(1,previousHeight/2),"Mip dimensions halve through rectangular tails");
    bytes+=static_cast<std::size_t>(width)*height*4;previousWidth=width;previousHeight=height;
    if(width==1&&height==1){check(bytes==texture.bytes,"GPU accounting includes every uploaded RGBA8 mip level");check(glGetError()==GL_NO_ERROR,"OpenGL texture inspection succeeds");return bytes;}
  }
  throw std::runtime_error("Texture mip chain does not reach one texel");
}
void testNativeUploads(){
  TemporaryImages files;HiddenGl context;linkworld::NativeTextures textures;
  const auto alphaPath=files.directory/"alpha.png",largePath=files.directory/"large.png";
  const std::vector<std::uint8_t> alphaPixels={255,0,0,255, 0,0,0,0, 0,0,255,255, 255,0,0,255, 0,0,0,0, 0,0,255,255};
  writePng(alphaPath,3,2,alphaPixels);linkworld::World tiny;tiny.images.emplace("alpha",alphaPath);textures.load(tiny);
  const auto* alpha=textures.image("alpha");check(alpha&&alpha->width==3&&alpha->height==2,"Small authored images retain their dimensions");check(inspectMipChain(*alpha)==44,"Rectangular POT image accounts for 4x2, 2x1 and 1x1 levels");
  check(alpha->u0==.125&&alpha->u1==.625&&alpha->v0==.25&&alpha->v1==.75,"Padded image retains the existing half-texel UV contract");
  std::vector<std::uint8_t> base(4*2*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,base.data());
  for(unsigned y=0;y<2;++y){const auto at=(y*4+3)*4;check(base[at]==0&&base[at+2]==255&&base[at+3]==255,"POT padding repeats the source edge instead of transparent black");}
  std::vector<std::uint8_t> first(2*4);glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,first.data());
  check(first[0]==255&&first[1]==0&&first[2]==0&&first[3]==128,"Alpha-aware mips retain saturated color beside transparent pixels");
  check(first[4]==0&&first[6]==255&&first[7]==255,"Padded blue edge remains opaque through minification");
  textures.clear();check(textures.bytes()==0&&textures.created()==textures.deleted(),"Mip texture release clears all byte accounting");
  std::vector<std::uint8_t> largePixels(2048*2048*4,255);writePng(largePath,2048,2048,largePixels);
  linkworld::World crowded;for(unsigned i=0;i<10;++i)crowded.images.emplace("surface"+std::to_string(i),largePath);
  linkworld::NodeDefinition plaque;plaque.id="plaque";plaque.text="LINK";plaque.geometry.size={1,1,0};crowded.nodes.push_back(plaque);
  textures.load(crowded);check(textures.imageCount()==10&&textures.textCount()==1,"Adaptation keeps every authored image and text resource");
  std::size_t measured=0;bool fullDetail=false,reducedDetail=false;
  for(const auto& [key,path]:crowded.images){const auto* texture=textures.image(key);check(texture&&texture->width<=2048&&texture->height<=2048,"Scene image dimensions stay bounded");fullDetail=fullDetail||texture->width==2048;reducedDetail=reducedDetail||texture->width<2048;measured+=inspectMipChain(*texture);}
  measured+=inspectMipChain(*textures.text("plaque"));
  check(fullDetail&&reducedDetail,"Budget planning preserves 2048 detail while adapting oversized collections");
  check(measured==textures.bytes()&&measured<=150ull*1024*1024,"Scene budget includes all image and text mip allocations");
  const auto& viewer=textures.viewer(largePath);check(viewer.width==2048&&viewer.height==2048,"Photo viewer retains full 2048 detail");inspectMipChain(viewer);
  check(textures.bytes()==measured+viewer.bytes&&textures.bytes()<=256ull*1024*1024,"Viewer and scene share the unchanged hard upload budget");
  const auto viewerId=viewer.id;textures.clearViewer();check(textures.bytes()==measured&&glIsTexture(viewerId)==GL_FALSE,"Viewer release deletes its complete mip chain");
  textures.clear();check(textures.count()==0&&textures.bytes()==0&&textures.created()==textures.deleted(),"All adapted scene texture allocations are released");
  linkworld::World interrupted;interrupted.images.emplace("first",alphaPath);interrupted.images.emplace("second",largePath);textures.begin(interrupted);textures.step();
  {std::ofstream corrupt(largePath,std::ios::binary|std::ios::trunc);corrupt<<"broken after preflight";}
  bool recovered=false;try{textures.step();}catch(const std::exception& error){recovered=std::string(error.what()).find("second")!=std::string::npos;}
  check(recovered&&textures.count()==0&&textures.bytes()==0&&textures.created()==textures.deleted(),"A post-preflight decode failure reports its resource and releases earlier uploads");
}
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
    testNativeUploads();
    const auto base=std::filesystem::canonical(std::filesystem::current_path());
    const auto bad=base/("link-corrupt-image-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jpg");
    struct RemoveFile{std::filesystem::path path;~RemoveFile(){std::error_code ec;std::filesystem::remove(path,ec);}} remove{bad};
    {std::ofstream out(bad,std::ios::binary);out<<"not a photograph";}
    bool rejected=false;try{(void)linkworld::decodeImage(bad,64);}catch(const std::exception&){rejected=true;}
    check(rejected,"A corrupt image must produce a recoverable decoder exception");
    std::cout<<"PASS native WIC decoding ("<<decoded<<" photographs), mip uploads, adaptive budget, alpha filtering, GPU cleanup, aspect fit, Korean text, malformed image rejection\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL native images: "<<error.what()<<'\n';return 1;}
}
