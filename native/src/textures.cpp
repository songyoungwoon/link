#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <GL/gl.h>
#include "link/textures.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace linkworld {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result,const char* operation){if(FAILED(result)){std::ostringstream message;message<<operation<<" failed (HRESULT 0x"<<std::hex<<static_cast<unsigned>(result)<<")";throw std::runtime_error(message.str());}}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Apartment(){if(result!=RPC_E_CHANGED_MODE)checked(result,"COM initialization");}~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
unsigned powerOfTwo(unsigned n){unsigned result=1;while(result<n)result*=2;return result;}
GLint edgeClamp(){
  const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if(version&&(version[0]>'1'||(version[0]=='1'&&version[1]=='.'&&version[2]>='2')))return 0x812F; // GL_CLAMP_TO_EDGE
  const auto* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  if(extensions){const std::string list=std::string(" ")+extensions+" ";if(list.find(" GL_EXT_texture_edge_clamp ")!=std::string::npos||list.find(" GL_SGIS_texture_edge_clamp ")!=std::string::npos)return 0x812F;}
  return GL_CLAMP;
}
constexpr std::size_t uploadBudget=256ull*1024*1024,sceneBudget=150ull*1024*1024;
struct ImageSize {unsigned width,height;};
ImageSize scaledSize(ImageSize size,unsigned cap){
  const double scale=std::min(1.,double(cap)/std::max(size.width,size.height));
  return {std::max(1u,unsigned(std::lround(size.width*scale))),std::max(1u,unsigned(std::lround(size.height*scale)))};
}
std::size_t mipBytes(ImageSize size){
  unsigned w=powerOfTwo(size.width),h=powerOfTwo(size.height);std::size_t bytes=0;
  for(;;){bytes+=static_cast<std::size_t>(w)*h*4;if(w==1&&h==1)return bytes;w=std::max(1u,w/2);h=std::max(1u,h/2);}
}
unsigned textHeight(double aspect){return std::clamp(unsigned(1024/std::max(.25,aspect)),64u,1024u);}
ComPtr<IWICBitmapFrameDecode> imageFrame(IWICImagingFactory* factory,const std::filesystem::path& path){
  ComPtr<IWICBitmapDecoder> decoder;checked(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"WIC image decoder");
  ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"WIC image frame");return frame;
}
ImageSize imageSize(IWICBitmapFrameDecode* frame){
  UINT w=0,h=0;checked(frame->GetSize(&w,&h),"WIC image dimensions");
  if(!w||!h||w>8192||h>8192)throw std::runtime_error("Image dimensions must be between 1 and 8192 pixels");return {w,h};
}
std::vector<std::uint8_t> smallerMip(const std::vector<std::uint8_t>& pixels,unsigned width,unsigned height){
  const unsigned w=std::max(1u,width/2),h=std::max(1u,height/2);std::vector<std::uint8_t> result(static_cast<std::size_t>(w)*h*4);
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
    unsigned alpha=0;std::array<unsigned,3> weighted{},plain{};
    for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
      const auto at=(static_cast<std::size_t>(std::min(height-1,y*2+dy))*width+std::min(width-1,x*2+dx))*4;const unsigned a=pixels[at+3];alpha+=a;
      for(unsigned c=0;c<3;++c){weighted[c]+=unsigned(pixels[at+c])*a;plain[c]+=pixels[at+c];}
    }
    const auto at=(static_cast<std::size_t>(y)*w+x)*4;
    // Average coverage in premultiplied space, then restore straight RGBA for
    // GL_SRC_ALPHA blending. Preserve transparent glyph RGB when coverage is zero.
    for(unsigned c=0;c<3;++c)result[at+c]=static_cast<std::uint8_t>(alpha?(weighted[c]+alpha/2)/alpha:(plain[c]+2)/4);
    result[at+3]=static_cast<std::uint8_t>((alpha+2)/4);
  }return result;
}
std::wstring wideText(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF-8 plaque text");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
}
CpuImage decodeImage(const std::filesystem::path& path,unsigned maxDimension){
  if(!maxDimension||maxDimension>2048)throw std::invalid_argument("Image target dimension must be between 1 and 2048");
  Apartment apartment;ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
  auto frame=imageFrame(factory.Get(),path);const auto original=imageSize(frame.Get()),size=scaledSize(original,maxDimension);
  CpuImage result;result.originalWidth=original.width;result.originalHeight=original.height;result.width=size.width;result.height=size.height;
  ComPtr<IWICBitmapSource> source;checked(frame.As(&source),"WIC bitmap source");ComPtr<IWICBitmapScaler> scaler;
  if(result.width!=original.width||result.height!=original.height){checked(factory->CreateBitmapScaler(&scaler),"WIC scaler");checked(scaler->Initialize(source.Get(),result.width,result.height,WICBitmapInterpolationModeFant),"WIC image scaling");checked(scaler.As(&source),"WIC scaled source");}
  ComPtr<IWICFormatConverter> converter;checked(factory->CreateFormatConverter(&converter),"WIC format converter");checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"WIC RGBA conversion");
  result.rgba.resize(static_cast<std::size_t>(result.width)*result.height*4);checked(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.rgba.size()),result.rgba.data()),"WIC pixels");return result;
}
CpuImage rasterText(const std::string& text,double aspect,const std::string& color,bool textBackground){
  auto content=wideText(text);
  CpuImage result;result.width=1024;result.height=textHeight(aspect);result.originalWidth=result.width;result.originalHeight=result.height;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=result.width;info.bmiHeader.biHeight=-static_cast<LONG>(result.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!dc||!bitmap){if(dc)DeleteDC(dc);throw std::runtime_error("Could not rasterize gallery plaque");}auto oldBitmap=SelectObject(dc,bitmap);
  unsigned rgb=0x26302c;try{rgb=static_cast<unsigned>(std::stoul(color.substr(1),nullptr,16));}catch(...){}
  // White-on-black GDI output becomes a grayscale glyph coverage mask. Keep the
  // requested RGB in every transparent texel to avoid dark bilinear edge fringes.
  const auto background=textBackground?RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255):RGB(0,0,0);
  HBRUSH brush=CreateSolidBrush(background);RECT area{0,0,static_cast<LONG>(result.width),static_cast<LONG>(result.height)};FillRect(dc,&area,brush);DeleteObject(brush);
  int lines=1;for(char c:text)if(c=='\n')++lines;int fontSize=std::clamp(int(result.height)/(lines+1),18,72);
  HFONT font=CreateFontW(-fontSize,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,HANGUL_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);double luminance=.2126*((rgb>>16)&255)+.7152*((rgb>>8)&255)+.0722*(rgb&255);SetTextColor(dc,textBackground?(luminance>140?RGB(60,57,49):RGB(242,235,215)):RGB(255,255,255));RECT bounds{38,16,static_cast<LONG>(result.width)-38,static_cast<LONG>(result.height)-16};RECT measured=bounds;DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&measured,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);bounds.top=std::max<LONG>(12,(result.height-(measured.bottom-measured.top))/2);DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&bounds,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
  GdiFlush();result.rgba.resize(static_cast<std::size_t>(result.width)*result.height*4);auto* bgra=static_cast<unsigned char*>(bits);for(std::size_t i=0;i<result.rgba.size();i+=4){
    if(textBackground){result.rgba[i]=bgra[i+2];result.rgba[i+1]=bgra[i+1];result.rgba[i+2]=bgra[i];result.rgba[i+3]=255;}
    else{result.rgba[i]=static_cast<std::uint8_t>((rgb>>16)&255);result.rgba[i+1]=static_cast<std::uint8_t>((rgb>>8)&255);result.rgba[i+2]=static_cast<std::uint8_t>(rgb&255);result.rgba[i+3]=std::max({bgra[i],bgra[i+1],bgra[i+2]});}
  }
  SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);return result;
}
NativeTextures::~NativeTextures(){clear();}
Texture NativeTextures::upload(const CpuImage& image){
  Texture result;result.width=image.width;result.height=image.height;result.aspect=double(image.originalWidth)/image.originalHeight;
  const unsigned baseWidth=powerOfTwo(image.width),baseHeight=powerOfTwo(image.height);result.bytes=mipBytes({image.width,image.height});
  if(liveBytes_+result.bytes>uploadBudget)throw std::runtime_error("World textures exceed the 256 MiB GPU upload budget");
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(baseWidth)*baseHeight*4);
  // Extend the final source row/column into POT padding so every mip remains
  // seamless at the existing half-texel image UV boundaries.
  for(unsigned y=0;y<baseHeight;++y){
    const auto* source=image.rgba.data()+static_cast<std::size_t>(std::min(y,image.height-1))*image.width*4;
    auto* target=pixels.data()+static_cast<std::size_t>(y)*baseWidth*4;std::copy_n(source,image.width*4,target);
    for(unsigned x=image.width;x<baseWidth;++x)std::copy_n(source+(image.width-1)*4,4,target+x*4);
  }
  while(glGetError()!=GL_NO_ERROR){}
  try{
    glGenTextures(1,&result.id);glBindTexture(GL_TEXTURE_2D,result.id);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    const auto clamp=edgeClamp();glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,clamp);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,clamp);
    unsigned w=baseWidth,h=baseHeight;GLint level=0;
    for(;;){
      glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      if(w==1&&h==1)break;
      pixels=smallerMip(pixels,w,h);w=std::max(1u,w/2);h=std::max(1u,h/2);++level;
    }
    glBindTexture(GL_TEXTURE_2D,0);
    if(!result.id||glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL photo texture allocation failed");
  }catch(...){glBindTexture(GL_TEXTURE_2D,0);if(result.id)glDeleteTextures(1,&result.id);throw;}
  result.u0=.5/baseWidth;result.v0=.5/baseHeight;result.u1=(image.width-.5)/baseWidth;result.v1=(image.height-.5)/baseHeight;
  liveBytes_+=result.bytes;++created_;return result;
}
void NativeTextures::release(Texture& texture){if(!texture.id)return;glDeleteTextures(1,&texture.id);liveBytes_-=texture.bytes;++deleted_;texture={};}
void NativeTextures::clearViewer(){release(viewer_);}
void NativeTextures::clear(){clearViewer();for(auto& [key,t]:images_)release(t);for(auto& [key,t]:texts_)release(t);images_.clear();texts_.clear();jobs_.clear();completed_=0;}
void NativeTextures::begin(const World& world){
  clear();if(world.images.size()>128)throw std::runtime_error("World has more than 128 image resources");
  try{
    std::vector<Job> plaques;std::size_t textBytes=0;
    for(const auto& node:world.nodes)if(!node.text.empty()){
      if(plaques.size()>=16)throw std::runtime_error("World has more than 16 text plaques");
      const auto material=resolveStyle(node,world);const double aspect=node.geometry.size[0]/node.geometry.size[1];
      plaques.push_back({node.id,node.text,material.color,{},aspect,material.textBackground});textBytes+=mipBytes({1024,textHeight(aspect)});
    }
    struct PlannedImage {Job job;ImageSize source;std::size_t bytes;};std::vector<PlannedImage> images;std::size_t imageBytes=0;
    if(!world.images.empty()){
      Apartment apartment;ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
      for(const auto& [key,path]:world.images){
        try{
          auto frame=imageFrame(factory.Get(),path);const auto size=imageSize(frame.Get());Job job{key,"","",path,1};
          job.maxDimension=std::min(2048u,powerOfTwo(std::max(size.width,size.height)));const auto bytes=mipBytes(scaledSize(size,job.maxDimension));
          images.push_back({std::move(job),size,bytes});imageBytes+=bytes;
        }catch(const std::exception& error){throw std::runtime_error("Image resource "+key+": "+error.what());}
      }
    }
    // Preserve authored detail up to 2048. Reduce the largest allocations first
    // until the entire scene, including text and all mip levels, fits 150 MiB.
    // The separate 256 MiB hard limit also covers the full-resolution viewer.
    const auto imageBudget=sceneBudget-textBytes;
    while(imageBytes>imageBudget){
      auto largest=images.end();
      for(auto it=images.begin();it!=images.end();++it)if(it->job.maxDimension>64&&(largest==images.end()||it->bytes>largest->bytes))largest=it;
      if(largest==images.end())throw std::runtime_error("World images cannot fit the scene texture budget");
      imageBytes-=largest->bytes;largest->job.maxDimension/=2;largest->bytes=mipBytes(scaledSize(largest->source,largest->job.maxDimension));imageBytes+=largest->bytes;
    }
    for(auto& image:images)jobs_.push_back(std::move(image.job));for(auto& plaque:plaques)jobs_.push_back(std::move(plaque));
  }catch(...){clear();throw;}
}
void NativeTextures::step(unsigned maxItems){
  try{for(unsigned i=0;i<maxItems&&loading();++i){const auto& job=jobs_[completed_];if(job.text.empty())images_.emplace(job.key,upload(decodeImage(job.path,job.maxDimension)));else{auto texture=upload(rasterText(job.text,job.aspect,job.color,job.textBackground));texture.textBackground=job.textBackground;texts_.emplace(job.key,texture);}++completed_;}}
  catch(const std::exception& error){const auto key=completed_<jobs_.size()?jobs_[completed_].key:std::string("unknown");const auto message=std::string("Image resource ")+key+": "+error.what();clear();throw std::runtime_error(message);}
  catch(...){clear();throw;}
}
void NativeTextures::load(const World& world,LogFn log){
  begin(world);while(loading())step();
  if(log)log(false,"Native photo textures: "+std::to_string(images_.size())+" images + "+std::to_string(texts_.size())+" plaques, "+std::to_string(liveBytes_/1024/1024)+" MiB");
}
const Texture* NativeTextures::image(const std::string& key)const{auto found=images_.find(key);return found==images_.end()?nullptr:&found->second;}
const Texture* NativeTextures::text(const std::string& key)const{auto found=texts_.find(key);return found==texts_.end()?nullptr:&found->second;}
const Texture& NativeTextures::viewer(const std::filesystem::path& path){clearViewer();viewer_=upload(decodeImage(path,2048));return viewer_;}
} // namespace linkworld
