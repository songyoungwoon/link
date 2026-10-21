#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <GL/gl.h>
#include "link/desktop.hpp"
#include "link/navigation.hpp"
#include "link/walk.hpp"
#include "link/textures.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace linkworld {
namespace {
constexpr double pi=3.14159265358979323846;
using Clock=std::chrono::steady_clock;
std::wstring wide(const std::string& value) {
  if(value.empty()) return {};
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
  if(!n) return L"[invalid UTF-8]";
  std::wstring result(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n); return result;
}
std::string utf8(const std::wstring& value) {
  if(value.empty()) return {};
  int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  std::string result(n,'\0'); WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr); return result;
}
std::wstring windowText(HWND window) { std::wstring result(GetWindowTextLengthW(window)+1,L'\0'); result.resize(GetWindowTextW(window,result.data(),static_cast<int>(result.size()))); return result; }
void setText(HWND window,const std::wstring& value) { if(windowText(window)!=value) SetWindowTextW(window,value.c_str()); }
Vec3 add(Vec3 a,Vec3 b) { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
Vec3 sub(Vec3 a,Vec3 b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Vec3 mul(Vec3 a,double b) { return {a[0]*b,a[1]*b,a[2]*b}; }
double dot(Vec3 a,Vec3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 cross(Vec3 a,Vec3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
Vec3 unit(Vec3 a) { double n=std::sqrt(dot(a,a)); return n>1e-12?mul(a,1/n):Vec3{0,0,0}; }
struct Matrix {
  std::array<double,16> m{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  Vec3 point(Vec3 v) const { return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12],m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]}; }
  Vec3 direction(Vec3 v) const { return sub(point(v),point({0,0,0})); }
};
Matrix product(const Matrix& a,const Matrix& b) {
  Matrix r; r.m.fill(0);
  for(int c=0;c<4;++c) for(int row=0;row<4;++row) for(int k=0;k<4;++k) r.m[c*4+row]+=a.m[k*4+row]*b.m[c*4+k];
  return r;
}
Matrix localMatrix(const Transform& t) {
  Matrix r; r.m[12]=t.position[0];r.m[13]=t.position[1];r.m[14]=t.position[2];
  for(int axis=0;axis<3;++axis) {
    Matrix a;double c=std::cos(t.rotation[axis]*pi/180),s=std::sin(t.rotation[axis]*pi/180);
    if(axis==0) {a.m[5]=c;a.m[6]=s;a.m[9]=-s;a.m[10]=c;}
    if(axis==1) {a.m[0]=c;a.m[2]=-s;a.m[8]=s;a.m[10]=c;}
    if(axis==2) {a.m[0]=c;a.m[1]=s;a.m[4]=-s;a.m[5]=c;}
    r=product(r,a);
  }
  Matrix scale;scale.m[0]=t.scale[0];scale.m[5]=t.scale[1];scale.m[10]=t.scale[2];return product(r,scale);
}
bool inverse(const Matrix& a,Matrix& out) {
  const auto& m=a.m;
  double det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
  if(std::abs(det)<1e-12) return false;
  out.m[0]=(m[5]*m[10]-m[9]*m[6])/det;out.m[4]=(m[8]*m[6]-m[4]*m[10])/det;out.m[8]=(m[4]*m[9]-m[8]*m[5])/det;
  out.m[1]=(m[9]*m[2]-m[1]*m[10])/det;out.m[5]=(m[0]*m[10]-m[8]*m[2])/det;out.m[9]=(m[8]*m[1]-m[0]*m[9])/det;
  out.m[2]=(m[1]*m[6]-m[5]*m[2])/det;out.m[6]=(m[4]*m[2]-m[0]*m[6])/det;out.m[10]=(m[0]*m[5]-m[4]*m[1])/det;
  auto p=mul(out.direction({m[12],m[13],m[14]}),-1);out.m[12]=p[0];out.m[13]=p[1];out.m[14]=p[2];return true;
}
struct SceneMatrices { std::vector<Matrix> world;std::vector<bool> visible; };
SceneMatrices sceneMatrices(const Runtime& runtime) {
  SceneMatrices result;const auto& nodes=runtime.nodes();result.world.resize(nodes.size());result.visible.resize(nodes.size());
  // Semantic validation produces a parent-before-child flat tree.
  for(std::size_t i=0;i<nodes.size();++i) {
    auto p=nodes[i].definition.parent;auto local=localMatrix(nodes[i].transform);
    result.world[i]=p==noParent?local:product(result.world.at(p),local);
    result.visible[i]=nodes[i].transform.visible&&(p==noParent||result.visible.at(p));
  }
  return result;
}
struct Camera {
  Vec3 target{0,1,0};double yaw=.65,pitch=.48,distance=10;
  Vec3 eye() const { return add(target,{distance*std::cos(pitch)*std::sin(yaw),distance*std::sin(pitch),distance*std::cos(pitch)*std::cos(yaw)}); }
  void jump(Vec3 position,Vec3 aim) { target=aim;auto d=sub(position,target);distance=std::clamp(std::sqrt(dot(d,d)),.3,220.);yaw=std::atan2(d[0],d[2]);pitch=std::clamp(std::asin(std::clamp(d[1]/distance,-1.,1.)),-1.48,1.48); }
  void reset(const World& world) {jump(world.eye,world.target);}
  Vec3 forward() const { return unit(sub(target,eye())); }
  Vec3 right() const { return unit(cross(forward(),{0,1,0})); }
  Vec3 up() const { return cross(right(),forward()); }
  Matrix view() const { auto s=right(),u=up(),f=forward(),e=eye();Matrix m;m.m={s[0],u[0],-f[0],0,s[1],u[1],-f[1],0,s[2],u[2],-f[2],0,-dot(s,e),-dot(u,e),dot(f,e),1};return m; }
};
double intersect(const Geometry& g,Vec3 o,Vec3 d) {
  constexpr double inf=std::numeric_limits<double>::infinity();
  if(g.type==Geometry::Type::Frame){
    double best=inf;Geometry box;box.type=Geometry::Type::Box;
    box.size={g.size[0],g.border,g.depth};for(double sign:{-1.,1.})best=std::min(best,intersect(box,sub(o,{0,sign*(g.size[1]-g.border)/2,0}),d));
    box.size={g.border,g.size[1]-2*g.border,g.depth};for(double sign:{-1.,1.})best=std::min(best,intersect(box,sub(o,{sign*(g.size[0]-g.border)/2,0,0}),d));return best;
  }
  if(g.type==Geometry::Type::Cylinder){
    double best=inf,half=g.height/2,k=(g.radiusTop-g.radius)/g.height,c=(g.radius+g.radiusTop)/2;
    double a=d[0]*d[0]+d[2]*d[2]-k*k*d[1]*d[1],b=2*(o[0]*d[0]+o[2]*d[2]-(k*o[1]+c)*k*d[1]),cc=o[0]*o[0]+o[2]*o[2]-(k*o[1]+c)*(k*o[1]+c);
    auto side=[&](double t){double y=o[1]+t*d[1];if(t>1e-6&&y>=-half&&y<=half)best=std::min(best,t);};
    if(std::abs(a)<1e-12){if(std::abs(b)>1e-12)side(-cc/b);}else{double disc=b*b-4*a*cc;if(disc>=0){side((-b-std::sqrt(disc))/(2*a));side((-b+std::sqrt(disc))/(2*a));}}
    if(std::abs(d[1])>1e-12)for(int end=0;end<2;++end){double y=end?half:-half,r=end?g.radiusTop:g.radius,t=(y-o[1])/d[1];auto p=add(o,mul(d,t));if(t>1e-6&&p[0]*p[0]+p[2]*p[2]<=r*r)best=std::min(best,t);}return best;
  }
  if(g.type==Geometry::Type::Sphere) {
    double a=dot(d,d),b=2*dot(o,d),c=dot(o,o)-g.radius*g.radius,disc=b*b-4*a*c;
    if(a<1e-20||disc<0)return inf;double n=(-b-std::sqrt(disc))/(2*a),f=(-b+std::sqrt(disc))/(2*a);return n>1e-6?n:f>1e-6?f:inf;
  }
  if(g.type==Geometry::Type::Plane) {
    if(std::abs(d[2])<1e-12)return inf;double t=-o[2]/d[2];auto p=add(o,mul(d,t));
    return t>1e-6&&std::abs(p[0])<=g.size[0]/2&&std::abs(p[1])<=g.size[1]/2?t:inf;
  }
  double nearT=0,farT=inf;
  for(int a=0;a<3;++a) {
    double h=g.size[a]/2;
    if(std::abs(d[a])<1e-12){if(o[a]<-h||o[a]>h)return inf;continue;}
    double aT=(-h-o[a])/d[a],bT=(h-o[a])/d[a];if(aT>bT)std::swap(aT,bT);nearT=std::max(nearT,aT);farT=std::min(farT,bT);if(nearT>farT)return inf;
  }
  return nearT>1e-6?nearT:farT>1e-6?farT:inf;
}
std::size_t raycast(const Runtime& runtime,const Camera& camera,int x,int y,int width,int height) {
  if(width<=0||height<=0)return noParent;
  double tangent=std::tan(45*pi/360),aspect=double(width)/height;
  Vec3 direction=unit(add(camera.forward(),add(mul(camera.right(),((2.*x/width)-1)*aspect*tangent),mul(camera.up(),(1-2.*y/height)*tangent))));
  auto matrices=sceneMatrices(runtime);double closest=std::numeric_limits<double>::infinity();std::size_t hit=noParent;
  for(std::size_t i=0;i<runtime.nodes().size();++i) {
    const auto& n=runtime.nodes()[i];if(n.definition.kind!=Kind::Element||!matrices.visible[i]||runtime.material(i).opacity<=0)continue;
    Matrix inv;if(!inverse(matrices.world[i],inv))continue;
    double t=intersect(n.definition.geometry,inv.point(camera.eye()),inv.direction(direction));if(t<closest){closest=t;hit=i;}
  }
  return hit;
}
void geometry(const Geometry& g) {
  if(g.type==Geometry::Type::Frame){Geometry box;box.type=Geometry::Type::Box;box.size={g.size[0],g.border,g.depth};for(double sign:{-1.,1.}){glPushMatrix();glTranslated(0,sign*(g.size[1]-g.border)/2,0);geometry(box);glPopMatrix();}box.size={g.border,g.size[1]-2*g.border,g.depth};for(double sign:{-1.,1.}){glPushMatrix();glTranslated(sign*(g.size[0]-g.border)/2,0,0);geometry(box);glPopMatrix();}return;}
  if(g.type==Geometry::Type::Cylinder){constexpr int slices=40;double half=g.height/2;glBegin(GL_QUAD_STRIP);for(int i=0;i<=slices;++i){double angle=2*pi*i/slices,c=std::cos(angle),s=std::sin(angle);auto n=unit({c,(g.radius-g.radiusTop)/g.height,s});glNormal3dv(n.data());glVertex3d(g.radius*c,-half,g.radius*s);glVertex3d(g.radiusTop*c,half,g.radiusTop*s);}glEnd();for(int end=0;end<2;++end){double r=end?g.radiusTop:g.radius,y=end?half:-half;if(r<=0)continue;glBegin(GL_TRIANGLE_FAN);glNormal3d(0,end?1:-1,0);glVertex3d(0,y,0);for(int i=0;i<=slices;++i){double angle=(end?-1:1)*2*pi*i/slices;glVertex3d(r*std::cos(angle),y,r*std::sin(angle));}glEnd();}return;}
  if(g.type==Geometry::Type::Sphere) {
    constexpr int slices=28,stacks=18;
    for(int j=0;j<stacks;++j){double a=-pi/2+pi*j/stacks,b=-pi/2+pi*(j+1)/stacks;glBegin(GL_QUAD_STRIP);
      for(int i=0;i<=slices;++i){double u=2*pi*i/slices;for(double v:{b,a}){Vec3 n{std::cos(v)*std::cos(u),std::sin(v),std::cos(v)*std::sin(u)};glNormal3dv(n.data());auto p=mul(n,g.radius);glVertex3dv(p.data());}}glEnd();}return;
  }
  double x=g.size[0]/2,y=g.size[1]/2,z=g.size[2]/2;
  if(g.type==Geometry::Type::Plane){glBegin(GL_QUADS);glNormal3d(0,0,1);glVertex3d(-x,-y,0);glVertex3d(x,-y,0);glVertex3d(x,y,0);glVertex3d(-x,y,0);glEnd();return;}
  glBegin(GL_QUADS);
  glNormal3d(0,0,1);glVertex3d(-x,-y,z);glVertex3d(x,-y,z);glVertex3d(x,y,z);glVertex3d(-x,y,z);
  glNormal3d(0,0,-1);glVertex3d(x,-y,-z);glVertex3d(-x,-y,-z);glVertex3d(-x,y,-z);glVertex3d(x,y,-z);
  glNormal3d(1,0,0);glVertex3d(x,-y,z);glVertex3d(x,-y,-z);glVertex3d(x,y,-z);glVertex3d(x,y,z);
  glNormal3d(-1,0,0);glVertex3d(-x,-y,-z);glVertex3d(-x,-y,z);glVertex3d(-x,y,z);glVertex3d(-x,y,-z);
  glNormal3d(0,1,0);glVertex3d(-x,y,z);glVertex3d(x,y,z);glVertex3d(x,y,-z);glVertex3d(-x,y,-z);
  glNormal3d(0,-1,0);glVertex3d(-x,-y,-z);glVertex3d(x,-y,-z);glVertex3d(x,-y,z);glVertex3d(-x,-y,z);glEnd();
}
void texturedQuad(const Texture& texture,double width,double height,double opacity=1){
  glDisable(GL_LIGHTING);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,texture.id);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glColor4d(1,1,1,opacity);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glBegin(GL_QUADS);glTexCoord2d(texture.u0,texture.v1);glVertex3d(-width/2,-height/2,0);glTexCoord2d(texture.u1,texture.v1);glVertex3d(width/2,-height/2,0);glTexCoord2d(texture.u1,texture.v0);glVertex3d(width/2,height/2,0);glTexCoord2d(texture.u0,texture.v0);glVertex3d(-width/2,height/2,0);glEnd();glBindTexture(GL_TEXTURE_2D,0);glDisable(GL_TEXTURE_2D);glEnable(GL_LIGHTING);
}
void applyMaterial(const Material& m) {
  unsigned rgb=0xc4d9d0;try {rgb=static_cast<unsigned>(std::stoul(m.color.substr(1),nullptr,16));}catch(...){}
  GLfloat color[]={float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,float(m.opacity)};
  glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE,color);
  const double intensity=m.emission/(1+m.emission);GLfloat emission[]={float(color[0]*intensity),float(color[1]*intensity),float(color[2]*intensity),1};glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,emission);
  GLfloat spec[]={float(.08+m.metalness*.7),float(.08+m.metalness*.7),float(.08+m.metalness*.7),1};
  glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,spec);glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,float(4+(1-m.roughness)*100));
  glPolygonMode(GL_FRONT_AND_BACK,m.wireframe?GL_LINE:GL_FILL);
}
enum ControlId {Address=100,Open,Reload,Home,Stop,Cancel,Gallery,RoomA,RoomB,NodeList,Inspector,Logs,Status,PreviousPhoto,NextPhoto,ClosePhoto,PhotoTitle,Menu,DeveloperTools,DeveloperClose,ErrorDetails,FocusAddress};
class Desktop {
public:
  explicit Desktop(std::filesystem::path root):navigator_(std::move(root),[this](bool error,const std::string& message){log(error,message);}){}
  ~Desktop(){navigator_.stop();destroyGl();if(font_)DeleteObject(font_);if(mono_)DeleteObject(mono_);if(addressFont_)DeleteObject(addressFont_);if(background_)DeleteObject(background_);if(paper_)DeleteObject(paper_);if(photoBrush_)DeleteObject(photoBrush_);}
  int run(int show);
  std::string initialAddress;std::filesystem::path capturePath,smokePath,gallerySmokePath,shellSmokePath;
private:
  Navigator navigator_;NativeTextures textures_;HWND window_=nullptr,viewport_=nullptr,address_=nullptr,status_=nullptr,nodes_=nullptr,inspector_=nullptr,logs_=nullptr;
  std::map<int,HWND> controls_;HDC glDc_=nullptr;HGLRC glContext_=nullptr;HFONT font_=nullptr,mono_=nullptr;HBRUSH background_=nullptr,paper_=nullptr;
  HFONT addressFont_=nullptr;HBRUSH photoBrush_=nullptr;HWND tooltip_=nullptr;RECT addressPill_{},viewRect_{};bool toolsOpen_=false,shellActive_=false,shellLoading_=false,shellError_=false,addressComposing_=false;int hovered_=0;int homeAnchor_=0,contentWidth_=0;
  int shellStage_=0;std::size_t shellCreated_=0;bool shellHome_=false,shellFocus_=false,shellTools_=false,shellResize_=false,shellDpi_=false,shellReload_=false,shellCancel_=false,shellFailure_=false;std::string shellFailureMessage_;
  UINT dpi_=96;int width_=1280,height_=820,viewWidth_=0,viewHeight_=0;Camera camera_;bool minimized_=false,dragged_=false;int dragButton_=0;POINT lastMouse_{},downMouse_{};
  std::array<bool,256> walkKeys_{};
  std::vector<std::string> nodeIds_;std::vector<std::wstring> logLines_;std::string selected_,lastStructure_,lastError_,displayAddress_,lastState_;std::wstring mode_=L"시작 화면";
  Clock::time_point previous_=Clock::now(),lastInspect_=Clock::now(),started_=Clock::now();std::size_t createdSeen_=0;std::size_t frames_=0,clicks_=0;std::string glDescription_;
  int smokeStage_=0,switches_=0,exitCode_=0,smokeDoorCycles_=0;bool smokeClicked_=false,smokeDrag_=false,smokePreserved_=false,smokeStones_=false,smokeHome_=false,smokeStopped_=false;std::size_t smokeNodeCount_=0;Clock::time_point smokeTime_=Clock::now();std::string smokeFailure_;
  std::vector<std::size_t> photoNodes_;std::size_t photoIndex_=0;int viewIndex_=0;bool photoOpen_=false;const Texture* viewerTexture_=nullptr;std::string renderError_;double maxTextureStepMs_=0;Clock::time_point texturesStarted_=Clock::now();
  int galleryStage_=0;std::size_t galleryImages_=0,galleryTexts_=0,galleryBytes_=0;bool galleryPhotoClicked_=false,galleryViewer_=false,galleryPortal_=false,galleryText3d_=false,galleryHologram_=false,galleryWalk_=false,galleryBounds_=false,galleryLook_=false,galleryPortalCaptured_=false,galleryReload_=false,galleryReleased_=false;std::string galleryFailure_;Clock::time_point galleryTime_=Clock::now();Vec3 galleryWalkStart_{};std::string galleryPortalAddress_;std::size_t galleryWalkSteps_=0;double galleryWalkDistance_=0,galleryExpectedDistance_=0;
  static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);static LRESULT CALLBACK viewProc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK buttonProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  static LRESULT CALLBACK editProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  LRESULT message(UINT,WPARAM,LPARAM);LRESULT viewMessage(UINT,WPARAM,LPARAM);
  int px(int n)const{return MulDiv(n,dpi_,96);}HWND control(const wchar_t*,const wchar_t*,DWORD,int,DWORD=0);
  void layout();void paint();void drawText(HDC,const std::wstring&,RECT,COLORREF,int=13,bool=false,UINT=DT_LEFT|DT_TOP|DT_NOPREFIX);
  void createControls();void initGl();void destroyGl();void render();void frame();void command(int);void request(const std::string&);
  void log(bool,const std::string&);void refreshInspector();void click(int,int);bool clickBinding(std::size_t);void smoke();void finishSmoke(bool);bool capture(const std::filesystem::path&);
  void jumpView(int);void openPhoto(std::size_t);void closePhoto();void movePhoto(int);void setPhotoControls();void clearWorldGraphics();void renderPhoto();bool clickImage(bool);void gallerySmoke();void finishGallerySmoke(bool);
  bool walking()const;bool movementKey(WPARAM)const;void updateWalk(double);void lookWalk(int,int);
  void drawButton(const DRAWITEMSTRUCT&);void drawIcon(HDC,int,int,int,int,COLORREF);void toggleTools();void showMenu();void refreshShell();void createFonts();bool shortcut(WPARAM,bool);void shellSmoke();void finishShellSmoke(bool);bool homeLayoutFits()const;
};

HWND Desktop::control(const wchar_t* kind,const wchar_t* text,DWORD style,int id,DWORD ex) {
  HWND child=CreateWindowExW(ex,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
  if(!child)throw std::runtime_error("Could not create native UI control");SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);controls_[id]=child;
  if(std::wstring(kind)==L"BUTTON")SetWindowSubclass(child,buttonProc,1,reinterpret_cast<DWORD_PTR>(this));return child;
}
void Desktop::createFonts(){
  if(font_)DeleteObject(font_);if(mono_)DeleteObject(mono_);if(addressFont_)DeleteObject(addressFont_);
  font_=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  addressFont_=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  mono_=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
  for(auto [id,child]:controls_)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(id==Inspector||id==Logs?mono_:font_),TRUE);
}
void Desktop::createControls() {
  dpi_=GetDpiForWindow(window_);background_=CreateSolidBrush(RGB(255,255,255));paper_=CreateSolidBrush(RGB(248,250,252));photoBrush_=CreateSolidBrush(RGB(14,17,18));createFonts();
  address_=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,Address);
  SendMessageW(address_,EM_SETLIMITTEXT,4096,0);SendMessageW(address_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
  SetWindowSubclass(address_,editProc,2,reinterpret_cast<DWORD_PTR>(this));
  for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t*>>{{Open,L"열기"},{Reload,L"새로 고침 · F5"},{Home,L"홈"},{Stop,L"실행 중지"},{Cancel,L"로딩 취소 · Esc"},{Gallery,L"흑룡이 전시회"},{RoomA,L"Room A"},{RoomB,L"Room B"},{Menu,L"메뉴"},{DeveloperClose,L"개발자 도구 닫기"},{ErrorDetails,L"자세히"}})control(L"BUTTON",title,BS_OWNERDRAW|WS_TABSTOP,id);
  status_=control(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS,Status);
  nodes_=control(L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP,NodeList);
  inspector_=control(L"EDIT",L"공간 또는 목록에서 노드를 선택하세요.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,Inspector);
  logs_=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,Logs);
  SendMessageW(inspector_,WM_SETFONT,reinterpret_cast<WPARAM>(mono_),TRUE);SendMessageW(logs_,WM_SETFONT,reinterpret_cast<WPARAM>(mono_),TRUE);
  viewport_=CreateWindowExW(0,L"LINK.WorldViewport",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,1,1,window_,nullptr,GetModuleHandleW(nullptr),this);
  if(!viewport_)throw std::runtime_error("Could not create native 3D viewport");initGl();
  for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t*>>{{PreviousPhoto,L"이전 사진"},{NextPhoto,L"다음 사진"},{ClosePhoto,L"닫기 · Esc"}})control(L"BUTTON",title,BS_OWNERDRAW|WS_TABSTOP,id);
  control(L"STATIC",L"",SS_LEFT,PhotoTitle);
  for(auto id:{PreviousPhoto,NextPhoto,ClosePhoto,PhotoTitle})ShowWindow(controls_[id],SW_HIDE);
  for(auto id:{Cancel,Stop,Reload,Home,NodeList,Inspector,Logs,Status,DeveloperClose,ErrorDetails})ShowWindow(controls_[id],SW_HIDE);
  EnableWindow(controls_[Reload],FALSE);EnableWindow(controls_[Stop],FALSE);
  tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
  for(auto id:{Home,Reload,Menu,Cancel,Open,DeveloperClose}){TOOLINFOW tool{};tool.cbSize=sizeof(tool);tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=window_;tool.uId=reinterpret_cast<UINT_PTR>(controls_[id]);tool.lpszText=const_cast<wchar_t*>(id==Home?L"시작 화면":id==Reload?L"새로 고침 (F5)":id==Menu?L"메뉴":id==Cancel?L"로딩 취소 (Esc)":id==DeveloperClose?L"개발자 도구 닫기 (F12)":L"공간 열기 (Enter)");SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
}
void Desktop::initGl(){
  glDc_=GetDC(viewport_);PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cDepthBits=24;pfd.cStencilBits=8;pfd.iLayerType=PFD_MAIN_PLANE;
  int format=ChoosePixelFormat(glDc_,&pfd);if(!format||!SetPixelFormat(glDc_,format,&pfd))throw std::runtime_error("OpenGL pixel format initialization failed");
  glContext_=wglCreateContext(glDc_);if(!glContext_||!wglMakeCurrent(glDc_,glContext_))throw std::runtime_error("WGL context initialization failed");
  auto text=[](GLenum key){const auto* p=glGetString(key);return p?std::string(reinterpret_cast<const char*>(p)):std::string("unknown");};glDescription_=text(GL_VENDOR)+" / "+text(GL_RENDERER)+" / "+text(GL_VERSION);
  glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glEnable(GL_NORMALIZE);glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_LIGHT1);glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);glDisable(GL_CULL_FACE);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glClearColor(.066f,.11f,.13f,1);log(false,"WGL ready: "+glDescription_);
}
void Desktop::destroyGl(){if(glContext_){wglMakeCurrent(glDc_,glContext_);textures_.clear();wglMakeCurrent(nullptr,nullptr);wglDeleteContext(glContext_);glContext_=nullptr;}if(glDc_&&viewport_){ReleaseDC(viewport_,glDc_);glDc_=nullptr;}}
void Desktop::layout(){
  if(!viewport_)return;
  RECT client{};GetClientRect(window_,&client);width_=client.right;height_=client.bottom;
  const bool active=navigator_.current()!=nullptr,loading=navigator_.loading()||textures_.loading();
  const int side=toolsOpen_?std::min(px(336),std::max(px(280),width_/3)):0;
  contentWidth_=std::max(1,width_-side);
  viewRect_={0,px(64),contentWidth_,std::max(px(64),height_-px(30))};
  viewWidth_=viewRect_.right-viewRect_.left;viewHeight_=std::max<int>(0,viewRect_.bottom-viewRect_.top);
  MoveWindow(viewport_,viewRect_.left,viewRect_.top,viewWidth_,viewHeight_,TRUE);
  ShowWindow(viewport_,active?SW_SHOW:SW_HIDE);
  for(auto id:{Gallery,RoomA,RoomB})ShowWindow(controls_[id],active?SW_HIDE:SW_SHOW);
  for(auto id:{Home,Reload})ShowWindow(controls_[id],active?SW_SHOW:SW_HIDE);
  ShowWindow(controls_[Stop],SW_HIDE);ShowWindow(controls_[Cancel],loading?SW_SHOW:SW_HIDE);
  EnableWindow(controls_[Reload],!displayAddress_.empty());EnableWindow(controls_[Stop],active);
  if(active){
    MoveWindow(controls_[Home],px(12),px(12),px(40),px(40),TRUE);MoveWindow(controls_[Reload],px(57),px(12),px(40),px(40),TRUE);
    addressPill_={px(110),px(11),contentWidth_-px(66)-(loading?px(43):0),px(53)};
    MoveWindow(address_,addressPill_.left+px(39),px(23),std::max<int>(px(60),addressPill_.right-addressPill_.left-px(87)),px(22),TRUE);
    MoveWindow(controls_[Open],addressPill_.right-px(36),px(16),px(30),px(32),TRUE);
    MoveWindow(controls_[Cancel],contentWidth_-px(104),px(14),px(36),px(36),TRUE);
    SendMessageW(address_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
  }else{
    homeAnchor_=std::clamp(int(height_*.38),px(173),std::max(px(173),height_-px(244)));
    const int fieldWidth=std::max(px(280),std::min(px(620),contentWidth_-px(64)));
    const int x=(contentWidth_-fieldWidth)/2,y=homeAnchor_+px(28);
    addressPill_={x,y,x+fieldWidth,y+px(58)};
    MoveWindow(address_,x+px(54),y+px(18),std::max(px(80),fieldWidth-px(153)-(loading?px(40):0)),px(26),TRUE);
    MoveWindow(controls_[Open],x+fieldWidth-px(83),y+px(8),px(75),px(42),TRUE);
    MoveWindow(controls_[Cancel],x+fieldWidth-px(123),y+px(11),px(36),px(36),TRUE);
    const int gap=px(18),tileWidth=std::min(px(116),std::max(px(80),(contentWidth_-px(48)-gap*2)/3)),rowWidth=tileWidth*3+gap*2;int tileX=(contentWidth_-rowWidth)/2;
    for(auto id:{Gallery,RoomA,RoomB}){MoveWindow(controls_[id],tileX,homeAnchor_+px(119),tileWidth,px(109),TRUE);tileX+=tileWidth+gap;}
    SendMessageW(address_,WM_SETFONT,reinterpret_cast<WPARAM>(addressFont_),TRUE);
  }
  MoveWindow(controls_[Menu],contentWidth_-px(51),px(13),px(38),px(38),TRUE);
  const bool hasStatus=active||loading||shellError_;
  ShowWindow(status_,hasStatus?SW_SHOW:SW_HIDE);ShowWindow(controls_[ErrorDetails],shellError_?SW_SHOW:SW_HIDE);
  LONG_PTR statusStyle=GetWindowLongPtrW(status_,GWL_STYLE);statusStyle&=~SS_TYPEMASK;statusStyle|=active?SS_LEFT:SS_CENTER;SetWindowLongPtrW(status_,GWL_STYLE,statusStyle);
  if(active){MoveWindow(status_,px(18),height_-px(24),std::max(px(160),contentWidth_-(shellError_?px(112):px(330))),px(20),TRUE);MoveWindow(controls_[ErrorDetails],contentWidth_-px(87),height_-px(28),px(72),px(25),TRUE);}
  else{MoveWindow(status_,px(28),homeAnchor_+px(244),std::max(px(100),contentWidth_-px(56)),px(22),TRUE);MoveWindow(controls_[ErrorDetails],(contentWidth_-px(76))/2,homeAnchor_+px(272),px(76),px(27),TRUE);}
  for(auto id:{NodeList,Inspector,Logs,DeveloperClose})ShowWindow(controls_[id],toolsOpen_?SW_SHOW:SW_HIDE);
  if(toolsOpen_){
    const int sx=contentWidth_+px(18),sw=side-px(36);MoveWindow(controls_[DeveloperClose],width_-px(49),px(13),px(34),px(34),TRUE);
    const int listY=px(91),listH=std::max(px(88),std::min(px(195),height_/4));MoveWindow(nodes_,sx,listY,sw,listH,TRUE);
    const int inspectY=listY+listH+px(43),inspectH=std::max(px(112),std::min(px(210),height_/3-px(24)));MoveWindow(inspector_,sx,inspectY,sw,inspectH,TRUE);
    const int logY=inspectY+inspectH+px(43);MoveWindow(logs_,sx,logY,sw,std::max(px(40),height_-logY-px(44)),TRUE);
  }
  const int photoActions=px(350);int photoX=(contentWidth_-photoActions)/2;
  for(auto id:{PreviousPhoto,NextPhoto,ClosePhoto}){MoveWindow(controls_[id],photoX,viewRect_.bottom-px(53),px(110),px(35),TRUE);photoX+=px(120);}
  MoveWindow(controls_[PhotoTitle],px(23),viewRect_.top+px(17),std::max(1,contentWidth_-px(46)),px(42),TRUE);
  setPhotoControls();InvalidateRect(window_,nullptr,TRUE);
}
void Desktop::drawText(HDC dc,const std::wstring& text,RECT rect,COLORREF color,int size,bool bold,UINT format){
  HFONT f=CreateFontW(-px(size),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto old=SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rect,format);SelectObject(dc,old);DeleteObject(f);
}
void Desktop::drawIcon(HDC dc,int id,int cx,int cy,int size,COLORREF color){
  int x=cx-size/2,y=cy-size/2;auto v=[&](int n){return MulDiv(n,size,20);};auto point=[&](int a,int b){return POINT{x+v(a),y+v(b)};};
  HPEN pen=CreatePen(PS_SOLID,std::max(1,size/13),color);auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
  auto line=[&](std::initializer_list<POINT> points){std::vector<POINT> p(points);Polyline(dc,p.data(),static_cast<int>(p.size()));};
  if(id==Home){line({point(2,9),point(10,2),point(18,9)});line({point(4,8),point(4,18),point(8,18),point(8,12),point(12,12),point(12,18),point(16,18),point(16,8)});}
  else if(id==Reload){Arc(dc,x+v(3),y+v(3),x+v(17),y+v(17),x+v(17),y+v(7),x+v(17),y+v(13));line({point(17,2),point(17,7),point(12,7)});}
  else if(id==Cancel||id==ClosePhoto||id==DeveloperClose){line({point(5,5),point(15,15)});line({point(5,15),point(15,5)});}
  else if(id==Menu){HBRUSH dot=CreateSolidBrush(color);SelectObject(dc,dot);for(int n:{4,10,16})Ellipse(dc,x+v(n-1),y+v(9),x+v(n+1)+1,y+v(11)+1);SelectObject(dc,GetStockObject(NULL_BRUSH));DeleteObject(dot);}
  else if(id==Gallery){Ellipse(dc,x+v(4),y+v(6),x+v(16),y+v(18));line({point(4,10),point(3,3),point(8,7)});line({point(12,7),point(17,3),point(16,10)});line({point(7,11),point(7,12)});line({point(13,11),point(13,12)});line({point(9,14),point(10,15),point(11,14)});}
  else if(id==RoomA){RoundRect(dc,x+v(5),y+v(2),x+v(16),y+v(18),v(2),v(2));line({point(5,2),point(12,5),point(12,18)});line({point(9,11),point(10,11)});}
  else if(id==RoomB){line({point(6,3),point(14,4),point(18,11),point(13,17),point(5,17),point(2,10),point(6,3)});line({point(6,3),point(8,10),point(18,11)});line({point(8,10),point(5,17)});}
  else if(id==FocusAddress){RoundRect(dc,x+v(2),y+v(7),x+v(13),y+v(17),v(5),v(5));RoundRect(dc,x+v(8),y+v(2),x+v(19),y+v(12),v(5),v(5));}
  else{const bool previous=id==PreviousPhoto;line({point(previous?16:3,10),point(previous?4:17,10)});line({point(previous?9:12,5),point(previous?4:17,10),point(previous?9:12,15)});}
  SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
}
void Desktop::drawButton(const DRAWITEMSTRUCT& item){
  const int id=static_cast<int>(item.CtlID),w=item.rcItem.right-item.rcItem.left,h=item.rcItem.bottom-item.rcItem.top;if(w<=0||h<=0)return;
  HDC dc=CreateCompatibleDC(item.hDC);HBITMAP bitmap=CreateCompatibleBitmap(item.hDC,w*2,h*2);auto oldBitmap=SelectObject(dc,bitmap);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,w,h,nullptr);SetViewportExtEx(dc,w*2,h*2,nullptr);
  const bool disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0,hover=hovered_==id;
  const bool tile=id==Gallery||id==RoomA||id==RoomB,photo=id==PreviousPhoto||id==NextPhoto||id==ClosePhoto,active=navigator_.current()!=nullptr;
  COLORREF canvas=photo?RGB(14,17,18):((active&&id!=ErrorDetails)||id==DeveloperClose)?RGB(248,250,252):RGB(255,255,255);if(id==Open)canvas=RGB(255,255,255);
  HBRUSH base=CreateSolidBrush(canvas);RECT area{0,0,w,h};FillRect(dc,&area,base);DeleteObject(base);
  auto round=[&](RECT r,COLORREF fill,int radius){HBRUSH brush=CreateSolidBrush(fill);auto old=SelectObject(dc,brush);auto p=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,p);SelectObject(dc,old);DeleteObject(brush);};
  const COLORREF ink=disabled?RGB(190,199,209):RGB(83,99,117),accent=disabled?RGB(176,197,192):RGB(18,137,119);
  if(tile){
    if(hover||pressed)round({1,1,w-1,h-1},pressed?RGB(232,239,242):RGB(245,248,250),px(17));
    RECT circle{(w-px(49))/2,px(11),(w+px(49))/2,px(60)};round(circle,id==Gallery?RGB(231,244,240):id==RoomA?RGB(237,242,248):RGB(241,239,247),px(49));
    drawIcon(dc,id,w/2,px(36),px(24),id==Gallery?RGB(18,137,119):id==RoomA?RGB(93,119,148):RGB(126,112,149));
    drawText(dc,windowText(item.hwndItem),{px(2),px(76),w-px(2),h-px(7)},RGB(79,95,113),12,false,DT_CENTER|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
  }else if(id==Open){
    round({1,1,w-1,h-1},active?(hover?RGB(214,238,231):RGB(232,245,240)):(pressed?RGB(14,111,96):hover?RGB(24,153,133):RGB(18,137,119)),px(active?17:26));
    if(active)drawIcon(dc,Open,w/2,h/2,px(18),accent);else drawText(dc,L"열기",{0,0,w,h},RGB(255,255,255),13,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
  }else if(photo||id==ErrorDetails){
    round({1,1,w-1,h-1},pressed?RGB(220,230,235):hover?RGB(235,243,245):RGB(249,251,252),px(28));
    drawText(dc,windowText(item.hwndItem),{0,0,w,h},id==ErrorDetails?accent:RGB(58,79,96),12,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
  }else{
    if(hover||pressed)round({1,1,w-1,h-1},pressed?RGB(226,233,239):RGB(237,242,246),px(36));drawIcon(dc,id,w/2,h/2,px(19),ink);
  }
  if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){HPEN pen=CreatePen(PS_DOT,1,RGB(55,151,135));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,px(3),px(3),w-px(3),h-px(3),px(tile?15:22),px(tile?15:22));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);}
  SetMapMode(dc,MM_TEXT);SetStretchBltMode(item.hDC,HALFTONE);SetBrushOrgEx(item.hDC,0,0,nullptr);StretchBlt(item.hDC,item.rcItem.left,item.rcItem.top,w,h,dc,0,0,w*2,h*2,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK Desktop::buttonProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data){
  auto* app=reinterpret_cast<Desktop*>(data);int id=GetDlgCtrlID(window);
  if(message==WM_MOUSEMOVE&&app->hovered_!=id){int old=app->hovered_;app->hovered_=id;if(app->controls_.contains(old))InvalidateRect(app->controls_[old],nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);}
  if(message==WM_MOUSELEAVE&&app->hovered_==id){app->hovered_=0;InvalidateRect(window,nullptr,FALSE);}
  if(message==WM_SETFOCUS||message==WM_KILLFOCUS)InvalidateRect(window,nullptr,FALSE);
  if(message==WM_SETCURSOR&&IsWindowEnabled(window)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,buttonProc,1);
  return DefSubclassProc(window,message,wParam,lParam);
}
LRESULT CALLBACK Desktop::editProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data){
  auto* app=reinterpret_cast<Desktop*>(data);
  if(message==WM_IME_STARTCOMPOSITION)app->addressComposing_=true;
  if(message==WM_IME_ENDCOMPOSITION)app->addressComposing_=false;
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,editProc,2);
  LRESULT result=DefSubclassProc(window,message,wParam,lParam);
  if(message==WM_PAINT&&!app->addressComposing_&&GetWindowTextLengthW(window)==0){HDC dc=GetDC(window);RECT rect{};GetClientRect(window,&rect);auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));auto old=SelectObject(dc,font);SetTextColor(dc,RGB(143,156,172));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"LINK 주소를 입력하세요",-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);}
  if(message==WM_SETTEXT||message==WM_IME_STARTCOMPOSITION||message==WM_IME_ENDCOMPOSITION)InvalidateRect(window,nullptr,TRUE);
  return result;
}
void Desktop::paint(){
  PAINTSTRUCT ps{};HDC target=BeginPaint(window_,&ps);HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1,width_),std::max(1,height_));auto oldBitmap=SelectObject(dc,bitmap);
  RECT all{0,0,width_,height_};FillRect(dc,&all,background_);const bool active=navigator_.current()!=nullptr;
  auto rounded=[&](RECT r,COLORREF fill,COLORREF stroke,int radius){HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,stroke);auto oldBrush=SelectObject(dc,brush);auto oldPen=SelectObject(dc,pen);RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);};
  if(active){
    RECT bar{0,0,contentWidth_,px(64)};FillRect(dc,&bar,paper_);
    HPEN line=CreatePen(PS_SOLID,1,RGB(229,234,239));auto old=SelectObject(dc,line);MoveToEx(dc,0,px(63),nullptr);LineTo(dc,contentWidth_,px(63));SelectObject(dc,old);DeleteObject(line);
    if(!shellError_&&contentWidth_>px(540))drawText(dc,photoOpen_?L"← → 사진 이동   ·   Esc 닫기":walking()?L"WASD 이동  ·  드래그 둘러보기":L"드래그 회전  ·  우클릭 이동  ·  휠 확대",{contentWidth_-px(308),height_-px(23),contentWidth_-px(17),height_-px(3)},RGB(102,116,134),11,false,DT_RIGHT|DT_TOP|DT_SINGLELINE);
  }else{
    drawText(dc,L"LINK Browser",{px(26),px(24),px(210),px(45)},RGB(107,119,134),12);
    drawText(dc,L"LINK",{0,homeAnchor_-px(121),contentWidth_,homeAnchor_-px(20)},RGB(30,41,55),83,true,DT_CENTER|DT_TOP|DT_SINGLELINE);
    drawText(dc,L"Every World. One Link.",{0,homeAnchor_-px(14),contentWidth_,homeAnchor_+px(14)},RGB(119,132,149),15,false,DT_CENTER|DT_TOP|DT_SINGLELINE);
    drawText(dc,L"Linked Interactive Network",{px(26),height_-px(34),contentWidth_-px(26),height_-px(11)},RGB(115,129,147),10);
    drawText(dc,L"v0.1.0-alpha",{px(26),height_-px(34),contentWidth_-px(26),height_-px(11)},RGB(115,129,147),10,false,DT_RIGHT|DT_TOP|DT_SINGLELINE);
  }
  const bool addressFocus=GetFocus()==address_;
  if(!active){RECT shadow=addressPill_;OffsetRect(&shadow,0,px(3));rounded(shadow,RGB(245,247,249),RGB(245,247,249),px(60));}
  rounded(addressPill_,RGB(255,255,255),addressFocus?RGB(100,177,164):RGB(218,225,232),px(active?23:58));
  drawIcon(dc,FocusAddress,addressPill_.left+px(active?20:28),(addressPill_.top+addressPill_.bottom)/2,px(active?16:19),addressFocus?RGB(25,139,123):RGB(139,153,168));
  if(toolsOpen_){
    RECT side{contentWidth_,0,width_,height_};FillRect(dc,&side,paper_);HPEN line=CreatePen(PS_SOLID,1,RGB(224,231,238));auto old=SelectObject(dc,line);MoveToEx(dc,contentWidth_,0,nullptr);LineTo(dc,contentWidth_,height_);SelectObject(dc,old);DeleteObject(line);
    drawText(dc,L"개발자 도구",{contentWidth_+px(19),px(21),width_-px(55),px(44)},RGB(44,61,78),14,true);
    for(auto [child,title]:std::initializer_list<std::pair<HWND,const wchar_t*>>{{nodes_,L"공간 구조"},{inspector_,L"노드 검사기"},{logs_,L"실행 로그"}}){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&r),2);drawText(dc,title,{r.left,r.top-px(25),r.right,r.top-px(3)},RGB(104,119,138),11,true);RECT border=r;InflateRect(&border,1,1);FrameRect(dc,&border,static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));}
    auto c=counters();std::wostringstream info;info<<L"worlds "<<c.activeWorlds<<L"  bindings "<<c.activeBindings<<L"  textures "<<textures_.count()<<L" / "<<textures_.bytes()/1024/1024<<L" MiB";drawText(dc,info.str(),{contentWidth_+px(18),height_-px(27),width_-px(14),height_-px(7)},RGB(136,150,166),9);
  }
  BitBlt(target,0,0,width_,height_,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window_,&ps);
}
void Desktop::render(){
  auto* runtime=navigator_.current();if(!runtime||!glContext_||viewWidth_<=0||viewHeight_<=0||minimized_)return;
  wglMakeCurrent(glDc_,glContext_);if(photoOpen_){renderPhoto();SwapBuffers(glDc_);++frames_;return;}unsigned background=static_cast<unsigned>(std::stoul(runtime->world().background.substr(1),nullptr,16));glClearColor(float((background>>16)&255)/255,float((background>>8)&255)/255,float(background&255)/255,1);glViewport(0,0,viewWidth_,viewHeight_);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glMatrixMode(GL_PROJECTION);glLoadIdentity();double nearZ=.05,top=nearZ*std::tan(45*pi/360),aspect=double(viewWidth_)/viewHeight_;glFrustum(-top*aspect,top*aspect,-top,top,nearZ,500);glMatrixMode(GL_MODELVIEW);auto view=camera_.view();glLoadMatrixd(view.m.data());
  if(runtime->world().fog){const auto& fog=*runtime->world().fog;const auto color=static_cast<unsigned>(std::stoul(fog.color.substr(1),nullptr,16));GLfloat rgba[]={float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,1};glEnable(GL_FOG);glFogi(GL_FOG_MODE,GL_LINEAR);glFogfv(GL_FOG_COLOR,rgba);glFogf(GL_FOG_START,float(fog.start));glFogf(GL_FOG_END,float(fog.end));}else glDisable(GL_FOG);
  GLfloat ambient[]={.26f,.28f,.30f,1},key[]={.74f,.72f,.66f,1},fill[]={.22f,.28f,.33f,1},keyPos[]={-.6f,1,.8f,0},fillPos[]={.7f,.4f,-.8f,0};glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);glLightfv(GL_LIGHT0,GL_DIFFUSE,key);glLightfv(GL_LIGHT0,GL_SPECULAR,key);glLightfv(GL_LIGHT0,GL_POSITION,keyPos);glLightfv(GL_LIGHT1,GL_DIFFUSE,fill);glLightfv(GL_LIGHT1,GL_POSITION,fillPos);
  if(runtime->world().grid){glDisable(GL_LIGHTING);glColor3f(.17f,.25f,.25f);glBegin(GL_LINES);for(int i=-12;i<=12;++i){glVertex3d(i,-.025,-12);glVertex3d(i,-.025,12);glVertex3d(-12,-.025,i);glVertex3d(12,-.025,i);}glEnd();glEnable(GL_LIGHTING);}
  auto matrices=sceneMatrices(*runtime);std::vector<std::size_t> opaque,transparent;
  for(std::size_t i=0;i<runtime->nodes().size();++i)if(runtime->nodes()[i].definition.kind==Kind::Element&&matrices.visible[i]){auto material=runtime->material(i);if(material.opacity<=0)continue;const auto* text=runtime->nodes()[i].definition.text.empty()?nullptr:textures_.text(runtime->nodes()[i].definition.id);const bool alphaText=text&&!text->textBackground;(material.opacity<1||alphaText?transparent:opaque).push_back(i);}
  std::sort(transparent.begin(),transparent.end(),[&](auto a,auto b){return dot(sub(matrices.world[a].point({0,0,0}),camera_.eye()),camera_.forward())>dot(sub(matrices.world[b].point({0,0,0}),camera_.eye()),camera_.forward());});
  auto draw=[&](std::size_t i){glPushMatrix();glMultMatrixd(matrices.world[i].m.data());const auto& node=runtime->nodes()[i].definition;auto material=runtime->material(i);const Texture* texture=node.image.empty()?textures_.text(node.id):textures_.image(node.image);if(texture)texturedQuad(*texture,node.geometry.size[0],node.geometry.size[1],material.opacity);else{applyMaterial(material);geometry(node.geometry);}glPopMatrix();};
  glDisable(GL_BLEND);glDepthMask(GL_TRUE);for(auto i:opaque)draw(i);glEnable(GL_BLEND);glDepthMask(GL_FALSE);for(auto i:transparent)draw(i);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
  // Small additive shells soften authored light sources while keeping their real geometry.
  glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_CURRENT_BIT|GL_POLYGON_BIT);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glDepthMask(GL_FALSE);
  for(std::size_t i=0;i<runtime->nodes().size();++i){const auto& node=runtime->nodes()[i].definition;if(node.kind!=Kind::Element||!matrices.visible[i]||!node.image.empty()||!node.text.empty())continue;const auto material=runtime->material(i);if(material.emission<=0||material.opacity<=0)continue;const auto rgb=static_cast<unsigned>(std::stoul(material.color.substr(1),nullptr,16));
    for(auto [scale,alpha]:{std::pair{1.10,.018},std::pair{1.045,.045}}){glColor4d(double((rgb>>16)&255)/255,double((rgb>>8)&255)/255,double(rgb&255)/255,alpha*std::min(material.emission,2.)*material.opacity);glPushMatrix();glMultMatrixd(matrices.world[i].m.data());glScaled(scale,scale,scale);geometry(node.geometry);glPopMatrix();}
  }glPopAttrib();
  if(toolsOpen_&&!selected_.empty()){glDisable(GL_LIGHTING);glColor3f(.68f,.87f,.66f);glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glLineWidth(1.5f);glEnable(GL_POLYGON_OFFSET_LINE);glPolygonOffset(-1,-1);
    for(std::size_t i=0;i<runtime->nodes().size();++i){if(runtime->nodes()[i].definition.kind!=Kind::Element||!matrices.visible[i])continue;auto p=i;bool selected=false;while(p!=noParent){if(runtime->nodes()[p].definition.id==selected_){selected=true;break;}p=runtime->nodes()[p].definition.parent;}if(selected){glPushMatrix();glMultMatrixd(matrices.world[i].m.data());geometry(runtime->nodes()[i].definition.geometry);glPopMatrix();}}
    glDisable(GL_POLYGON_OFFSET_LINE);glLineWidth(1);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glEnable(GL_LIGHTING);
  }SwapBuffers(glDc_);++frames_;
}
void Desktop::renderPhoto(){
  glDisable(GL_FOG);glViewport(0,0,viewWidth_,viewHeight_);glClearColor(.055f,.065f,.07f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,viewWidth_,0,viewHeight_,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
  if(photoIndex_<photoNodes_.size()){auto* runtime=navigator_.current();if(runtime){const auto& image=runtime->nodes()[photoNodes_[photoIndex_]].definition.image;const Texture* texture=textures_.image(image); // Fallback while a larger viewer image is unavailable.
      auto found=runtime->world().images.find(image);(void)found;
      const auto* large=viewerTexture_;if(large)texture=large;
      if(texture){double availableW=std::max(1,viewWidth_-px(54)),availableH=std::max(1,viewHeight_-px(143));double w=availableW,h=w/texture->aspect;if(h>availableH){h=availableH;w=h*texture->aspect;}glPushMatrix();glTranslated(viewWidth_/2.,(viewHeight_+px(3))/2.,0);texturedQuad(*texture,w,h);glPopMatrix();}
  }}glEnable(GL_DEPTH_TEST);
}
void Desktop::request(const std::string& address){walkKeys_.fill(false);navigator_.request(address);SetWindowTextW(address_,wide(address).c_str());lastError_.clear();renderError_.clear();refreshShell();}
bool Desktop::walking()const{auto* runtime=navigator_.current();return runtime&&runtime->world().walk.has_value();}
bool Desktop::movementKey(WPARAM key)const{return key=='W'||key=='A'||key=='S'||key=='D'||key==VK_UP||key==VK_LEFT||key==VK_DOWN||key==VK_RIGHT;}
void Desktop::lookWalk(int dx,int dy){
  const auto eye=camera_.eye();camera_.yaw-=dx*.004;camera_.pitch=std::clamp(camera_.pitch+dy*.004,-1.2,1.2);camera_.target=add(eye,mul(camera_.forward(),camera_.distance));
}
void Desktop::updateWalk(double seconds){
  if(!walking()||photoOpen_||navigator_.loading()||textures_.loading()||GetFocus()!=viewport_)return;
  const double forward=double(walkKeys_['W']||walkKeys_[VK_UP])-double(walkKeys_['S']||walkKeys_[VK_DOWN]);
  const double right=double(walkKeys_['D']||walkKeys_[VK_RIGHT])-double(walkKeys_['A']||walkKeys_[VK_LEFT]);
  if(forward==0&&right==0)return;
  auto& world=navigator_.current()->world();const auto from=camera_.eye(),to=advanceWalk(*world.walk,from,camera_.forward(),forward,right,seconds);
  camera_.target=add(camera_.target,sub(to,from));
  if(!gallerySmokePath.empty()&&galleryStage_==2&&dot(sub(to,from),sub(to,from))>1e-12){++galleryWalkSteps_;galleryWalkDistance_+=std::sqrt(dot(sub(to,from),sub(to,from)));}
  if(const auto* portal=enteredPortal(world,from,to)){const auto address=portal->address;log(false,"Portal "+portal->id+" -> "+address);request(address);}
}
void Desktop::command(int id){
  if(id==Menu){showMenu();return;}
  if(id==DeveloperTools||id==DeveloperClose){toggleTools();return;}
  if(id==ErrorDetails){if(!toolsOpen_)toggleTools();SetFocus(logs_);return;}
  if(id==FocusAddress){SetFocus(address_);SendMessageW(address_,EM_SETSEL,0,-1);return;}
  if(id==Open){auto value=utf8(windowText(address_));if(!value.empty())request(value);}
  if(id==Gallery)request("link://local/heukryong");
  if(id==RoomA)request("link://local/room-a");if(id==RoomB)request("link://local/room-b");
  if(id==Reload){auto address=displayAddress_;if(!address.empty())request(address);}
  if(id==Home||id==Stop){clearWorldGraphics();navigator_.stop();selected_.clear();displayAddress_.clear();mode_=id==Home?L"시작 화면":L"실행 중지됨";lastError_.clear();renderError_.clear();SetWindowTextW(address_,L"");refreshInspector();refreshShell();SetFocus(address_);InvalidateRect(window_,nullptr,FALSE);}
  if(id==Cancel){navigator_.cancel();if(textures_.loading()){clearWorldGraphics();navigator_.stop();mode_=L"사진 준비 취소";}lastError_.clear();log(false,"Loading cancelled.");}
  if(id==PreviousPhoto)movePhoto(-1);
  if(id==NextPhoto)movePhoto(1);
  if(id==ClosePhoto)closePhoto();
  if(id==NodeList){int row=static_cast<int>(SendMessageW(nodes_,LB_GETCURSEL,0,0));if(row>=0&&static_cast<std::size_t>(row)<nodeIds_.size()){selected_=nodeIds_[row];refreshInspector();}}
  refreshShell();
}
void Desktop::toggleTools(){
  toolsOpen_=!toolsOpen_;if(!toolsOpen_&&(GetFocus()==nodes_||GetFocus()==inspector_||GetFocus()==logs_||GetFocus()==controls_[DeveloperClose]))SetFocus(navigator_.current()?viewport_:address_);
  layout();render();
}
void Desktop::showMenu(){
  walkKeys_.fill(false);HMENU menu=CreatePopupMenu();const bool active=navigator_.current()!=nullptr,loading=navigator_.loading()||textures_.loading();
  AppendMenuW(menu,MF_STRING,FocusAddress,L"주소로 이동\tCtrl+L");AppendMenuW(menu,MF_STRING|(!displayAddress_.empty()?0:MF_GRAYED),Reload,L"새로 고침\tF5");
  AppendMenuW(menu,MF_STRING,Home,L"홈");AppendMenuW(menu,MF_STRING|(active?0:MF_GRAYED),Stop,L"실행 중지");if(loading)AppendMenuW(menu,MF_STRING,Cancel,L"로딩 취소\tEsc");
  AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(toolsOpen_?MF_CHECKED:0),DeveloperTools,L"개발자 도구\tF12");
  RECT anchor{};GetWindowRect(controls_[Menu],&anchor);int selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_TOPALIGN,anchor.right,anchor.bottom+px(5),window_,nullptr);DestroyMenu(menu);if(selected)command(selected);
}
bool Desktop::shortcut(WPARAM key,bool control){
  if(control&&(key=='L'||key=='l')){command(FocusAddress);return true;}
  if(key==VK_F12){command(DeveloperTools);return true;}
  if(key==VK_F5){command(Reload);return true;}
  if(key==VK_RETURN&&GetFocus()==address_){command(Open);return true;}
  if(key==VK_ESCAPE){if(photoOpen_){closePhoto();return true;}if(navigator_.loading()||textures_.loading()){command(Cancel);return true;}}
  if(photoOpen_&&(key==VK_LEFT||key==VK_RIGHT)){movePhoto(key==VK_LEFT?-1:1);return true;}
  return false;
}
void Desktop::refreshShell(){
  auto* runtime=navigator_.current();const bool active=runtime!=nullptr,loading=navigator_.loading()||textures_.loading(),error=!navigator_.error().empty()||!renderError_.empty();
  bool changed=active!=shellActive_||loading!=shellLoading_||error!=shellError_;shellActive_=active;shellLoading_=loading;shellError_=error;
  std::wstring text=runtime?wide(runtime->world().name):L"";
  if(navigator_.loading())text=L"공간을 여는 중…";else if(textures_.loading())text=L"사진 준비 중 · "+std::to_wstring(textures_.completed())+L" / "+std::to_wstring(textures_.total());
  else if(error)text=active?L"공간을 열지 못했습니다 · 현재 공간 유지":L"공간을 열지 못했습니다. 주소를 확인해 주세요.";
  setText(status_,text);if(changed)layout();
}
void Desktop::clearWorldGraphics(){
  walkKeys_.fill(false);
  photoOpen_=false;viewerTexture_=nullptr;photoNodes_.clear();
  if(glContext_)wglMakeCurrent(glDc_,glContext_);
  textures_.clear();setPhotoControls();
}
void Desktop::jumpView(int index){
  auto* runtime=navigator_.current();if(!runtime||runtime->world().views.empty())return;
  closePhoto();walkKeys_.fill(false);const auto size=static_cast<int>(runtime->world().views.size());viewIndex_=(index%size+size)%size;
  const auto& view=runtime->world().views[viewIndex_];camera_.jump(view.position,view.target);render();
}
void Desktop::setPhotoControls(){
  for(auto id:{PreviousPhoto,NextPhoto,ClosePhoto,PhotoTitle}){
    auto found=controls_.find(id);if(found==controls_.end())continue;
    ShowWindow(found->second,photoOpen_?SW_SHOW:SW_HIDE);
    if(photoOpen_)SetWindowPos(found->second,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  }
  InvalidateRect(window_,nullptr,FALSE);
}
void Desktop::openPhoto(std::size_t index){
  auto* runtime=navigator_.current();if(!runtime||photoNodes_.empty())return;
  walkKeys_.fill(false);photoIndex_=index%photoNodes_.size();const auto& node=runtime->nodes()[photoNodes_[photoIndex_]].definition;
  selected_=node.id;wglMakeCurrent(glDc_,glContext_);viewerTexture_=nullptr;
  try{viewerTexture_=&textures_.viewer(runtime->world().images.at(node.image));}
  catch(const std::exception& error){log(true,std::string("Photo viewer: ")+error.what());}
  photoOpen_=true;std::wostringstream title;title<<wide(node.name)<<L"  ·  "<<(photoIndex_+1)<<L" / "<<photoNodes_.size();setText(controls_[PhotoTitle],title.str());setPhotoControls();refreshInspector();render();
}
void Desktop::closePhoto(){photoOpen_=false;viewerTexture_=nullptr;if(glContext_)wglMakeCurrent(glDc_,glContext_);textures_.clearViewer();setPhotoControls();}
void Desktop::movePhoto(int direction){if(photoNodes_.empty())return;auto size=static_cast<int>(photoNodes_.size());auto next=(static_cast<int>(photoIndex_)+direction)%size;openPhoto(static_cast<std::size_t>((next+size)%size));}
void Desktop::log(bool error,const std::string& message){
  std::cout<<(error?"[error] ":"[info] ")<<message<<'\n';SYSTEMTIME time{};GetLocalTime(&time);std::wostringstream line;line<<std::setfill(L'0')<<std::setw(2)<<time.wHour<<L":"<<std::setw(2)<<time.wMinute<<L":"<<std::setw(2)<<time.wSecond<<(error?L"  ! ":L"  · ")<<wide(message);logLines_.push_back(line.str());if(logLines_.size()>40)logLines_.erase(logLines_.begin());
  if(logs_){std::wstring all;for(const auto& l:logLines_)all+=l+L"\r\n";SetWindowTextW(logs_,all.c_str());SendMessageW(logs_,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));SendMessageW(logs_,EM_SCROLLCARET,0,0);}
}
void Desktop::refreshInspector(){
  auto* runtime=navigator_.current();std::string structure;if(runtime)for(const auto& n:runtime->nodes())structure+=n.definition.id+"\n";
  if(structure!=lastStructure_){lastStructure_=structure;SendMessageW(nodes_,LB_RESETCONTENT,0,0);nodeIds_.clear();if(runtime)for(const auto& n:runtime->nodes()){int depth=0;auto parent=n.definition.parent;while(parent!=noParent){++depth;parent=runtime->nodes()[parent].definition.parent;}auto kind=n.definition.kind==Kind::Object?L"O  ":n.definition.kind==Kind::Component?L"C  ":L"·  ";std::wstring label(depth*2,L' ');label+=kind;label+=wide(n.definition.name.empty()?n.definition.id:n.definition.name);SendMessageW(nodes_,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));nodeIds_.push_back(n.definition.id);}}
  if(!runtime||selected_.empty()){
    if(walking()){
      std::wstring help=L"화면을 클릭한 뒤 이동하세요.\r\n\r\nWASD / 방향키: 걷기\r\n마우스 드래그: 둘러보기";
      if(!photoNodes_.empty())help+=L"\r\n사진 더블클릭: 크게 보기";
      if(!runtime->world().portals.empty())help+=L"\r\n\r\n포탈로 걸어 들어가 다음 공간으로 이동하세요.";
      else help+=L"\r\n\r\n걸음을 옮겨 새로운 공간을 탐험하세요.";
      setText(inspector_,help);
    }else setText(inspector_,L"공간 또는 목록에서 노드를 선택하세요.\r\n\r\n마우스 왼쪽: 회전 / 클릭: 상호작용\r\n마우스 오른쪽: 이동\r\n휠: 확대 · 축소");
    return;
  }
  try{const auto& n=runtime->find(selected_);std::wostringstream text;auto kind=n.definition.kind==Kind::Object?L"Object":n.definition.kind==Kind::Component?L"Component":L"Element";text<<wide(n.definition.name)<<L"  ["<<kind<<L"]\r\nID     "<<wide(n.definition.id)<<L"\r\nParent "<<(n.definition.parent==noParent?L"(root)":wide(runtime->nodes()[n.definition.parent].definition.id))<<L"\r\n\r\nLOCAL STATE\r\n";
    for(const auto& [key,value]:n.state)text<<wide(key)<<L" = "<<wide(value.display())<<L"\r\n";if(n.state.empty())text<<L"{}\r\n";
    auto owner=n.definition.config.find("owner");if(owner!=n.definition.config.end()&&std::holds_alternative<std::string>(owner->second.data)){
      try{const auto& ownerNode=runtime->find(owner->second.string());text<<L"\r\nOWNER LOCAL STATE\r\n"<<wide(ownerNode.definition.id)<<L"\r\n";for(const auto& [key,value]:ownerNode.state)text<<wide(key)<<L" = "<<wide(value.display())<<L"\r\n";}catch(const std::exception& e){text<<L"\r\nOwner: "<<wide(e.what());}
    }
    text<<L"\r\nTRANSFORM  (m / degree)\r\n"<<std::fixed<<std::setprecision(2);auto vec=[&](const wchar_t* label,const Vec3& v){text<<label<<v[0]<<L", "<<v[1]<<L", "<<v[2]<<L"\r\n";};vec(L"Pos   ",n.transform.position);vec(L"Rot   ",n.transform.rotation);vec(L"Scale ",n.transform.scale);text<<L"Visible "<<(n.transform.visible?L"true":L"false")<<L"\r\n";
    setText(inspector_,text.str());auto found=std::find(nodeIds_.begin(),nodeIds_.end(),selected_);if(found!=nodeIds_.end())SendMessageW(nodes_,LB_SETCURSEL,found-nodeIds_.begin(),0);
  }catch(const std::exception&){selected_.clear();setText(inspector_,L"선택한 노드가 제거되었습니다.");}
}
void Desktop::click(int x,int y){auto* runtime=navigator_.current();if(!runtime||photoOpen_)return;auto hit=raycast(*runtime,camera_,x,y,viewWidth_,viewHeight_);if(hit==noParent)return;const auto id=runtime->nodes()[hit].definition.id;selected_=runtime->interact(id);if(selected_.empty())selected_=id;for(std::size_t i=0;i<photoNodes_.size();++i)if(photoNodes_[i]==hit)photoIndex_=i;++clicks_;refreshInspector();}
bool Desktop::clickBinding(std::size_t binding){
  auto* runtime=navigator_.current();if(!runtime)return false;
  auto nearest=[&](std::size_t node){while(node!=noParent&&runtime->nodes()[node].definition.script.empty())node=runtime->nodes()[node].definition.parent;return node;};
  auto send=[&](int x,int y){auto hit=raycast(*runtime,camera_,x,y,viewWidth_,viewHeight_);if(hit==noParent||nearest(hit)!=binding)return false;SendMessageW(viewport_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(viewport_,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
  auto matrices=sceneMatrices(*runtime);double tangent=std::tan(45*pi/360),aspect=double(viewWidth_)/viewHeight_;
  for(std::size_t i=0;i<runtime->nodes().size();++i){if(runtime->nodes()[i].definition.kind!=Kind::Element||nearest(i)!=binding)continue;auto to=sub(matrices.world[i].point({0,0,0}),camera_.eye());double depth=dot(to,camera_.forward());if(depth<=.05)continue;int x=int((dot(to,camera_.right())/(depth*tangent*aspect)+1)*viewWidth_/2),y=int((1-dot(to,camera_.up())/(depth*tangent))*viewHeight_/2);if(x>=0&&x<viewWidth_&&y>=0&&y<viewHeight_&&send(x,y))return true;}
  // Fall back to a bounded screen scan if another piece occludes an element's center.
  for(int y=viewHeight_/8;y<viewHeight_*7/8;y+=11)for(int x=viewWidth_/8;x<viewWidth_*7/8;x+=11)if(send(x,y))return true;
  return false;
}
void Desktop::frame(){
  auto now=Clock::now();double delta=std::chrono::duration<double>(now-previous_).count();previous_=now;
  navigator_.poll();auto* runtime=navigator_.current();auto c=counters();
  if(runtime&&createdSeen_!=c.createdWorlds){
    clearWorldGraphics();createdSeen_=c.createdWorlds;camera_.reset(runtime->world());displayAddress_=runtime->world().address;selected_.clear();mode_=L"실행 중";lastStructure_.clear();renderError_.clear();viewIndex_=0;photoIndex_=0;
    for(std::size_t i=0;i<runtime->nodes().size();++i)if(!runtime->nodes()[i].definition.image.empty())photoNodes_.push_back(i);
    texturesStarted_=now;
    try{textures_.begin(runtime->world());}catch(const std::exception& error){renderError_=error.what();clearWorldGraphics();navigator_.stop();runtime=nullptr;log(true,renderError_);mode_=L"사진 준비 실패";}
    layout();if(runtime)SetFocus(viewport_);InvalidateRect(window_,nullptr,FALSE);
  }
  if(runtime&&textures_.loading()&&!minimized_){
    const auto stepStart=Clock::now();
    try{textures_.step();}catch(const std::exception& error){renderError_=error.what();clearWorldGraphics();navigator_.stop();runtime=nullptr;log(true,renderError_);mode_=L"사진 준비 실패";}
    maxTextureStepMs_=std::max(maxTextureStepMs_,std::chrono::duration<double,std::milli>(Clock::now()-stepStart).count());
  }
  refreshShell();
  if(navigator_.error()!=lastError_){lastError_=navigator_.error();InvalidateRect(window_,nullptr,FALSE);}
  if(runtime&&!minimized_&&viewWidth_>0&&viewHeight_>0){updateWalk(delta);runtime->update(std::clamp(delta,0.,.1));}
  if(now-lastInspect_>std::chrono::milliseconds(200)){refreshInspector();lastInspect_=now;if(toolsOpen_){RECT footer{contentWidth_,height_-px(31),width_,height_};InvalidateRect(window_,&footer,FALSE);}}
  render();if(!shellSmokePath.empty())shellSmoke();else if(!gallerySmokePath.empty())gallerySmoke();else if(!smokePath.empty())smoke();
  else if(!capturePath.empty()&&!navigator_.loading()&&!textures_.loading()&&now-started_>std::chrono::seconds(2)){capture(capturePath);capturePath.clear();}
}

bool Desktop::capture(const std::filesystem::path& path){
  if(width_<=0||height_<=0)return false;
  // Flush native child-control repaint messages before copying the client surface.
  RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  HDC source=GetDC(window_),memory=CreateCompatibleDC(source);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width_;info.bmiHeader.biHeight=height_;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=24;info.bmiHeader.biCompression=BI_RGB;
  void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap){DeleteDC(memory);ReleaseDC(window_,source);return false;}
  auto old=SelectObject(memory,bitmap);BitBlt(memory,0,0,width_,height_,source,0,0,SRCCOPY|CAPTUREBLT);
  // Read the real front buffer explicitly; GDI capture of hardware surfaces is driver dependent.
  if(navigator_.current()&&glContext_&&viewWidth_>0&&viewHeight_>0){
    RECT viewportBounds{};GetClientRect(viewport_,&viewportBounds);MapWindowPoints(viewport_,window_,reinterpret_cast<POINT*>(&viewportBounds),2);int capturedWidth=viewportBounds.right-viewportBounds.left,capturedHeight=viewportBounds.bottom-viewportBounds.top;
    wglMakeCurrent(glDc_,glContext_);std::vector<unsigned char> rgb(static_cast<std::size_t>(capturedWidth)*capturedHeight*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_FRONT);glReadPixels(0,0,capturedWidth,capturedHeight,GL_RGB,GL_UNSIGNED_BYTE,rgb.data());
    int stride=(width_*3+3)&~3;auto* dest=static_cast<unsigned char*>(bits);int originX=viewportBounds.left,originY=viewportBounds.top;
    std::vector<RECT> overlays;if(photoOpen_)for(auto id:{PreviousPhoto,NextPhoto,ClosePhoto,PhotoTitle}){RECT rect{};GetWindowRect(controls_[id],&rect);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&rect),2);overlays.push_back(rect);}
    for(int y=0;y<capturedHeight;++y)for(int x=0;x<capturedWidth;++x){int dy=height_-originY-capturedHeight+y,dx=originX+x;if(dy<0||dy>=height_||dx<0||dx>=width_)continue;POINT point{dx,height_-1-dy};if(std::any_of(overlays.begin(),overlays.end(),[&](const RECT& rect){return PtInRect(&rect,point)!=FALSE;}))continue;auto s=(static_cast<std::size_t>(y)*capturedWidth+x)*3;auto d=static_cast<std::size_t>(dy)*stride+dx*3;dest[d]=rgb[s+2];dest[d+1]=rgb[s+1];dest[d+2]=rgb[s];}
  }
  std::size_t bytes=static_cast<std::size_t>((width_*3+3)&~3)*height_;BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=static_cast<DWORD>(header.bfOffBits+bytes);
  bool okay=false;try{if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(static_cast<const char*>(bits),static_cast<std::streamsize>(bytes));okay=bool(file);}catch(...){}
  SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window_,source);log(!okay,okay?"Native screenshot saved: "+utf8(path.wstring()):"Could not save native screenshot");return okay;
}
void Desktop::smoke(){
  if(Clock::now()-started_>std::chrono::seconds(45)){smokeFailure_="Native smoke test timed out";finishSmoke(false);return;}
  auto* runtime=navigator_.current();
  if(smokeStage_==0){if(!runtime||navigator_.loading())return;smokeNodeCount_=runtime->nodes().size();smokeTime_=Clock::now();smokeStage_=1;return;}
  if(smokeStage_==1){
    if(!runtime||Clock::now()-smokeTime_<std::chrono::milliseconds(300))return;
    std::size_t owner=noParent;for(std::size_t i=0;i<runtime->nodes().size();++i)if(!runtime->nodes()[i].definition.script.empty()&&runtime->nodes()[i].state.contains("isOpen")){owner=i;break;}
    if(owner==noParent){smokeFailure_="Example has no inspectable door isOpen state";finishSmoke(false);return;}
    auto before=Value(runtime->nodes()[owner].state).display();bool found=clickBinding(owner);
    smokeClicked_=found&&clicks_==1&&before!=Value(runtime->nodes()[owner].state).display()&&selected_==runtime->nodes()[owner].definition.id;
    if(!smokeClicked_){smokeFailure_="Native raycast click did not change the owning door state";finishSmoke(false);return;}
    smokeStage_=2;smokeTime_=Clock::now();return;
  }
  if(smokeStage_==2){
    if(Clock::now()-smokeTime_<std::chrono::milliseconds(750))return;
    std::size_t owner=noParent;for(std::size_t i=0;runtime&&i<runtime->nodes().size();++i)if(!runtime->nodes()[i].definition.script.empty()&&runtime->nodes()[i].state.contains("isOpen")){owner=i;break;}
    if(owner==noParent){smokeFailure_="Door binding disappeared during animation";finishSmoke(false);return;}
    if(smokeDoorCycles_<2){
      bool shouldOpen=smokeDoorCycles_==1;
      if(shouldOpen&&std::abs(runtime->nodes()[owner].transform.rotation[1])>1.5){smokeFailure_="Door close state and hinge visual transform diverged";finishSmoke(false);return;}
      if(!clickBinding(owner)||runtime->nodes()[owner].state.at("isOpen").boolean()!=shouldOpen){smokeFailure_="Repeated native door click did not toggle the state";finishSmoke(false);return;}
      ++smokeDoorCycles_;smokeTime_=Clock::now();return;
    }
    if(std::abs(runtime->nodes()[owner].transform.rotation[1])<10){smokeFailure_="Reopened door did not animate its hinge transform";finishSmoke(false);return;}
    auto before=camera_.eye();auto clickCount=clicks_;SendMessageW(viewport_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,40));SendMessageW(viewport_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(65,47));SendMessageW(viewport_,WM_LBUTTONUP,0,MAKELPARAM(65,47));SendMessageW(viewport_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(40,40));SendMessageW(viewport_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(45,43));SendMessageW(viewport_,WM_RBUTTONUP,0,MAKELPARAM(45,43));SendMessageW(viewport_,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
    smokeDrag_=before!=camera_.eye()&&clicks_==clickCount;if(!smokeDrag_){smokeFailure_="Orbit/pan/zoom route failed or delivered an unwanted interaction";finishSmoke(false);return;}
    render();refreshInspector();RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);auto path=capturePath;if(path.empty()){path=smokePath;path.replace_extension(L".bmp");}if(!capture(path)){smokeFailure_="Could not capture native framebuffer";finishSmoke(false);return;}capturePath.clear();request("link://local/room-b");smokeStage_=3;return;
  }
  if(smokeStage_==3){
    if(navigator_.loading())return;if(!runtime){smokeFailure_="Room transition failed: "+navigator_.error();finishSmoke(false);return;}
    if(!smokeStones_){
      std::size_t soft=noParent,hard=noParent;std::string softOwner,hardOwner;
      for(std::size_t i=0;i<runtime->nodes().size();++i){const auto& n=runtime->nodes()[i];auto owner=n.definition.config.find("owner");if(n.definition.script.empty()||owner==n.definition.config.end()||!std::holds_alternative<std::string>(owner->second.data))continue;const auto& state=runtime->find(owner->second.string()).state;auto strength=state.find("strength");if(strength==state.end())continue;if(strength->second.number()==10){soft=i;softOwner=owner->second.string();}if(strength->second.number()==100){hard=i;hardOwner=owner->second.string();}}
      if(soft==noParent||hard==noParent||!clickBinding(soft)){smokeFailure_="Could not raycast the soft stone";finishSmoke(false);return;}
      bool independent=runtime->find(softOwner).state.at("strength").number()==0&&runtime->find(softOwner).state.at("broken").boolean()&&runtime->find(hardOwner).state.at("strength").number()==100;
      if(!independent||!clickBinding(hard)){smokeFailure_="Soft stone click changed the wrong state or hard stone raycast failed";finishSmoke(false);return;}
      smokeStones_=runtime->find(softOwner).state.at("strength").number()==0&&runtime->find(hardOwner).state.at("strength").number()==90&&!runtime->find(hardOwner).state.at("broken").boolean()&&runtime->nodes()[soft].state.at("hits").number()==1&&runtime->nodes()[hard].state.at("hits").number()==1;
      if(!smokeStones_){smokeFailure_="Native stone clicks did not preserve independent state";finishSmoke(false);return;}
      render();auto stonePath=smokePath;stonePath.replace_extension(L".stones.bmp");capture(stonePath);
    }
    ++switches_;if(switches_<20){request(switches_%2?"link://local/room-a":"link://local/room-b");return;}
    if(counters().activeWorlds!=1){smokeFailure_="Transition test found more than one active world";finishSmoke(false);return;}
    request("link://local/nonexistent-smoke-world");smokeStage_=4;return;
  }
  if(smokeStage_==4){
    if(navigator_.loading())return;smokePreserved_=runtime!=nullptr&&!navigator_.error().empty()&&counters().activeWorlds==1;
    if(!smokePreserved_){smokeFailure_="Failed navigation did not preserve the active world";finishSmoke(false);return;}
    command(Home);smokeStage_=5;return;
  }
  if(smokeStage_==5){smokeHome_=counters().activeWorlds==0&&counters().activeBindings==0;if(!smokeHome_){smokeFailure_="Home did not release runtime resources";finishSmoke(false);return;}request("link://local/room-a");smokeStage_=6;return;}
  if(smokeStage_==6){if(navigator_.loading())return;if(!runtime){smokeFailure_="Reopening after Home failed";finishSmoke(false);return;}command(Stop);smokeStage_=7;return;}
  if(smokeStage_==7){smokeStopped_=counters().activeWorlds==0&&counters().activeBindings==0;if(!smokeStopped_){smokeFailure_="Stop did not release runtime resources";finishSmoke(false);return;}finishSmoke(true);}
}
void Desktop::finishSmoke(bool okay){
  auto escape=[](const std::string& text){std::string out;for(char c:text){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else out+=c;}return out;};
  auto c=counters();if(!smokePath.parent_path().empty())std::filesystem::create_directories(smokePath.parent_path());std::ofstream file(smokePath);file<<"{\n  \"passed\": "<<(okay?"true":"false")<<",\n  \"failure\": \""<<escape(smokeFailure_)<<"\",\n  \"backend\": \"Win32 + WGL/OpenGL 1.1\",\n  \"gl\": \""<<escape(glDescription_)<<"\",\n  \"viewport\": ["<<viewWidth_<<", "<<viewHeight_<<"],\n  \"roomANodes\": "<<smokeNodeCount_<<",\n  \"nativeRaycastClick\": "<<(smokeClicked_?"true":"false")<<",\n  \"nativeDoorRepeatAndTransform\": "<<(smokeDoorCycles_==2?"true":"false")<<",\n  \"nativeStoneIndependence\": "<<(smokeStones_?"true":"false")<<",\n  \"orbitPanZoomWithoutClick\": "<<(smokeDrag_?"true":"false")<<",\n  \"transitions\": "<<switches_<<",\n  \"failureRetainsCurrent\": "<<(smokePreserved_?"true":"false")<<",\n  \"homeReleased\": "<<(smokeHome_?"true":"false")<<",\n  \"stopReleased\": "<<(smokeStopped_?"true":"false")<<",\n  \"activeWorldsAfterHome\": "<<(smokeHome_?0:c.activeWorlds)<<",\n  \"activeBindingsAfterHome\": "<<(smokeHome_?0:c.activeBindings)<<",\n  \"activeWorldsAfterStop\": "<<c.activeWorlds<<",\n  \"activeBindingsAfterStop\": "<<c.activeBindings<<",\n  \"createdWorlds\": "<<c.createdWorlds<<",\n  \"destroyedWorlds\": "<<c.destroyedWorlds<<",\n  \"renderedFrames\": "<<frames_<<",\n  \"smokeElapsedSeconds\": "<<std::chrono::duration<double>(Clock::now()-started_).count()<<",\n  \"persistentWglContexts\": 1,\n  \"worldGpuAllocations\": 0\n}\n";file.close();std::cout<<(okay?"Native smoke PASS\n":"Native smoke FAIL: "+smokeFailure_+"\n");smokePath.clear();exitCode_=okay?0:2;DestroyWindow(window_);
}
bool Desktop::clickImage(bool doubleClick){
  auto* runtime=navigator_.current();if(!runtime||photoNodes_.empty())return false;
  const auto matrices=sceneMatrices(*runtime);const auto forward=camera_.forward(),right=camera_.right(),up=camera_.up();const auto tangent=std::tan(45*pi/360),aspect=double(viewWidth_)/viewHeight_;
  for(const auto index:photoNodes_){
    const auto to=sub(matrices.world[index].point({0,0,0}),camera_.eye());const auto depth=dot(to,forward);if(depth<=.05)continue;
    int x=int((dot(to,right)/(depth*tangent*aspect)+1)*viewWidth_/2),y=int((1-dot(to,up)/(depth*tangent))*viewHeight_/2);
    if(x<0||x>=viewWidth_||y<0||y>=viewHeight_||raycast(*runtime,camera_,x,y,viewWidth_,viewHeight_)!=index)continue;
    SendMessageW(viewport_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(viewport_,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    if(doubleClick)SendMessageW(viewport_,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(x,y));
    return selected_==runtime->nodes()[index].definition.id&&(!doubleClick||photoOpen_);
  }
  return false;
}
void Desktop::gallerySmoke(){
  if(Clock::now()-started_>std::chrono::seconds(75)){galleryFailure_="Gallery verification timed out: "+navigator_.error()+renderError_;finishGallerySmoke(false);return;}
  auto* runtime=navigator_.current();if(navigator_.loading()||textures_.loading())return;
  const auto take=[&](const wchar_t* suffix){auto path=gallerySmokePath;path.replace_extension(suffix);render();refreshInspector();return capture(path);};
  if(galleryStage_==5){if(!take(L".home.bmp")){galleryFailure_="Home capture failed";finishGallerySmoke(false);return;}finishGallerySmoke(true);return;}
  if(!runtime){galleryFailure_="No active corridor: "+navigator_.error()+renderError_;finishGallerySmoke(false);return;}
  if(galleryStage_==0){
    galleryImages_=textures_.imageCount();galleryTexts_=textures_.textCount();galleryBytes_=textures_.bytes();
    if(galleryImages_!=runtime->world().images.size()||galleryImages_==0||photoNodes_.size()!=galleryImages_||galleryTexts_!=0||!runtime->world().walk||runtime->world().portals.size()!=1){galleryFailure_="Corridor requires current photos, walking, one portal, and no decorative text";finishGallerySmoke(false);return;}
    if(!take(L".gallery.bmp")){galleryFailure_="Gallery capture failed";finishGallerySmoke(false);return;}
    // Use a photo's actual viewpoint so this remains stable as the selection changes.
    const auto& firstPhoto=runtime->nodes()[photoNodes_.front()].definition;
    for(std::size_t i=0;i<runtime->world().views.size();++i)if(runtime->world().views[i].name==firstPhoto.name){jumpView(static_cast<int>(i));break;}
    galleryPhotoClicked_=clickImage(true);galleryViewer_=photoOpen_&&viewerTexture_;
    if(!galleryPhotoClicked_||!galleryViewer_){galleryFailure_="Photo raycast or double-click viewer failed";finishGallerySmoke(false);return;}
    take(L".photo.bmp");auto previous=photoIndex_;command(NextPhoto);command(PreviousPhoto);galleryViewer_=photoOpen_&&photoIndex_==previous;command(ClosePhoto);
    for(auto photo:photoNodes_)if(runtime->nodes()[photo].definition.image.starts_with("assets/owner-")){
      for(std::size_t i=0;i<runtime->world().views.size();++i)if(runtime->world().views[i].name==runtime->nodes()[photo].definition.name){jumpView(static_cast<int>(i));take(L".owner.bmp");break;}
      break;
    }
    jumpView(0);SetFocus(viewport_);const auto lookStart=camera_.eye(),lookDirection=camera_.forward();int x=viewWidth_/2,y=viewHeight_/2;
    SendMessageW(viewport_,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(x,y));SendMessageW(viewport_,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(x+32,y+16));SendMessageW(viewport_,WM_RBUTTONUP,0,MAKELPARAM(x+32,y+16));
    galleryLook_=std::sqrt(dot(sub(camera_.eye(),lookStart),sub(camera_.eye(),lookStart)))<1e-8&&dot(sub(camera_.forward(),lookDirection),sub(camera_.forward(),lookDirection))>.001; // Looking must rotate without orbiting the walker.
    jumpView(0);SetFocus(viewport_);SendMessageW(viewport_,WM_KEYDOWN,'A',0);galleryTime_=Clock::now();galleryStage_=1;return;
  }
  if(galleryStage_==1){
    if(Clock::now()-galleryTime_<std::chrono::milliseconds(1300))return;
    SendMessageW(viewport_,WM_KEYUP,'A',0);galleryBounds_=std::abs(camera_.eye()[0]-runtime->world().walk->min[0])<1e-6;
    if(!galleryBounds_||!galleryLook_){galleryFailure_="Walking bounds or anchored mouse look failed";finishGallerySmoke(false);return;}
    jumpView(0);galleryWalkStart_=camera_.eye();const auto& portal=runtime->world().portals.front();galleryPortalAddress_=portal.address;galleryExpectedDistance_=std::abs(portal.position[2]-galleryWalkStart_[2])-portal.size[2]/2;
    SetFocus(viewport_);SendMessageW(viewport_,WM_KEYDOWN,'W',0);galleryTime_=Clock::now();galleryStage_=2;return;
  }
  if(galleryStage_==2){
    if(runtime->world().address!=galleryPortalAddress_){
      const auto& portal=runtime->world().portals.front();
      if(!galleryPortalCaptured_&&std::abs(camera_.eye()[2]-portal.position[2])<4){galleryPortalCaptured_=take(L".portal.bmp");}
      if(Clock::now()-galleryTime_>std::chrono::seconds(35)){galleryFailure_="Walking through the portal did not change the LINK address";finishGallerySmoke(false);}return;
    }
    galleryPortal_=true;galleryWalk_=galleryWalkSteps_>100&&galleryWalkDistance_>=galleryExpectedDistance_-.01;
    galleryText3d_=runtime->world().images.empty()&&runtime->find("hello_world_text").definition.name=="Hello World!"&&std::count_if(runtime->nodes().begin(),runtime->nodes().end(),[](const auto& node){return node.definition.id.starts_with("hello_stroke_")&&node.definition.geometry.type==Geometry::Type::Box&&node.definition.geometry.size[2]>=.2;})>20;
    const auto* hologram=textures_.text("link_hologram_text");galleryHologram_=textures_.textCount()==1&&hologram&&hologram->id&&!hologram->textBackground;
    if(!galleryWalk_||!galleryText3d_||!galleryHologram_||!galleryPortalCaptured_){galleryFailure_="Corridor traversal, extruded greeting, or transparent introduction texture failed";finishGallerySmoke(false);return;}
    take(L".hello.bmp");if(runtime->world().views.size()>2){jumpView(2);take(L".hello-side.bmp");}
    request("link://local/heukryong");galleryStage_=3;return;
  }
  if(galleryStage_==3){command(Reload);galleryStage_=4;return;}
  if(galleryStage_==4){
    galleryReload_=textures_.imageCount()==galleryImages_&&photoNodes_.size()==galleryImages_&&textures_.textCount()==0&&runtime->world().walk.has_value();
    command(Home);galleryReleased_=textures_.count()==0&&textures_.bytes()==0&&textures_.created()==textures_.deleted()&&counters().activeWorlds==0;
    if(!galleryViewer_||!galleryReload_||!galleryReleased_){galleryFailure_="Viewer/reload/resource ownership check failed";finishGallerySmoke(false);return;}
    galleryStage_=5;
  }
}
void Desktop::finishGallerySmoke(bool okay){
  auto escape=[](const std::string& text){std::string out;for(char c:text){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else out+=c;}return out;};
  if(!gallerySmokePath.parent_path().empty())std::filesystem::create_directories(gallerySmokePath.parent_path());
  std::ofstream out(gallerySmokePath);
  out<<"{\n  \"passed\": "<<(okay?"true":"false")<<",\n  \"failure\": \""<<escape(galleryFailure_)<<"\",\n  \"images\": "<<galleryImages_<<",\n  \"textPlaques\": "<<galleryTexts_<<",\n  \"galleryTextureBytes\": "<<galleryBytes_;
  out<<",\n  \"nativePhotoRaycast\": "<<(galleryPhotoClicked_?"true":"false")<<",\n  \"largeViewerAndNavigation\": "<<(galleryViewer_?"true":"false");
  out<<",\n  \"nativeKeyboardWalk\": "<<(galleryWalk_?"true":"false")<<",\n  \"walkBounds\": "<<(galleryBounds_?"true":"false")<<",\n  \"anchoredMouseLook\": "<<(galleryLook_?"true":"false")<<",\n  \"walkSteps\": "<<galleryWalkSteps_<<",\n  \"walkDistance\": "<<galleryWalkDistance_;
  out<<",\n  \"portalTransition\": "<<(galleryPortal_?"true":"false")<<",\n  \"portalAddress\": \""<<escape(galleryPortalAddress_)<<"\",\n  \"extrudedHelloWorld\": "<<(galleryText3d_?"true":"false");
  out<<",\n  \"transparentLinkIntroduction\": "<<(galleryHologram_?"true":"false");
  out<<",\n  \"reload\": "<<(galleryReload_?"true":"false")<<",\n  \"releasedAfterHome\": "<<(galleryReleased_?"true":"false")<<",\n  \"createdTextures\": "<<textures_.created()<<",\n  \"deletedTextures\": "<<textures_.deleted()<<",\n  \"liveTextures\": "<<textures_.count()<<",\n  \"liveTextureBytes\": "<<textures_.bytes();
  out<<",\n  \"maxTextureStepMs\": "<<maxTextureStepMs_<<",\n  \"elapsedSeconds\": "<<std::chrono::duration<double>(Clock::now()-started_).count()<<",\n  \"gl\": \""<<escape(glDescription_)<<"\"\n}\n";
  out.close();std::cout<<(okay?"Gallery smoke PASS\n":"Gallery smoke FAIL: "+galleryFailure_+"\n");gallerySmokePath.clear();exitCode_=okay?0:2;DestroyWindow(window_);
}
bool Desktop::homeLayoutFits()const{
  if(navigator_.current()||!windowText(address_).empty())return false;
  RECT previous{};bool first=true;
  for(auto id:{Gallery,RoomA,RoomB}){HWND child=controls_.at(id);if(!IsWindowVisible(child))return false;RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&rect),2);if(rect.left<0||rect.right>contentWidth_||rect.top<addressPill_.bottom||rect.bottom>height_-px(44))return false;RECT overlap{};if(!first&&IntersectRect(&overlap,&previous,&rect))return false;previous=rect;first=false;}
  RECT edit{};GetWindowRect(address_,&edit);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&edit),2);return edit.left>addressPill_.left&&edit.right<addressPill_.right&&edit.top>=addressPill_.top&&edit.bottom<=addressPill_.bottom;
}
void Desktop::shellSmoke(){
  if(Clock::now()-started_>std::chrono::seconds(25)){shellFailureMessage_="Shell smoke timed out";finishShellSmoke(false);return;}
  auto save=[&](const wchar_t* extension){auto path=shellSmokePath;path.replace_extension(extension);return capture(path);};
  auto fail=[&](const std::string& message){shellFailureMessage_=message;finishShellSmoke(false);};
  if(shellStage_==0){
    if(Clock::now()-started_<std::chrono::milliseconds(350))return;
    shellHome_=homeLayoutFits()&&!toolsOpen_&&!IsWindowVisible(nodes_)&&!IsWindowVisible(inspector_)&&!IsWindowVisible(logs_)&&!IsWindowVisible(status_);
    if(!shellHome_){fail("Home must start with an empty address, three shortcuts, and hidden developer tools");return;}save(L".home.bmp");
    SetWindowTextW(address_,L"link://local/room-a");SetFocus(controls_[Menu]);BYTE keys[256]{};GetKeyboardState(keys);BYTE simulated[256]{};std::copy_n(keys,256,simulated);simulated[VK_CONTROL]|=0x80;SetKeyboardState(simulated);SendMessageW(window_,WM_KEYDOWN,'L',0);SetKeyboardState(keys);DWORD begin=0,end=0;SendMessageW(address_,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));shellFocus_=GetFocus()==address_&&begin==0&&end==windowText(address_).size();SetWindowTextW(address_,L"");
    int width=viewWidth_;SendMessageW(window_,WM_KEYDOWN,VK_F12,0);shellTools_=toolsOpen_&&IsWindowVisible(nodes_)&&viewWidth_<width;SendMessageW(window_,WM_KEYDOWN,VK_F12,0);shellTools_=shellTools_&&!toolsOpen_&&!IsWindowVisible(nodes_)&&viewWidth_==width;
    RECT original{};GetWindowRect(window_,&original);SetWindowPos(window_,nullptr,0,0,900,650,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);shellResize_=homeLayoutFits();save(L".small.bmp");
    UINT originalDpi=dpi_;RECT enlarged{original.left,original.top,original.left+1440,original.top+900};SendMessageW(window_,WM_DPICHANGED,MAKELPARAM(144,144),reinterpret_cast<LPARAM>(&enlarged));shellDpi_=dpi_==144&&homeLayoutFits();save(L".dpi150.bmp");SendMessageW(window_,WM_DPICHANGED,MAKELPARAM(originalDpi,originalDpi),reinterpret_cast<LPARAM>(&original));
    if(!shellFocus_||!shellTools_||!shellResize_||!shellDpi_){fail("Address focus, F12, resize, or synthetic 150-percent layout failed");return;}
    SendMessageW(controls_[RoomA],BM_CLICK,0,0);shellStage_=1;return;
  }
  if(shellStage_==1){
    if(navigator_.loading())return;if(!navigator_.current()){fail("Room A shortcut did not open a world");return;}
    save(L".world.bmp");int width=viewWidth_;SendMessageW(window_,WM_KEYDOWN,VK_F12,0);save(L".tools.bmp");SendMessageW(window_,WM_KEYDOWN,VK_F12,0);shellTools_=shellTools_&&width==viewWidth_&&!toolsOpen_;
    shellCreated_=counters().createdWorlds;SendMessageW(window_,WM_KEYDOWN,VK_F5,0);shellStage_=2;return;
  }
  if(shellStage_==2){
    if(navigator_.loading())return;shellReload_=navigator_.current()&&counters().createdWorlds>shellCreated_;if(!shellReload_){fail("F5 did not reload the current world");return;}
    request("link://local/room-b");SendMessageW(window_,WM_KEYDOWN,VK_ESCAPE,0);shellCancel_=!navigator_.loading()&&navigator_.current()!=nullptr;
    SetWindowTextW(address_,L"link://local/missing-shell-smoke");SetFocus(address_);SendMessageW(window_,WM_KEYDOWN,VK_RETURN,0);shellStage_=3;return;
  }
  if(shellStage_==3){
    if(navigator_.loading())return;shellFailure_=navigator_.current()&&!navigator_.error().empty()&&IsWindowVisible(status_)&&IsWindowVisible(controls_[ErrorDetails]);save(L".error.bmp");
    SendMessageW(controls_[ErrorDetails],BM_CLICK,0,0);shellFailure_=shellFailure_&&toolsOpen_&&GetFocus()==logs_;if(toolsOpen_)SendMessageW(window_,WM_KEYDOWN,VK_F12,0);
    if(!shellFailure_||!shellCancel_){fail("Failed navigation or cancellation was not recoverable and visible");return;}SendMessageW(controls_[Home],BM_CLICK,0,0);shellStage_=4;return;
  }
  if(shellStage_==4){SendMessageW(window_,WM_KEYDOWN,VK_F5,0);if(!homeLayoutFits()||navigator_.loading()||counters().activeWorlds||counters().activeBindings){fail("Home or its disabled reload retained a runtime");return;}finishShellSmoke(true);}
}
void Desktop::finishShellSmoke(bool okay){
  if(!shellSmokePath.parent_path().empty())std::filesystem::create_directories(shellSmokePath.parent_path());std::ofstream out(shellSmokePath);out<<"{\n  \"passed\": "<<(okay?"true":"false")<<",\n  \"failure\": \""<<shellFailureMessage_<<"\",\n  \"emptyHomeThreeShortcuts\": "<<(shellHome_?"true":"false")<<",\n  \"ctrlLSelectsAddress\": "<<(shellFocus_?"true":"false")<<",\n  \"developerToolsToggleRestoresViewport\": "<<(shellTools_?"true":"false")<<",\n  \"smallWindowBounds\": "<<(shellResize_?"true":"false")<<",\n  \"syntheticDpi150Bounds\": "<<(shellDpi_?"true":"false")<<",\n  \"f5Reload\": "<<(shellReload_?"true":"false")<<",\n  \"escapeCancels\": "<<(shellCancel_?"true":"false")<<",\n  \"failureVisibleAndDetailed\": "<<(shellFailure_?"true":"false")<<",\n  \"activeWorldsAfterHome\": "<<counters().activeWorlds<<",\n  \"liveTexturesAfterHome\": "<<textures_.count()<<"\n}\n";out.close();shellSmokePath.clear();exitCode_=okay?0:2;DestroyWindow(window_);
}
LRESULT Desktop::message(UINT message,WPARAM wParam,LPARAM lParam){
  switch(message){
    case WM_CREATE:createControls();layout();SetTimer(window_,1,16,nullptr);return 0;
    case WM_SIZE:minimized_=wParam==SIZE_MINIMIZED;if(!minimized_&&viewport_)layout();return 0;
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={px(720),px(560)};return 0;}
    case WM_DPICHANGED:{dpi_=HIWORD(wParam);auto* rect=reinterpret_cast<RECT*>(lParam);SetWindowPos(window_,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);createFonts();layout();return 0;}
    case WM_COMMAND:if(LOWORD(wParam)==Address&&HIWORD(wParam)==EN_CHANGE){InvalidateRect(address_,nullptr,TRUE);return 0;}if(LOWORD(wParam)==Address&&(HIWORD(wParam)==EN_SETFOCUS||HIWORD(wParam)==EN_KILLFOCUS)){InvalidateRect(window_,&addressPill_,FALSE);return 0;}if(HIWORD(wParam)==BN_CLICKED||(LOWORD(wParam)==NodeList&&HIWORD(wParam)==LBN_SELCHANGE))command(LOWORD(wParam));return 0;
    case WM_KEYDOWN:if(shortcut(wParam,(GetKeyState(VK_CONTROL)&0x8000)!=0))return 0;break;
    case WM_DRAWITEM:if(wParam){drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));return TRUE;}break;
    case WM_TIMER:if(wParam==1)frame();return 0;
    case WM_PAINT:paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wParam);auto child=reinterpret_cast<HWND>(lParam);if(child==controls_[PhotoTitle]){SetTextColor(dc,RGB(225,233,239));SetBkColor(dc,RGB(14,17,18));return reinterpret_cast<LRESULT>(photoBrush_);}SetTextColor(dc,child==status_&&shellError_?RGB(166,102,46):RGB(94,112,132));SetBkColor(dc,RGB(255,255,255));return reinterpret_cast<LRESULT>(background_);}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wParam);SetTextColor(dc,RGB(51,65,85));SetBkColor(dc,RGB(255,255,255));return reinterpret_cast<LRESULT>(background_);}
    case WM_CLOSE:DestroyWindow(window_);return 0;
    case WM_DESTROY:KillTimer(window_,1);clearWorldGraphics();navigator_.stop();destroyGl();PostQuitMessage(exitCode_);return 0;
  }return DefWindowProcW(window_,message,wParam,lParam);
}
LRESULT Desktop::viewMessage(UINT message,WPARAM wParam,LPARAM lParam){
  int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
  switch(message){
    case WM_GETDLGCODE:if(walking())return DLGC_WANTARROWS|DLGC_WANTCHARS;break;
    case WM_KEYDOWN:case WM_KEYUP:if(walking()&&movementKey(wParam)){if(!photoOpen_)walkKeys_[static_cast<std::size_t>(wParam)]=message==WM_KEYDOWN;return 0;}break;
    case WM_KILLFOCUS:walkKeys_.fill(false);break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(viewport_,&ps);EndPaint(viewport_,&ps);render();return 0;}
    case WM_LBUTTONDBLCLK:if(!photoOpen_){click(x,y);auto* runtime=navigator_.current();if(runtime&&!selected_.empty()&&!runtime->find(selected_).definition.image.empty())openPhoto(photoIndex_);}return 0;
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:if(photoOpen_)return 0;SetFocus(viewport_);SetCapture(viewport_);dragButton_=message==WM_LBUTTONDOWN?1:2;lastMouse_=downMouse_={x,y};dragged_=false;return 0;
    case WM_MOUSEMOVE:if(dragButton_){int dx=x-lastMouse_.x,dy=y-lastMouse_.y;if(std::abs(x-downMouse_.x)+std::abs(y-downMouse_.y)>3)dragged_=true;if(dragged_){if(walking())lookWalk(dx,dy);else if(dragButton_==1){camera_.yaw-=dx*.008;camera_.pitch=std::clamp(camera_.pitch+dy*.008,-1.48,1.48);}else{double scale=camera_.distance*.0017;camera_.target=add(camera_.target,add(mul(camera_.right(),-dx*scale),mul(camera_.up(),dy*scale)));}}lastMouse_={x,y};return 0;}break;
    case WM_LBUTTONUP:case WM_RBUTTONUP:{bool interact=dragButton_==1&&message==WM_LBUTTONUP&&!dragged_;dragButton_=0;ReleaseCapture();if(interact)click(x,y);return 0;}
    case WM_CAPTURECHANGED:dragButton_=0;return 0;
    case WM_MOUSEWHEEL:if(!photoOpen_&&!walking())camera_.distance=std::clamp(camera_.distance*std::exp(-GET_WHEEL_DELTA_WPARAM(wParam)*.001),.3,220.);return 0;
    case WM_DESTROY:destroyGl();return 0;
  }return DefWindowProcW(viewport_,message,wParam,lParam);
}
LRESULT CALLBACK Desktop::windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
  auto* app=reinterpret_cast<Desktop*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){app=static_cast<Desktop*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);app->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
  if(!app)return DefWindowProcW(window,message,wParam,lParam);
  try{return app->message(message,wParam,lParam);}catch(const std::exception& e){app->log(true,e.what());if(message==WM_CREATE)return -1;return 0;}
}
LRESULT CALLBACK Desktop::viewProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
  auto* app=reinterpret_cast<Desktop*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){app=static_cast<Desktop*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);app->viewport_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
  if(!app)return DefWindowProcW(window,message,wParam,lParam);try{return app->viewMessage(message,wParam,lParam);}catch(const std::exception& e){app->log(true,e.what());return 0;}
}
int Desktop::run(int show){
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSEXW frame{};frame.cbSize=sizeof(frame);frame.hInstance=instance;frame.lpfnWndProc=windowProc;frame.hCursor=LoadCursorW(nullptr,IDC_ARROW);frame.hIcon=LoadIconW(nullptr,IDI_APPLICATION);frame.lpszClassName=L"LINK.NativeShell";frame.style=CS_HREDRAW|CS_VREDRAW;RegisterClassExW(&frame);
  WNDCLASSEXW view{};view.cbSize=sizeof(view);view.hInstance=instance;view.lpfnWndProc=viewProc;view.hCursor=LoadCursorW(nullptr,IDC_CROSS);view.lpszClassName=L"LINK.WorldViewport";view.style=CS_OWNDC|CS_DBLCLKS;RegisterClassExW(&view);
  HWND main=CreateWindowExW(0,frame.lpszClassName,L"LINK Browser — Linked Interactive Network",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,820,nullptr,nullptr,instance,this);
  if(!main)throw std::runtime_error("Native LINK window initialization failed");ShowWindow(main,show);UpdateWindow(main);SetFocus(address_);if(!initialAddress.empty())request(initialAddress);else if(!gallerySmokePath.empty())request("link://local/heukryong");else if(!smokePath.empty())request("link://local/room-a");
  MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
    if(message.message==WM_KEYDOWN&&shortcut(message.wParam,(GetKeyState(VK_CONTROL)&0x8000)!=0))continue;
    if((message.message==WM_KEYDOWN||message.message==WM_KEYUP)&&GetFocus()==viewport_&&walking()&&movementKey(message.wParam)){TranslateMessage(&message);DispatchMessageW(&message);continue;}
    if(!IsDialogMessageW(main,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  return static_cast<int>(message.wParam);
}
} // namespace
int runDesktop(int argc,wchar_t** argv){
  std::vector<wchar_t> module(32768);GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));std::filesystem::path root=std::filesystem::path(module.data()).parent_path()/L"worlds";
  for(int i=1;i<argc;++i)if(std::wstring(argv[i])==L"--worlds"&&i+1<argc)root=std::filesystem::absolute(argv[++i]);
  try{
    Desktop app(root);
    for(int i=1;i<argc;++i){
      std::wstring arg=argv[i];
      if(arg==L"--worlds"){++i;continue;}
      if(arg==L"--open"&&i+1<argc)app.initialAddress=utf8(argv[++i]);
      else if(arg==L"--capture"&&i+1<argc)app.capturePath=argv[++i];
      else if(arg==L"--smoke-test"&&i+1<argc)app.smokePath=argv[++i];
      else if(arg==L"--gallery-smoke"&&i+1<argc)app.gallerySmokePath=argv[++i];
      else if(arg==L"--shell-smoke"&&i+1<argc)app.shellSmokePath=argv[++i];
      else if(arg==L"--help"){
        std::cout<<"LINK Browser — Linked Interactive Network\nEvery World. One Link.\n\nLINK [--worlds ABSOLUTE_DIRECTORY] [--open link://local/room-a] [--capture screenshot.bmp] [--smoke-test report.json] [--gallery-smoke report.json] [--shell-smoke report.json]\n";
        return 0;
      }else{std::cerr<<"Unknown or incomplete argument: "<<utf8(arg)<<'\n';return 1;}
    }
    return app.run(SW_SHOWNORMAL);
  }catch(const std::exception& error){std::cerr<<"LINK startup failed: "<<error.what()<<'\n';return 1;}
}
} // namespace linkworld

int wmain(int argc,wchar_t** argv){return linkworld::runDesktop(argc,argv);}
