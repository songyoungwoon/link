#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "link/model_renderer.hpp"
#include "link/model.hpp"
#include "link/environment.hpp"
#include "model_shaders.hpp"
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace linkworld {
namespace {
using Microsoft::WRL::ComPtr;
constexpr GLenum vertexShader=0x8B31,fragmentShader=0x8B30,compileStatus=0x8B81,linkStatus=0x8B82;
constexpr GLenum infoLogLength=0x8B84,currentProgram=0x8B8D,arrayBuffer=0x8892,elementBuffer=0x8893;
constexpr GLenum arrayBufferBinding=0x8894,elementBufferBinding=0x8895,staticDraw=0x88E4,dynamicDraw=0x88E8;
constexpr GLenum texture0=0x84C0,activeTexture=0x84E0,clientActiveTexture=0x84E1;
constexpr GLenum framebuffer=0x8D40,drawFramebuffer=0x8CA9,readFramebuffer=0x8CA8,depthAttachment=0x8D00,framebufferComplete=0x8CD5;
constexpr GLenum drawFramebufferBinding=0x8CA6,readFramebufferBinding=0x8CAA;
constexpr std::size_t legacyGpuBudget=32ull*1024*1024,lightingGpuBudget=192ull*1024*1024;
using BufferSize=std::ptrdiff_t;

struct GlApi {
#define LINK_GL_PROC(name,result,args) using name##Fn=result (APIENTRY*)args;name##Fn name=nullptr
  LINK_GL_PROC(CreateShader,GLuint,(GLenum));
  LINK_GL_PROC(ShaderSource,void,(GLuint,GLsizei,const char* const*,const GLint*));
  LINK_GL_PROC(CompileShader,void,(GLuint));
  LINK_GL_PROC(GetShaderiv,void,(GLuint,GLenum,GLint*));
  LINK_GL_PROC(GetShaderInfoLog,void,(GLuint,GLsizei,GLsizei*,char*));
  LINK_GL_PROC(DeleteShader,void,(GLuint));
  LINK_GL_PROC(CreateProgram,GLuint,());
  LINK_GL_PROC(AttachShader,void,(GLuint,GLuint));
  LINK_GL_PROC(LinkProgram,void,(GLuint));
  LINK_GL_PROC(GetProgramiv,void,(GLuint,GLenum,GLint*));
  LINK_GL_PROC(GetProgramInfoLog,void,(GLuint,GLsizei,GLsizei*,char*));
  LINK_GL_PROC(DeleteProgram,void,(GLuint));
  LINK_GL_PROC(UseProgram,void,(GLuint));
  LINK_GL_PROC(GetUniformLocation,GLint,(GLuint,const char*));
  LINK_GL_PROC(Uniform1i,void,(GLint,GLint));
  LINK_GL_PROC(UniformMatrix4fv,void,(GLint,GLsizei,GLboolean,const GLfloat*));
  LINK_GL_PROC(GenFramebuffers,void,(GLsizei,GLuint*));
  LINK_GL_PROC(DeleteFramebuffers,void,(GLsizei,const GLuint*));
  LINK_GL_PROC(BindFramebuffer,void,(GLenum,GLuint));
  LINK_GL_PROC(FramebufferTexture2D,void,(GLenum,GLenum,GLenum,GLuint,GLint));
  LINK_GL_PROC(CheckFramebufferStatus,GLenum,(GLenum));
  LINK_GL_PROC(Uniform1f,void,(GLint,GLfloat));
  LINK_GL_PROC(Uniform3f,void,(GLint,GLfloat,GLfloat,GLfloat));
  LINK_GL_PROC(Uniform4f,void,(GLint,GLfloat,GLfloat,GLfloat,GLfloat));
  LINK_GL_PROC(GenBuffers,void,(GLsizei,GLuint*));
  LINK_GL_PROC(DeleteBuffers,void,(GLsizei,const GLuint*));
  LINK_GL_PROC(BindBuffer,void,(GLenum,GLuint));
  LINK_GL_PROC(BufferData,void,(GLenum,BufferSize,const void*,GLenum));
  LINK_GL_PROC(BufferSubData,void,(GLenum,BufferSize,BufferSize,const void*));
  LINK_GL_PROC(ActiveTexture,void,(GLenum));
  LINK_GL_PROC(ClientActiveTexture,void,(GLenum));
#undef LINK_GL_PROC
  template<class T> static T function(const char* name){
    PROC p=wglGetProcAddress(name);
    if(!p||p==reinterpret_cast<PROC>(1)||p==reinterpret_cast<PROC>(2)||p==reinterpret_cast<PROC>(3)||p==reinterpret_cast<PROC>(-1))
      throw std::runtime_error(std::string("GLB rendering requires OpenGL 2.1 compatibility and graphics drivers providing ")+name+". Update the GPU driver or use a PC with shader support.");
    return reinterpret_cast<T>(p);
  }
  void loadFramebuffers(){
    GenFramebuffers=function<GenFramebuffersFn>("glGenFramebuffers");DeleteFramebuffers=function<DeleteFramebuffersFn>("glDeleteFramebuffers");
    BindFramebuffer=function<BindFramebufferFn>("glBindFramebuffer");FramebufferTexture2D=function<FramebufferTexture2DFn>("glFramebufferTexture2D");CheckFramebufferStatus=function<CheckFramebufferStatusFn>("glCheckFramebufferStatus");
  }
  void load(){
#define LINK_GL_LOAD(name) name=function<name##Fn>("gl" #name)
    LINK_GL_LOAD(CreateShader);LINK_GL_LOAD(ShaderSource);LINK_GL_LOAD(CompileShader);LINK_GL_LOAD(GetShaderiv);LINK_GL_LOAD(GetShaderInfoLog);LINK_GL_LOAD(DeleteShader);
    LINK_GL_LOAD(CreateProgram);LINK_GL_LOAD(AttachShader);LINK_GL_LOAD(LinkProgram);LINK_GL_LOAD(GetProgramiv);LINK_GL_LOAD(GetProgramInfoLog);LINK_GL_LOAD(DeleteProgram);LINK_GL_LOAD(UseProgram);
    LINK_GL_LOAD(GetUniformLocation);LINK_GL_LOAD(Uniform1i);LINK_GL_LOAD(UniformMatrix4fv);LINK_GL_LOAD(Uniform1f);LINK_GL_LOAD(Uniform3f);LINK_GL_LOAD(Uniform4f);
    LINK_GL_LOAD(GenBuffers);LINK_GL_LOAD(DeleteBuffers);LINK_GL_LOAD(BindBuffer);LINK_GL_LOAD(BufferData);LINK_GL_LOAD(BufferSubData);LINK_GL_LOAD(ActiveTexture);LINK_GL_LOAD(ClientActiveTexture);
#undef LINK_GL_LOAD
  }
};

void checked(HRESULT result,const char* operation){
  if(FAILED(result)){std::ostringstream message;message<<operation<<" failed (HRESULT 0x"<<std::hex<<static_cast<unsigned>(result)<<")";throw std::runtime_error(message.str());}
}
struct Apartment {
  HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  Apartment(){if(result!=RPC_E_CHANGED_MODE)checked(result,"GLB image COM initialization");}
  ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
struct Pixels {unsigned width=0,height=0;std::vector<std::uint8_t> rgba;};
Pixels decodeEmbedded(IWICImagingFactory* factory,const ModelImage& image){
  if(image.bytes.empty()||image.bytes.size()>std::numeric_limits<DWORD>::max())throw std::runtime_error("GLB embedded image has invalid size");
  ComPtr<IWICStream> stream;checked(factory->CreateStream(&stream),"GLB WIC memory stream");
  checked(stream->InitializeFromMemory(const_cast<BYTE*>(image.bytes.data()),static_cast<DWORD>(image.bytes.size())),"GLB WIC image bytes");
  ComPtr<IWICBitmapDecoder> decoder;checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder),"GLB PNG/JPEG decoder");
  GUID container{};checked(decoder->GetContainerFormat(&container),"GLB image format");
  if(container!=GUID_ContainerFormatPng&&container!=GUID_ContainerFormatJpeg)throw std::runtime_error("GLB embedded images must be PNG or JPEG");
  ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"GLB image frame");
  UINT w=0,h=0;checked(frame->GetSize(&w,&h),"GLB image dimensions");
  if(!w||!h||w>8192||h>8192)throw std::runtime_error("GLB image dimensions must be between 1 and 8192 pixels");
  const double scale=std::min(1.,2048./std::max(w,h));
  Pixels result;result.width=std::max(1u,static_cast<unsigned>(std::lround(w*scale)));result.height=std::max(1u,static_cast<unsigned>(std::lround(h*scale)));
  ComPtr<IWICBitmapSource> source;checked(frame.As(&source),"GLB WIC source");
  ComPtr<IWICBitmapScaler> scaler;
  if(result.width!=w||result.height!=h){
    checked(factory->CreateBitmapScaler(&scaler),"GLB WIC scaler");
    checked(scaler->Initialize(source.Get(),result.width,result.height,WICBitmapInterpolationModeFant),"GLB image scaling");checked(scaler.As(&source),"GLB scaled image");
  }
  ComPtr<IWICFormatConverter> converter;checked(factory->CreateFormatConverter(&converter),"GLB RGBA converter");
  checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"GLB RGBA format");
  result.rgba.resize(static_cast<std::size_t>(result.width)*result.height*4);
  checked(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.rgba.size()),result.rgba.data()),"GLB image pixels");
  return result;
}
std::vector<std::uint8_t> smallerMip(const Pixels& input){
  unsigned w=std::max(1u,input.width/2),h=std::max(1u,input.height/2);
  std::vector<std::uint8_t> result(static_cast<std::size_t>(w)*h*4);
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
    unsigned alpha=0;std::array<unsigned,3> weighted{},plain{};
    for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
      auto at=(static_cast<std::size_t>(std::min(input.height-1,y*2+dy))*input.width+std::min(input.width-1,x*2+dx))*4;
      const unsigned a=input.rgba[at+3];alpha+=a;
      for(unsigned c=0;c<3;++c){weighted[c]+=unsigned(input.rgba[at+c])*a;plain[c]+=input.rgba[at+c];}
    }
    auto at=(static_cast<std::size_t>(y)*w+x)*4;
    for(unsigned c=0;c<3;++c)result[at+c]=static_cast<std::uint8_t>(alpha?(weighted[c]+alpha/2)/alpha:(plain[c]+2)/4);
    result[at+3]=static_cast<std::uint8_t>((alpha+2)/4);
  }
  return result;
}
std::size_t mipBytes(unsigned w,unsigned h){
  std::size_t bytes=0;for(;;){bytes+=static_cast<std::size_t>(w)*h*4;if(w==1&&h==1)return bytes;w=std::max(1u,w/2);h=std::max(1u,h/2);}
}
struct PackedVertex {GLfloat position[3],normal[3],uv[2],color[4];};
std::vector<PackedVertex> packed(const std::vector<ModelDrawVertex>& vertices){
  std::vector<PackedVertex> result(vertices.size());
  for(std::size_t i=0;i<vertices.size();++i){const auto& a=vertices[i];auto& b=result[i];
    for(unsigned c=0;c<3;++c){b.position[c]=static_cast<float>(a.position[c]);b.normal[c]=static_cast<float>(a.normal[c]);}
    for(unsigned c=0;c<2;++c)b.uv[c]=static_cast<float>(a.uv[c]);
    for(unsigned c=0;c<4;++c)b.color[c]=static_cast<float>(a.color[c]);
  }
  return result;
}
Vec3 color(const std::string& text){
  if(text.size()!=7||text[0]!='#')throw std::runtime_error("GLB Style color must use #rrggbb");
  const auto n=std::stoul(text.substr(1),nullptr,16);
  return {double((n>>16)&255)/255,double((n>>8)&255)/255,double(n&255)/255};
}
float determinant(const GLdouble* m){return static_cast<float>(m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]));}

ModelMatrix identityMatrix(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
ModelMatrix multiplyMatrix(const ModelMatrix& a,const ModelMatrix& b){ModelMatrix out{};for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)out[col*4+row]+=a[k*4+row]*b[col*4+k];return out;}
ModelMatrix inverseMatrix(const ModelMatrix& m){
 double a[4][8]{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){a[r][c]=m[c*4+r];a[r][c+4]=r==c?1:0;}
 for(unsigned c=0;c<4;++c){unsigned best=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[best][c]))best=r;
  if(std::abs(a[best][c])<1e-18)throw std::runtime_error("Singular lighting transform");if(best!=c)for(unsigned j=0;j<8;++j)std::swap(a[best][j],a[c][j]);
  const auto scale=a[c][c];for(auto& value:a[c])value/=scale;
  for(unsigned r=0;r<4;++r)if(r!=c){const auto amount=a[r][c];for(unsigned j=0;j<8;++j)a[r][j]-=amount*a[c][j];}
 }
 ModelMatrix out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[c*4+r]=a[r][c+4];return out;
}
ModelMatrix normalMatrix(const ModelMatrix& m){const auto inv=inverseMatrix(m);ModelMatrix out{};for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)out[c*4+r]=inv[r*4+c];return out;}
ModelMatrix localMatrix(const Transform& transform){
 auto out=identityMatrix();for(unsigned a=0;a<3;++a)out[12+a]=transform.position[a];
 for(unsigned axis=0;axis<3;++axis){auto rotation=identityMatrix();const double angle=transform.rotation[axis]*3.141592653589793/180,c=std::cos(angle),sn=std::sin(angle);
  if(axis==0){rotation[5]=c;rotation[6]=sn;rotation[9]=-sn;rotation[10]=c;}if(axis==1){rotation[0]=c;rotation[2]=-sn;rotation[8]=sn;rotation[10]=c;}if(axis==2){rotation[0]=c;rotation[1]=sn;rotation[4]=-sn;rotation[5]=c;}out=multiplyMatrix(out,rotation);
 }auto scale=identityMatrix();for(unsigned a=0;a<3;++a)scale[a*5]=transform.scale[a];return multiplyMatrix(out,scale);
}
Vec3 cross3(Vec3 a,Vec3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double dot3(Vec3 a,Vec3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec3 unit3(Vec3 a){const auto n=std::sqrt(dot3(a,a));if(n<1e-15)return {0,1,0};for(auto& v:a)v/=n;return a;}
Vec3 linearColor(const std::string& text){auto out=color(text);for(auto& c:out)c=std::pow(c,2.2);return out;}
ModelMatrix sunViewProjection(const LightingSettings& lighting){
 const auto direction=unit3(lighting.sunDirection);Vec3 eye{},forward{};for(unsigned a=0;a<3;++a){eye[a]=lighting.shadow.center[a]+direction[a]*lighting.shadow.extent*2;forward[a]=-direction[a];}
 const Vec3 up=std::abs(direction[1])>.98?Vec3{0,0,1}:Vec3{0,1,0};const auto right=unit3(cross3(forward,up)),vertical=cross3(right,forward);
 ModelMatrix view{right[0],vertical[0],-forward[0],0,right[1],vertical[1],-forward[1],0,right[2],vertical[2],-forward[2],0,-dot3(right,eye),-dot3(vertical,eye),dot3(forward,eye),1};
 auto projection=identityMatrix();const double nearZ=.1,farZ=lighting.shadow.extent*4+.1;projection[0]=projection[5]=1/lighting.shadow.extent;projection[10]=-2/(farZ-nearZ);projection[14]=-(farZ+nearZ)/(farZ-nearZ);return multiplyMatrix(projection,view);
}
void matrixUniform(GlApi& gl,GLuint program,const char* name,const ModelMatrix& matrix){std::array<GLfloat,16> values{};std::transform(matrix.begin(),matrix.end(),values.begin(),[](double value){return static_cast<GLfloat>(value);});gl.UniformMatrix4fv(gl.GetUniformLocation(program,name),1,GL_FALSE,values.data());}
struct FramebufferState {
 GlApi& gl;GLint draw=0,read=0,drawBuffer=0,readBuffer=0,mode=GL_MODELVIEW;
 explicit FramebufferState(GlApi& api):gl(api){glGetIntegerv(drawFramebufferBinding,&draw);glGetIntegerv(readFramebufferBinding,&read);glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_MATRIX_MODE,&mode);glMatrixMode(GL_PROJECTION);glPushMatrix();glMatrixMode(GL_MODELVIEW);glPushMatrix();}
 ~FramebufferState(){glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(static_cast<GLenum>(mode));gl.BindFramebuffer(drawFramebuffer,static_cast<GLuint>(draw));gl.BindFramebuffer(readFramebuffer,static_cast<GLuint>(read));glDrawBuffer(static_cast<GLenum>(drawBuffer));glReadBuffer(static_cast<GLenum>(readBuffer));}
};

// Restore both compatibility state and programmable bindings. Never leave a
// shader, VBO, client array, blend function or texture unit in primitive drawing.
struct DrawState {
  GlApi& gl;GLint program=0,active=texture0,client=texture0,array=0,elements=0;
  explicit DrawState(GlApi& api):gl(api){
    glGetIntegerv(currentProgram,&program);glGetIntegerv(activeTexture,&active);glGetIntegerv(clientActiveTexture,&client);
    glGetIntegerv(arrayBufferBinding,&array);glGetIntegerv(elementBufferBinding,&elements);
    glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_POLYGON_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_LIGHTING_BIT|GL_VIEWPORT_BIT|GL_SCISSOR_BIT|GL_TRANSFORM_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT|GL_CLIENT_PIXEL_STORE_BIT);
    gl.ActiveTexture(texture0);gl.ClientActiveTexture(texture0);
  }
  ~DrawState(){
    glPopClientAttrib();gl.BindBuffer(arrayBuffer,static_cast<GLuint>(array));gl.BindBuffer(elementBuffer,static_cast<GLuint>(elements));
    glPopAttrib();gl.UseProgram(static_cast<GLuint>(program));gl.ActiveTexture(static_cast<GLenum>(active));gl.ClientActiveTexture(static_cast<GLenum>(client));
  }
};
} // namespace

struct NativeModels::Impl {
  struct Batch {
    std::size_t node=0;const ModelPrimitive* primitive=nullptr;
    GLuint vertices=0,indices=0;GLsizei count=0;
    Vec3 center{0,0,0};bool reflected=false;
  };
  struct Asset {
    std::shared_ptr<const ModelAsset> model;
    std::vector<GLuint> images;
    std::vector<Batch> batches;
    std::string cachedClip;double cachedTime=std::numeric_limits<double>::quiet_NaN();bool cachedLoop=true;
    bool animated=false;
  };
  GlApi gl;GLuint program=0,depthProgram=0,skyProgram=0,shadowTexture=0,shadowFramebuffer=0,radianceTexture=0,irradianceTexture=0,specularTexture=0;std::map<std::string,Asset> assets;
  bool advanced=false,shadowReady=false;std::size_t gpuLimit=legacyGpuBudget,mapCount=0,shadowPasses=0;std::uint64_t shadowHash=0;
  ModelMatrix lightViewProjection=identityMatrix();
  std::size_t gpuBytes=0,triangleCount=0;
  std::map<std::string,GLint> uniforms;

  GLint uniform(const char* name){auto found=uniforms.find(name);if(found!=uniforms.end())return found->second;const auto value=gl.GetUniformLocation(program,name);uniforms.emplace(name,value);return value;}
  void integer(const char* name,int value){gl.Uniform1i(uniform(name),value);}
  void scalar(const char* name,double value){gl.Uniform1f(uniform(name),static_cast<float>(value));}
  void vector(const char* name,const Vec3& value){gl.Uniform3f(uniform(name),static_cast<float>(value[0]),static_cast<float>(value[1]),static_cast<float>(value[2]));}
  void budget(std::size_t bytes){if(bytes>gpuLimit-gpuBytes)throw std::runtime_error("Model graphics exceed the "+std::to_string(gpuLimit/1024/1024)+" MiB GPU budget. Reduce mesh detail or texture resolution.");gpuBytes+=bytes;}
  GLuint compile(GLenum type,const char* source){
    const auto shader=gl.CreateShader(type);if(!shader)throw std::runtime_error("Could not allocate GLB shader");
    gl.ShaderSource(shader,1,&source,nullptr);gl.CompileShader(shader);GLint success=0;gl.GetShaderiv(shader,compileStatus,&success);
    if(!success){std::array<char,4096> log{};gl.GetShaderInfoLog(shader,static_cast<GLsizei>(log.size()-1),nullptr,log.data());gl.DeleteShader(shader);throw std::runtime_error(std::string("GLB shader compilation failed. Update the graphics driver. ")+log.data());}
    return shader;
  }
  GLuint makeProgram(const char* vertexSource,const char* fragmentSource){
    GLuint vertex=0,fragment=0,result=0;
    try{vertex=compile(vertexShader,vertexSource);fragment=compile(fragmentShader,fragmentSource);result=gl.CreateProgram();if(!result)throw std::runtime_error("Could not allocate model program");gl.AttachShader(result,vertex);gl.AttachShader(result,fragment);gl.LinkProgram(result);GLint okay=0;gl.GetProgramiv(result,linkStatus,&okay);if(!okay){std::array<char,4096> log{};gl.GetProgramInfoLog(result,static_cast<GLsizei>(log.size()-1),nullptr,log.data());throw std::runtime_error(std::string("Model program link failed: ")+log.data());}}
    catch(...){if(vertex)gl.DeleteShader(vertex);if(fragment)gl.DeleteShader(fragment);if(result)gl.DeleteProgram(result);throw;}
    gl.DeleteShader(vertex);gl.DeleteShader(fragment);return result;
  }
  void initialize(bool lighting){
    advanced=lighting;gpuLimit=lighting?lightingGpuBudget:legacyGpuBudget;gl.load();
    std::string fragment="#version 120\n";
    if(lighting)fragment+="#extension GL_ARB_shader_texture_lod : require\n#define LINK_ADVANCED_LIGHTING 1\n";
    fragment+=model_shaders::fragment;program=makeProgram(model_shaders::vertex,fragment.c_str());
    if(lighting){GLint units=0;glGetIntegerv(0x8872,&units);if(units<8)throw std::runtime_error("lighting-v1 requires at least eight fragment texture units");depthProgram=makeProgram(model_shaders::depthVertex,model_shaders::depthFragment);skyProgram=makeProgram(model_shaders::skyVertex,model_shaders::skyFragment);}
  }
  void bindImage(unsigned unit,GLuint texture){gl.ActiveTexture(texture0+unit);glBindTexture(GL_TEXTURE_2D,texture);}
  GLuint environmentTexture(const std::vector<EnvironmentImage>& levels){
    GLuint id=0;glGenTextures(1,&id);if(!id)throw std::runtime_error("Could not allocate HDR environment");
    try{glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,levels.size()>1?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,0x813D,static_cast<GLint>(levels.size()-1));
      for(std::size_t level=0;level<levels.size();++level){const auto& image=levels[level];budget(static_cast<std::size_t>(image.width)*image.height*12);glTexImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),0x8815,image.width,image.height,0,GL_RGB,GL_FLOAT,image.rgb.data());if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("HDR float texture upload failed");}
    }catch(...){glDeleteTextures(1,&id);throw;}return id;
  }
  void createShadow(const LightingSettings& lighting){
    if(!lighting.shadow.enabled)return;gl.loadFramebuffers();DrawState state(gl);FramebufferState frame(gl);
    gl.ActiveTexture(texture0);glGenTextures(1,&shadowTexture);if(!shadowTexture)throw std::runtime_error("Could not allocate shadow depth texture");
    glBindTexture(GL_TEXTURE_2D,shadowTexture);const auto resolution=lighting.shadow.resolution;budget(static_cast<std::size_t>(resolution)*resolution*4);
    glTexImage2D(GL_TEXTURE_2D,0,0x81A6,resolution,resolution,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812D);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812D);GLfloat white[]={1,1,1,1};glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,white);glTexParameteri(GL_TEXTURE_2D,0x884C,0x884E);glTexParameteri(GL_TEXTURE_2D,0x884D,GL_LEQUAL);
    gl.GenFramebuffers(1,&shadowFramebuffer);if(!shadowFramebuffer)throw std::runtime_error("Could not allocate shadow framebuffer");gl.BindFramebuffer(framebuffer,shadowFramebuffer);gl.FramebufferTexture2D(framebuffer,depthAttachment,GL_TEXTURE_2D,shadowTexture,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    if(gl.CheckFramebufferStatus(framebuffer)!=framebufferComplete||glGetError()!=GL_NO_ERROR)throw std::runtime_error("lighting-v1 requires a complete depth-texture framebuffer");
    lightViewProjection=sunViewProjection(lighting);
  }
  void pose(Asset& asset,const RuntimeNode& node){if(asset.animated&&(asset.cachedClip!=node.animation.clip||asset.cachedTime!=node.animation.time||asset.cachedLoop!=node.animation.loop)){update(asset,evaluateModel(*asset.model,node.animation.clip,node.animation.time,node.animation.loop));asset.cachedClip=node.animation.clip;asset.cachedTime=node.animation.time;asset.cachedLoop=node.animation.loop;}}
  GLuint texture(const Pixels& decoded){
    budget(mipBytes(decoded.width,decoded.height));GLuint id=0;glGenTextures(1,&id);if(!id)throw std::runtime_error("Could not allocate GLB texture");
    try{
      glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1);Pixels level=decoded;int mip=0;
      for(;;){
        glTexImage2D(GL_TEXTURE_2D,mip,GL_RGBA,level.width,level.height,0,GL_RGBA,GL_UNSIGNED_BYTE,level.rgba.data());
        if(const auto error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("GLB texture upload failed (OpenGL "+std::to_string(error)+")");
        if(level.width==1&&level.height==1)break;
        auto next=smallerMip(level);level.width=std::max(1u,level.width/2);level.height=std::max(1u,level.height/2);level.rgba=std::move(next);++mip;
      }
    }catch(...){glDeleteTextures(1,&id);throw;}
    return id;
  }
  void update(Asset& asset,const ModelFrame& frame){
    for(auto& batch:asset.batches){
      auto vertices=packed(deformModelPrimitive(*asset.model,frame,batch.node,*batch.primitive));
      Vec3 center{0,0,0};
      for(const auto& vertex:vertices)for(unsigned c=0;c<3;++c)center[c]+=vertex.position[c];
      if(!vertices.empty())for(auto& c:center)c/=vertices.size();batch.center=center;
      batch.reflected=batch.node<frame.nodes.size()&&determinant(frame.nodes[batch.node].data())<0;
      gl.BindBuffer(arrayBuffer,batch.vertices);gl.BufferSubData(arrayBuffer,0,static_cast<BufferSize>(vertices.size()*sizeof(PackedVertex)),vertices.data());
    }
  }
  void geometry(const Batch& batch){
    gl.BindBuffer(arrayBuffer,batch.vertices);gl.BindBuffer(elementBuffer,batch.indices);
    glVertexPointer(3,GL_FLOAT,sizeof(PackedVertex),reinterpret_cast<const void*>(offsetof(PackedVertex,position)));
    glNormalPointer(GL_FLOAT,sizeof(PackedVertex),reinterpret_cast<const void*>(offsetof(PackedVertex,normal)));
    glTexCoordPointer(2,GL_FLOAT,sizeof(PackedVertex),reinterpret_cast<const void*>(offsetof(PackedVertex,uv)));
    glColorPointer(4,GL_FLOAT,sizeof(PackedVertex),reinterpret_cast<const void*>(offsetof(PackedVertex,color)));
    glDrawElements(GL_TRIANGLES,batch.count,GL_UNSIGNED_INT,nullptr);
  }
};

NativeModels::NativeModels():impl_(std::make_unique<Impl>()){}
NativeModels::~NativeModels(){clear();}
void NativeModels::clear() noexcept {
  if(!impl_)return;auto& p=*impl_;
  // Context deletion frees GPU objects when a caller could not make it current.
  // Normal navigation and shutdown explicitly call clear() with the context set.
  if(wglGetCurrentContext()){
    for(auto& entry:p.assets){auto& asset=entry.second;
      if(!asset.images.empty())glDeleteTextures(static_cast<GLsizei>(asset.images.size()),asset.images.data());
      if(p.gl.DeleteBuffers)for(auto& batch:asset.batches){if(batch.vertices)p.gl.DeleteBuffers(1,&batch.vertices);if(batch.indices)p.gl.DeleteBuffers(1,&batch.indices);}
    }
    if(p.gl.DeleteFramebuffers&&p.shadowFramebuffer)p.gl.DeleteFramebuffers(1,&p.shadowFramebuffer);
    for(auto id:{p.shadowTexture,p.radianceTexture,p.irradianceTexture,p.specularTexture})if(id)glDeleteTextures(1,&id);
    for(auto id:{p.program,p.depthProgram,p.skyProgram})if(id&&p.gl.DeleteProgram)p.gl.DeleteProgram(id);
  }
  p.assets.clear();p.uniforms.clear();p.program=p.depthProgram=p.skyProgram=p.shadowTexture=p.shadowFramebuffer=p.radianceTexture=p.irradianceTexture=p.specularTexture=0;p.gpuBytes=p.triangleCount=p.mapCount=p.shadowPasses=0;p.advanced=p.shadowReady=false;p.shadowHash=0;
}
void NativeModels::load(const World& world){
  clear();if(world.models.empty()&&!world.lighting)return;
  if(!wglGetCurrentContext())throw std::runtime_error("GLB model loading requires the browser OpenGL context");
  auto& p=*impl_;
  try{
    p.initialize(world.lighting.has_value());DrawState state(p.gl);Apartment apartment;
    ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"GLB WIC factory");
    // Clear errors from older fixed-function work before checking our uploads.
    while(glGetError()!=GL_NO_ERROR){}
    if(world.lighting){
      if(const auto& environment=world.lighting->environment){p.radianceTexture=p.environmentTexture({environment->radiance});p.irradianceTexture=p.environmentTexture({environment->irradiance});p.specularTexture=p.environmentTexture(environment->specularMips);}
      p.createShadow(*world.lighting);
    }
    for(const auto& entry:world.models){
      auto& target=p.assets[entry.first];target.model=entry.second;
      if(!target.model)throw std::runtime_error("World contains an empty GLB model");
      const auto& model=*target.model;for(const auto& material:model.materials)for(auto image:{material.baseColorImage,material.normalImage,material.metallicRoughnessImage,material.occlusionImage,material.emissiveImage})if(image>=0)++p.mapCount;target.animated=!model.animations.empty()||!model.skins.empty();
      target.images.reserve(model.images.size()); // Reserve before ownership transfers from texture().
      for(const auto& image:model.images)target.images.push_back(p.texture(decodeEmbedded(factory.Get(),image)));
      const auto frame=evaluateModel(model,"",0);
      for(std::size_t node=0;node<model.nodes.size();++node){
        if(node>=model.sceneActive.size()||!model.sceneActive[node]||model.nodes[node].mesh<0)continue;
        const auto mesh=static_cast<std::size_t>(model.nodes[node].mesh);
        if(mesh>=model.meshes.size())throw std::runtime_error("GLB node references a missing mesh");
        for(const auto& primitive:model.meshes[mesh].primitives){
          target.batches.emplace_back();auto& batch=target.batches.back();batch.node=node;batch.primitive=&primitive;
          auto vertices=packed(deformModelPrimitive(model,frame,node,primitive));
          if(primitive.indices.size()>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))throw std::runtime_error("GLB mesh has too many indices");
          batch.count=static_cast<GLsizei>(primitive.indices.size());
          const auto vertexBytes=vertices.size()*sizeof(PackedVertex),indexBytes=primitive.indices.size()*sizeof(std::uint32_t);p.budget(vertexBytes+indexBytes);
          p.gl.GenBuffers(1,&batch.vertices);p.gl.GenBuffers(1,&batch.indices);if(!batch.vertices||!batch.indices)throw std::runtime_error("Could not allocate GLB mesh buffers");
          p.gl.BindBuffer(arrayBuffer,batch.vertices);p.gl.BufferData(arrayBuffer,static_cast<BufferSize>(vertexBytes),vertices.data(),target.animated?dynamicDraw:staticDraw);
          p.gl.BindBuffer(elementBuffer,batch.indices);p.gl.BufferData(elementBuffer,static_cast<BufferSize>(indexBytes),primitive.indices.data(),staticDraw);
          if(const auto error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("GLB mesh upload failed (OpenGL "+std::to_string(error)+")");
          for(const auto& vertex:vertices)for(unsigned c=0;c<3;++c)batch.center[c]+=vertex.position[c];
          if(!vertices.empty())for(auto& c:batch.center)c/=vertices.size();
          batch.reflected=node<frame.nodes.size()&&determinant(frame.nodes[node].data())<0;
          p.triangleCount+=primitive.indices.size()/3;
        }
      }
    }
  }catch(...){clear();throw;}
}
void NativeModels::prepareScene(const Runtime& runtime){
 auto& p=*impl_;if(!p.shadowFramebuffer||!runtime.world().lighting)return;
 const auto& nodes=runtime.nodes();std::vector<ModelMatrix> matrices(nodes.size());std::vector<bool> visible(nodes.size());
 std::uint64_t hash=1469598103934665603ull;const auto add=[&](std::uint64_t value){hash^=value;hash*=1099511628211ull;};
 for(std::size_t i=0;i<nodes.size();++i){const auto& node=nodes[i];const auto parent=node.definition.parent;matrices[i]=localMatrix(node.transform);if(parent!=noParent)matrices[i]=multiplyMatrix(matrices.at(parent),matrices[i]);visible[i]=node.transform.visible&&(parent==noParent||visible.at(parent));
  if(node.definition.kind!=Kind::Element||node.definition.geometry.type!=Geometry::Type::Model)continue;
  add(i);add(visible[i]);for(auto value:matrices[i])add(std::bit_cast<std::uint64_t>(value));add(std::bit_cast<std::uint64_t>(runtime.material(i).opacity));add(std::bit_cast<std::uint64_t>(node.animation.time));add(node.animation.loop);for(auto c:node.animation.clip)add(static_cast<unsigned char>(c));
 }
 if(p.shadowReady&&hash==p.shadowHash)return;
 DrawState state(p.gl);FramebufferState frame(p.gl);const auto& lighting=*runtime.world().lighting;
 p.gl.BindFramebuffer(framebuffer,p.shadowFramebuffer);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);glViewport(0,0,lighting.shadow.resolution,lighting.shadow.resolution);
 glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
 glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.5f,2.f);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
 glMatrixMode(GL_PROJECTION);glLoadMatrixd(p.lightViewProjection.data());glMatrixMode(GL_MODELVIEW);
 p.gl.UseProgram(p.depthProgram);p.gl.Uniform1i(p.gl.GetUniformLocation(p.depthProgram,"colorMap"),0);
 glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
 static const ModelMaterial fallback;
 for(std::size_t i=0;i<nodes.size();++i){const auto& node=nodes[i];if(!visible[i]||node.definition.kind!=Kind::Element||node.definition.geometry.type!=Geometry::Type::Model||runtime.material(i).opacity<1)continue;
  const auto found=p.assets.find(node.definition.geometry.model);if(found==p.assets.end())continue;auto& asset=found->second;p.pose(asset,node);glLoadMatrixd(matrices[i].data());
  const bool reflected=determinant(matrices[i].data())<0;
  for(const auto& batch:asset.batches){const auto material=batch.primitive->material;const auto& imported=material>=0?asset.model->materials.at(material):fallback;if(imported.alphaMode=="BLEND")continue;
   const auto image=imported.baseColorImage;const auto texture=image>=0?asset.images.at(image):0;p.bindImage(0,texture);
   p.gl.Uniform1i(p.gl.GetUniformLocation(p.depthProgram,"masked"),imported.alphaMode=="MASK"?1:0);p.gl.Uniform1i(p.gl.GetUniformLocation(p.depthProgram,"hasMap"),texture?1:0);p.gl.Uniform1f(p.gl.GetUniformLocation(p.depthProgram,"cutoff"),static_cast<float>(imported.alphaCutoff));p.gl.Uniform1f(p.gl.GetUniformLocation(p.depthProgram,"baseAlpha"),static_cast<float>(imported.baseColor[3]));
   glFrontFace(reflected!=batch.reflected?GL_CW:GL_CCW);if(imported.doubleSided)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(GL_BACK);}p.geometry(batch);
  }
 }
 if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Shadow depth pass produced an OpenGL error");
 p.shadowReady=true;p.shadowHash=hash;++p.shadowPasses;
}
void NativeModels::drawSky(const World& world,const ModelMatrix& view,const ModelMatrix& projection){
 auto& p=*impl_;if(!world.lighting||!p.radianceTexture||!p.skyProgram)return;DrawState state(p.gl);
 p.gl.UseProgram(p.skyProgram);p.bindImage(0,p.radianceTexture);p.gl.Uniform1i(p.gl.GetUniformLocation(p.skyProgram,"radianceMap"),0);p.gl.Uniform1f(p.gl.GetUniformLocation(p.skyProgram,"exposure"),static_cast<float>(world.lighting->exposure));
 matrixUniform(p.gl,p.skyProgram,"inverseProjection",inverseMatrix(projection));matrixUniform(p.gl,p.skyProgram,"inverseView",inverseMatrix(view));
 glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_LIGHTING);glDisable(GL_FOG);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
 glBegin(GL_QUADS);glVertex4f(-1,-1,1,1);glVertex4f(1,-1,1,1);glVertex4f(1,1,1,1);glVertex4f(-1,1,1,1);glEnd();
}
void NativeModels::draw(const RuntimeNode& node,const World& world,const Material& style,const ModelMatrix& modelMatrix,const ModelMatrix& viewMatrix){
 auto& p=*impl_;const auto found=p.assets.find(node.definition.geometry.model);if(found==p.assets.end()||!p.program)throw std::runtime_error("GLB model is not loaded: "+node.definition.geometry.model);
 auto& asset=found->second;const auto& model=*asset.model;DrawState state(p.gl);p.pose(asset,node);
 p.gl.UseProgram(p.program);p.integer("colorMap",0);p.integer("normalMap",1);p.integer("metallicRoughnessMap",2);p.integer("occlusionMap",3);p.integer("emissionMap",4);
 p.integer("fogEnabled",glIsEnabled(GL_FOG)?1:0);p.integer("toon",style.shading=="toon"?1:0);p.scalar("opacity",style.opacity);p.scalar("styleEmission",style.emission);p.vector("tint",color(style.color));
 matrixUniform(p.gl,p.program,"modelWorld",modelMatrix);matrixUniform(p.gl,p.program,"modelNormal",normalMatrix(modelMatrix));matrixUniform(p.gl,p.program,"lightViewProjection",p.lightViewProjection);
 const auto inverseView=inverseMatrix(viewMatrix);p.vector("cameraWorld",{inverseView[12],inverseView[13],inverseView[14]});p.vector("eyeUp",{viewMatrix[4],viewMatrix[5],viewMatrix[6]});
 if(world.lighting){const auto& lighting=*world.lighting;
  p.vector("sunDirection",unit3(lighting.sunDirection));p.vector("sunColor",linearColor(lighting.sunColor));p.scalar("sunIntensity",lighting.sunIntensity);p.vector("skyColor",linearColor(lighting.skyColor));p.vector("groundColor",linearColor(lighting.groundColor));p.scalar("ambientIntensity",lighting.ambientIntensity);p.scalar("environmentIntensity",lighting.environmentIntensity);p.scalar("exposure",lighting.exposure);
  p.integer("shadowMap",5);p.integer("irradianceMap",6);p.integer("specularMap",7);p.bindImage(5,p.shadowTexture);p.bindImage(6,p.irradianceTexture);p.bindImage(7,p.specularTexture);p.integer("hasEnvironment",p.specularTexture?1:0);p.integer("hasShadows",p.shadowReady?1:0);p.scalar("shadowBias",lighting.shadow.bias);p.scalar("shadowNormalBias",lighting.shadow.normalBias);p.scalar("shadowTexel",1./lighting.shadow.resolution);
  p.integer("pointCount",static_cast<int>(lighting.points.size()));
  for(std::size_t i=0;i<lighting.points.size();++i){const auto& point=lighting.points[i];const auto key="pointPositionRange["+std::to_string(i)+"]";p.gl.Uniform4f(p.uniform(key.c_str()),static_cast<float>(point.position[0]),static_cast<float>(point.position[1]),static_cast<float>(point.position[2]),static_cast<float>(point.range));auto rgb=linearColor(point.color);for(auto& value:rgb)value*=point.intensity;p.vector(("pointColorIntensity["+std::to_string(i)+"]").c_str(),rgb);}
 }else p.scalar("shadowNormalBias",0);
 p.gl.ActiveTexture(texture0);glDisable(GL_LIGHTING);glDisable(GL_ALPHA_TEST);glEnable(GL_DEPTH_TEST);glEnable(GL_TEXTURE_2D);glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);glEnableClientState(GL_COLOR_ARRAY);glPolygonMode(GL_FRONT_AND_BACK,style.wireframe?GL_LINE:GL_FILL);
 GLdouble modelview[16]{};glGetDoublev(GL_MODELVIEW_MATRIX,modelview);const bool reflected=determinant(modelview)<0;static const ModelMaterial fallback;
 const auto material=[&](const Impl::Batch& batch)->const ModelMaterial&{return batch.primitive->material>=0?model.materials.at(batch.primitive->material):fallback;};
 const auto isBlend=[&](const Impl::Batch& batch){return style.opacity<1||material(batch).alphaMode=="BLEND";};std::vector<const Impl::Batch*> opaque,blended;for(const auto& batch:asset.batches)(isBlend(batch)?blended:opaque).push_back(&batch);
 const auto depth=[&](const Impl::Batch* batch){const auto& v=batch->center;return modelview[2]*v[0]+modelview[6]*v[1]+modelview[10]*v[2]+modelview[14];};std::stable_sort(blended.begin(),blended.end(),[&](const auto* a,const auto* b){return depth(a)<depth(b);});
 const auto drawBatch=[&](const Impl::Batch& batch,bool blend){const auto& imported=material(batch);
  p.integer("alphaMode",imported.alphaMode=="MASK"?1:imported.alphaMode=="BLEND"?2:0);p.scalar("alphaCutoff",imported.alphaCutoff);p.scalar("roughness",style.authoredRoughness?style.roughness:imported.roughness);p.scalar("metalness",style.authoredMetalness?style.metalness:imported.metallic);
  p.gl.Uniform4f(p.uniform("baseColor"),static_cast<float>(imported.baseColor[0]),static_cast<float>(imported.baseColor[1]),static_cast<float>(imported.baseColor[2]),static_cast<float>(imported.baseColor[3]));p.vector("emissive",imported.emissive);p.integer("doubleSided",imported.doubleSided?1:0);p.integer("unlit",imported.unlit?1:0);p.scalar("normalScale",imported.normalScale);p.scalar("occlusionStrength",imported.occlusionStrength);
  const std::array<int,5> images={imported.baseColorImage,imported.normalImage,imported.metallicRoughnessImage,imported.occlusionImage,imported.emissiveImage};const char* flags[]={"hasColorMap","hasNormalMap","hasMetallicRoughnessMap","hasOcclusionMap","hasEmissionMap"};for(unsigned unit=0;unit<images.size();++unit){const auto image=images[unit];const auto texture=image>=0?asset.images.at(image):0;p.bindImage(unit,texture);p.integer(flags[unit],texture?1:0);}p.gl.ActiveTexture(texture0);
  glFrontFace(reflected!=batch.reflected?GL_CW:GL_CCW);if(blend){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}else{glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
  if(style.shading=="toon"&&style.outline>0&&!style.wireframe&&!blend&&!imported.doubleSided){p.integer("outlinePass",1);p.scalar("outlineWidth",style.outline);glEnable(GL_CULL_FACE);glCullFace(GL_FRONT);p.geometry(batch);}
  p.integer("outlinePass",0);p.scalar("outlineWidth",0);if(imported.doubleSided)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(GL_BACK);}p.geometry(batch);
 };
 for(const auto* batch:opaque)drawBatch(*batch,false);for(const auto* batch:blended)drawBatch(*batch,true);
}
ModelGraphicsStats NativeModels::stats() const noexcept{return {impl_->gpuBytes,impl_->mapCount,impl_->shadowPasses,impl_->shadowTexture?1u:0u,impl_->radianceTexture!=0};}

std::size_t NativeModels::count() const noexcept{return impl_->assets.size();}
std::size_t NativeModels::triangles() const noexcept{return impl_->triangleCount;}
} // namespace linkworld
