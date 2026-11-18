#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#include "link/model_renderer.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace linkworld;
namespace {
constexpr GLenum framebuffer=0x8D40,drawFramebuffer=0x8CA9,readFramebuffer=0x8CA8;
constexpr GLenum drawFramebufferBinding=0x8CA6,readFramebufferBinding=0x8CAA,colorAttachment0=0x8CE0,framebufferComplete=0x8CD5;
constexpr GLenum texture0=0x84C0,activeTexture=0x84E0,clientActiveTexture=0x84E1,currentProgram=0x8B8D;
constexpr GLenum arrayBuffer=0x8892,elementBuffer=0x8893,arrayBufferBinding=0x8894,elementBufferBinding=0x8895,staticDraw=0x88E4;
constexpr GLenum vertexShader=0x8B31,fragmentShader=0x8B30,compileStatus=0x8B81,linkStatus=0x8B82;
void require(bool okay,const std::string& message){if(!okay)throw std::runtime_error(message);}
void noError(const std::string& phase){const auto error=glGetError();require(error==GL_NO_ERROR,phase+" produced GL error "+std::to_string(error));}
ModelMatrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct HiddenGl {
  HWND window=nullptr;HDC dc=nullptr;HGLRC context=nullptr;ATOM windowClass=0;
  HiddenGl(){try{
    WNDCLASSW type{};type.style=CS_OWNDC;type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"LINK.ModelRendererTests.Hidden";
    windowClass=RegisterClassW(&type);require(windowClass!=0,"Register hidden renderer-test window");
    window=CreateWindowExW(0,type.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,type.hInstance,nullptr);require(window!=nullptr,"Create hidden renderer-test window");
    dc=GetDC(window);require(dc!=nullptr,"Acquire renderer-test device context");
    PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;format.cDepthBits=24;
    const auto selected=ChoosePixelFormat(dc,&format);require(selected&&SetPixelFormat(dc,selected,&format),"Set renderer-test pixel format");
    context=wglCreateContext(dc);require(context&&wglMakeCurrent(dc,context),"Create renderer-test compatibility context");
  }catch(...){clear();throw;}}
  void clear() noexcept{
    if(context){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);context=nullptr;}
    if(dc&&window){ReleaseDC(window,dc);dc=nullptr;}if(window){DestroyWindow(window);window=nullptr;}
    if(windowClass){UnregisterClassW(L"LINK.ModelRendererTests.Hidden",GetModuleHandleW(nullptr));windowClass=0;}
  }
  ~HiddenGl(){clear();}
};
struct Gl {
#define PROC(name,result,args) using name##Fn=result(APIENTRY*)args;name##Fn name=nullptr
  PROC(GenFramebuffers,void,(GLsizei,GLuint*));PROC(DeleteFramebuffers,void,(GLsizei,const GLuint*));PROC(BindFramebuffer,void,(GLenum,GLuint));
  PROC(FramebufferTexture2D,void,(GLenum,GLenum,GLenum,GLuint,GLint));PROC(CheckFramebufferStatus,GLenum,(GLenum));
  PROC(GenBuffers,void,(GLsizei,GLuint*));PROC(DeleteBuffers,void,(GLsizei,const GLuint*));PROC(BindBuffer,void,(GLenum,GLuint));PROC(BufferData,void,(GLenum,std::ptrdiff_t,const void*,GLenum));
  PROC(ActiveTexture,void,(GLenum));PROC(ClientActiveTexture,void,(GLenum));
  PROC(CreateShader,GLuint,(GLenum));PROC(ShaderSource,void,(GLuint,GLsizei,const char* const*,const GLint*));PROC(CompileShader,void,(GLuint));PROC(GetShaderiv,void,(GLuint,GLenum,GLint*));PROC(GetShaderInfoLog,void,(GLuint,GLsizei,GLsizei*,char*));PROC(DeleteShader,void,(GLuint));
  PROC(CreateProgram,GLuint,());PROC(AttachShader,void,(GLuint,GLuint));PROC(LinkProgram,void,(GLuint));PROC(GetProgramiv,void,(GLuint,GLenum,GLint*));PROC(GetProgramInfoLog,void,(GLuint,GLsizei,GLsizei*,char*));PROC(UseProgram,void,(GLuint));PROC(DeleteProgram,void,(GLuint));
#undef PROC
  template<class T>static T load(const char* name){const auto p=wglGetProcAddress(name);require(p&&p!=reinterpret_cast<PROC>(1)&&p!=reinterpret_cast<PROC>(2)&&p!=reinterpret_cast<PROC>(3)&&p!=reinterpret_cast<PROC>(-1),std::string("GPU driver is missing ")+name);return reinterpret_cast<T>(p);}
  Gl(){
#define LOAD(name) name=load<name##Fn>("gl" #name)
    LOAD(GenFramebuffers);LOAD(DeleteFramebuffers);LOAD(BindFramebuffer);LOAD(FramebufferTexture2D);LOAD(CheckFramebufferStatus);
    LOAD(GenBuffers);LOAD(DeleteBuffers);LOAD(BindBuffer);LOAD(BufferData);LOAD(ActiveTexture);LOAD(ClientActiveTexture);
    LOAD(CreateShader);LOAD(ShaderSource);LOAD(CompileShader);LOAD(GetShaderiv);LOAD(GetShaderInfoLog);LOAD(DeleteShader);LOAD(CreateProgram);LOAD(AttachShader);LOAD(LinkProgram);LOAD(GetProgramiv);LOAD(GetProgramInfoLog);LOAD(UseProgram);LOAD(DeleteProgram);
#undef LOAD
  }
  GLuint shader(GLenum kind,const char* source){const auto id=CreateShader(kind);require(id!=0,"Allocate test shader");ShaderSource(id,1,&source,nullptr);CompileShader(id);GLint okay=0;GetShaderiv(id,compileStatus,&okay);if(!okay){std::array<char,2048> log{};GetShaderInfoLog(id,static_cast<GLsizei>(log.size()),nullptr,log.data());DeleteShader(id);throw std::runtime_error(std::string("Test shader compilation: ")+log.data());}return id;}
};
struct ExternalTargets {
  Gl& gl;std::array<GLuint,2> fbos{},targets{},buffers{};std::array<GLuint,8> units{};GLuint program=0,vertex=0,fragment=0;
  explicit ExternalTargets(Gl& api):gl(api){try{
    gl.GenFramebuffers(2,fbos.data());glGenTextures(2,targets.data());glGenTextures(8,units.data());gl.GenBuffers(2,buffers.data());
    require(fbos[0]&&fbos[1]&&fbos[0]!=fbos[1],"Allocate separate external draw and read FBOs");
    for(std::size_t i=0;i<fbos.size();++i){gl.BindFramebuffer(framebuffer,fbos[i]);glBindTexture(GL_TEXTURE_2D,targets[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);gl.FramebufferTexture2D(framebuffer,colorAttachment0,GL_TEXTURE_2D,targets[i],0);glDrawBuffer(colorAttachment0);glReadBuffer(colorAttachment0);require(gl.CheckFramebufferStatus(framebuffer)==framebufferComplete,"External color FBO complete");}
    const std::array<GLfloat,12> vertices={-.5f,-.5f,0,.5f,-.5f,0,0,.5f,0,0,0,0};const std::array<GLuint,3> indices={0,1,2};
    gl.BindBuffer(arrayBuffer,buffers[0]);gl.BufferData(arrayBuffer,sizeof(vertices),vertices.data(),staticDraw);gl.BindBuffer(elementBuffer,buffers[1]);gl.BufferData(elementBuffer,sizeof(indices),indices.data(),staticDraw);
    vertex=gl.shader(vertexShader,"#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}");fragment=gl.shader(fragmentShader,"#version 120\nvoid main(){gl_FragColor=vec4(0.2,0.4,0.6,1.0);}");program=gl.CreateProgram();require(program!=0,"Allocate external GL program");gl.AttachShader(program,vertex);gl.AttachShader(program,fragment);gl.LinkProgram(program);GLint okay=0;gl.GetProgramiv(program,linkStatus,&okay);if(!okay){std::array<char,2048> log{};gl.GetProgramInfoLog(program,static_cast<GLsizei>(log.size()),nullptr,log.data());throw std::runtime_error(std::string("External GL program link: ")+log.data());}
    const std::array<GLubyte,4> pixel={103,67,29,255};
    for(unsigned unit=0;unit<units.size();++unit){gl.ActiveTexture(texture0+unit);glBindTexture(GL_TEXTURE_2D,units[unit]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);if(unit%2)glEnable(GL_TEXTURE_2D);else glDisable(GL_TEXTURE_2D);glMatrixMode(GL_TEXTURE);glLoadIdentity();glTranslated(.01*unit,.02*unit,0);}
    gl.BindFramebuffer(drawFramebuffer,fbos[0]);gl.BindFramebuffer(readFramebuffer,fbos[1]);glDrawBuffer(colorAttachment0);glReadBuffer(colorAttachment0);
    glViewport(3,5,24,22);glScissor(2,4,19,20);glEnable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-3,3,-2,2,.1,30);glTranslated(.125,.25,-.5);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslated(.2,-.1,-4);glRotated(17,0,1,0);
    gl.ActiveTexture(texture0+3);gl.ClientActiveTexture(texture0+2);glMatrixMode(GL_TEXTURE);
    gl.BindBuffer(arrayBuffer,buffers[0]);gl.BindBuffer(elementBuffer,buffers[1]);glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(3,GL_FLOAT,0,nullptr);glEnableClientState(GL_TEXTURE_COORD_ARRAY);glTexCoordPointer(2,GL_FLOAT,0,nullptr);
    gl.UseProgram(program);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_GREATER);glDepthMask(GL_FALSE);glClearDepth(.375);glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ZERO);glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glFrontFace(GL_CW);
    noError("External FBO/program/state setup");
  }catch(...){clear();throw;}}
  void clear() noexcept{
    gl.UseProgram(0);gl.BindFramebuffer(framebuffer,0);gl.BindBuffer(arrayBuffer,0);gl.BindBuffer(elementBuffer,0);
    for(unsigned unit=0;unit<units.size();++unit){gl.ActiveTexture(texture0+unit);glBindTexture(GL_TEXTURE_2D,0);}gl.ActiveTexture(texture0);gl.ClientActiveTexture(texture0);
    glDeleteTextures(2,targets.data());glDeleteTextures(8,units.data());gl.DeleteFramebuffers(2,fbos.data());gl.DeleteBuffers(2,buffers.data());
    if(program)gl.DeleteProgram(program);if(vertex)gl.DeleteShader(vertex);if(fragment)gl.DeleteShader(fragment);program=vertex=fragment=0;
  }
  ~ExternalTargets(){clear();}
};
struct Snapshot {
  GLint drawFbo=0,readFbo=0,drawBuffer=0,readBuffer=0,mode=0,program=0,active=0,client=0,array=0,elements=0,depthFunction=0,blendSource=0,blendDestination=0,frontFace=0;
  std::array<GLint,4> viewport{},scissor{};std::array<GLint,2> polygon{};std::array<GLboolean,4> colorMask{};
  GLboolean depthMask=0,depthTest=0,scissorTest=0,blend=0,vertexArray=0,texcoordArray=0;GLdouble clearDepth=0;
  std::array<GLdouble,16> projection{},modelview{};std::array<GLint,8> textures{};std::array<GLboolean,8> textureEnabled{};std::array<std::array<GLdouble,16>,8> textureMatrices{};
  explicit Snapshot(Gl& gl){
    glGetIntegerv(drawFramebufferBinding,&drawFbo);glGetIntegerv(readFramebufferBinding,&readFbo);glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_MATRIX_MODE,&mode);glGetIntegerv(currentProgram,&program);glGetIntegerv(activeTexture,&active);glGetIntegerv(clientActiveTexture,&client);glGetIntegerv(arrayBufferBinding,&array);glGetIntegerv(elementBufferBinding,&elements);
    glGetIntegerv(GL_VIEWPORT,viewport.data());glGetIntegerv(GL_SCISSOR_BOX,scissor.data());glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask.data());glGetDoublev(GL_DEPTH_CLEAR_VALUE,&clearDepth);depthTest=glIsEnabled(GL_DEPTH_TEST);scissorTest=glIsEnabled(GL_SCISSOR_TEST);blend=glIsEnabled(GL_BLEND);vertexArray=glIsEnabled(GL_VERTEX_ARRAY);texcoordArray=glIsEnabled(GL_TEXTURE_COORD_ARRAY);glGetIntegerv(GL_BLEND_SRC,&blendSource);glGetIntegerv(GL_BLEND_DST,&blendDestination);glGetIntegerv(GL_FRONT_FACE,&frontFace);glGetIntegerv(GL_POLYGON_MODE,polygon.data());
    glGetDoublev(GL_PROJECTION_MATRIX,projection.data());glGetDoublev(GL_MODELVIEW_MATRIX,modelview.data());
    for(unsigned unit=0;unit<textures.size();++unit){gl.ActiveTexture(texture0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[unit]);textureEnabled[unit]=glIsEnabled(GL_TEXTURE_2D);glGetDoublev(GL_TEXTURE_MATRIX,textureMatrices[unit].data());}gl.ActiveTexture(static_cast<GLenum>(active));noError("Capture GL state");
  }
  void matches(Gl& gl,const std::string& phase) const{
    const Snapshot after(gl);
#define SAME(field) require(field==after.field,phase+" changed " #field)
    SAME(drawFbo);SAME(readFbo);SAME(drawBuffer);SAME(readBuffer);SAME(viewport);SAME(scissor);SAME(mode);SAME(program);SAME(active);SAME(client);SAME(array);SAME(elements);SAME(depthFunction);SAME(depthMask);SAME(colorMask);SAME(clearDepth);SAME(depthTest);SAME(scissorTest);SAME(blend);SAME(blendSource);SAME(blendDestination);SAME(frontFace);SAME(polygon);SAME(vertexArray);SAME(texcoordArray);SAME(projection);SAME(modelview);SAME(textures);SAME(textureEnabled);SAME(textureMatrices);
#undef SAME
    noError(phase);
  }
};
std::shared_ptr<World> fixture(){
  auto world=std::make_shared<World>();world->extensions={"models-v1","lighting-v1"};world->lighting=LightingSettings{};world->lighting->shadow.resolution=512;world->lighting->shadow.center={0,0,0};world->lighting->shadow.extent=4;
  auto asset=std::make_shared<ModelAsset>();ModelMaterial material;material.baseColor={.6,.45,.3,1};material.metallic=0;material.roughness=.8;asset->materials.push_back(material);
  ModelPrimitive triangle;triangle.material=0;for(const Vec3 position:std::array<Vec3,3>{Vec3{-.5,0,0},Vec3{.5,0,0},Vec3{0,1,0}}){ModelVertex vertex;vertex.position=position;vertex.normal={0,0,1};triangle.vertices.push_back(vertex);}triangle.indices={0,1,2};
  ModelPrimitive floor;floor.material=0;for(const Vec3 position:std::array<Vec3,4>{Vec3{-2,-.1,-2},Vec3{2,-.1,-2},Vec3{2,-.1,2},Vec3{-2,-.1,2}}){ModelVertex vertex;vertex.position=position;vertex.normal={0,1,0};floor.vertices.push_back(vertex);}floor.indices={0,2,1,0,3,2};
  ModelMesh mesh;mesh.primitives={triangle,floor};asset->meshes.push_back(mesh);ModelNode node;node.mesh=0;asset->nodes.push_back(node);asset->sceneRoots={0};asset->sceneActive={true};asset->vertices=asset->drawVertices=7;asset->triangles=asset->drawTriangles=3;asset->minimum={-2,-.1,-2};asset->maximum={2,1,2};world->models["memory.glb"]=asset;
  NodeDefinition object;object.id="root";object.kind=Kind::Object;NodeDefinition component;component.id="group";component.kind=Kind::Component;component.parent=0;NodeDefinition model;model.id="model";model.kind=Kind::Element;model.parent=1;model.geometry.type=Geometry::Type::Model;model.geometry.model="memory.glb";world->nodes={object,component,model};return world;
}
void testGraphicsState(){
  HiddenGl context;Gl gl;ExternalTargets external(gl);const Snapshot before(gl);auto world=fixture();Runtime runtime(world);runtime.start();NativeModels models;
  models.load(*world);before.matches(gl,"Model load");const auto loaded=models.stats();require(models.count()==1&&models.triangles()==3&&loaded.bytes>0&&loaded.shadowMaps==1&&!loaded.hdrEnvironment,"Model load allocates its mesh and one shadow map");
  models.prepareScene(runtime);before.matches(gl,"First shadow pass");require(models.stats().shadowPasses==1,"First prepareScene renders one shadow pass");
  models.prepareScene(runtime);before.matches(gl,"Cached shadow pass");require(models.stats().shadowPasses==1,"Static prepareScene reuses the shadow map");
  runtime.find("model").transform.position[0]=.25;models.prepareScene(runtime);before.matches(gl,"Moved-model shadow pass");require(models.stats().shadowPasses==2,"Moving an Element invalidates its shadow map");
  auto model=identity();model[12]=.25;models.draw(runtime.find("model"),*world,runtime.material(2),model,identity());before.matches(gl,"Model draw");
  models.drawSky(*world,identity(),identity());before.matches(gl,"Sky draw without HDR");
  models.clear();before.matches(gl,"Model clear");const auto cleared=models.stats();require(models.count()==0&&models.triangles()==0&&cleared.bytes==0&&cleared.materialMaps==0&&cleared.shadowMaps==0&&cleared.shadowPasses==0&&!cleared.hdrEnvironment,"clear releases model GPU resources and resets diagnostics");
  // Reusing the same external targets catches resources left in another context
  // or stale bindings retained after a full world graphics lifecycle.
  models.load(*world);models.prepareScene(runtime);before.matches(gl,"Model reload and shadow pass");require(models.stats().shadowPasses==1,"Reload begins with a fresh shadow cache");models.clear();before.matches(gl,"Reload cleanup");
  std::cout<<"GPU: "<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<" / "<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<'\n';
}
}
int main(){try{testGraphicsState();std::cout<<"Native model framebuffer, GL state, shadow cache and cleanup PASS\n";return 0;}catch(const std::exception& error){std::cerr<<"Model renderer GPU regression failed: "<<error.what()<<'\n';return 1;}}
