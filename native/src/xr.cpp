// Copyright 2026 Song Youngwoon
// SPDX-License-Identifier: Apache-2.0
#include "link/xr.hpp"
#include <GL/gl.h>
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include <openxr/openxr.h>
#include <unknwn.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace linkworld {
namespace {
constexpr GLenum framebufferTarget=0x8D40,readFramebuffer=0x8CA8,drawFramebuffer=0x8CA9;
constexpr GLenum renderbufferTarget=0x8D41,colorAttachment=0x8CE0,depthAttachment=0x8D00;
constexpr GLenum depthComponent24=0x81A6,framebufferComplete=0x8CD5,framebufferSrgb=0x8DB9;
constexpr GLenum contextProfileMask=0x9126;
constexpr GLint compatibilityProfileBit=0x00000002;
constexpr std::int64_t rgba8=0x8058;
constexpr XrDuration imageWaitTimeout=100000000; // 100 ms; retry the SAME acquired image next frame.
constexpr std::uint32_t maxEnumeration=4096;

using GenObjects=void (APIENTRY*)(GLsizei,GLuint*);
using DeleteObjects=void (APIENTRY*)(GLsizei,const GLuint*);
using BindObject=void (APIENTRY*)(GLenum,GLuint);
using RenderbufferStorage=void (APIENTRY*)(GLenum,GLenum,GLsizei,GLsizei);
using FramebufferRenderbuffer=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint);
using FramebufferTexture2D=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);
using CheckFramebufferStatus=GLenum (APIENTRY*)(GLenum);
using BlitFramebuffer=void (APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);

template<class Function> Function loadGl(const char* name) {
  PROC address=wglGetProcAddress(name);
  const auto numeric=reinterpret_cast<std::uintptr_t>(address);
  if(!address||numeric<=3||numeric==std::numeric_limits<std::uintptr_t>::max()) {
    const HMODULE library=GetModuleHandleW(L"opengl32.dll");
    address=library?GetProcAddress(library,name):nullptr;
  }
  if(!address)throw std::runtime_error(std::string("OpenXR: OpenGL function is unavailable: ")+name+". Use a modern compatibility OpenGL driver.");
  Function function=nullptr;
  static_assert(sizeof(function)==sizeof(address));
  std::memcpy(&function,&address,sizeof(function));
  return function;
}

template<class Structure> Structure xrStruct(XrStructureType type) {
  Structure value{};value.type=type;return value;
}
XrPosef identityPose() { XrPosef pose{};pose.orientation.w=1;return pose; }
std::string versionText(XrVersion version) {
  return std::to_string(XR_VERSION_MAJOR(version))+"."+std::to_string(XR_VERSION_MINOR(version));
}
OpenXrSession::Pose copyPose(const XrPosef& source,XrSpaceLocationFlags flags) {
  OpenXrSession::Pose pose;
  pose.position={source.position.x,source.position.y,source.position.z};
  pose.orientation={source.orientation.x,source.orientation.y,source.orientation.z,source.orientation.w};
  bool finite=true;
  for(double value:pose.position)finite=finite&&std::isfinite(value);
  const auto& q=pose.orientation;
  const double norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
  finite=finite&&std::isfinite(norm)&&norm>1e-12;
  pose.positionValid=finite&&(flags&XR_SPACE_LOCATION_POSITION_VALID_BIT)!=0;
  pose.orientationValid=finite&&(flags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)!=0;
  pose.positionTracked=pose.positionValid&&(flags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)!=0;
  pose.orientationTracked=pose.orientationValid&&(flags&XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)!=0;
  return pose;
}
XrSpaceLocationFlags locationFlags(XrViewStateFlags flags) {
  XrSpaceLocationFlags result=0;
  if(flags&XR_VIEW_STATE_POSITION_VALID_BIT)result|=XR_SPACE_LOCATION_POSITION_VALID_BIT;
  if(flags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)result|=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if(flags&XR_VIEW_STATE_POSITION_TRACKED_BIT)result|=XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
  if(flags&XR_VIEW_STATE_ORIENTATION_TRACKED_BIT)result|=XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
  return result;
}
} // namespace

struct OpenXrSession::Impl {
  struct Gl {
    GenObjects genFramebuffers=nullptr,genRenderbuffers=nullptr;
    DeleteObjects deleteFramebuffers=nullptr,deleteRenderbuffers=nullptr;
    BindObject bindFramebuffer=nullptr,bindRenderbuffer=nullptr;
    RenderbufferStorage renderbufferStorage=nullptr;
    FramebufferRenderbuffer framebufferRenderbuffer=nullptr;
    FramebufferTexture2D framebufferTexture2D=nullptr;
    CheckFramebufferStatus checkFramebufferStatus=nullptr;
    BlitFramebuffer blitFramebuffer=nullptr;
    void load() {
      genFramebuffers=loadGl<GenObjects>("glGenFramebuffers");
      genRenderbuffers=loadGl<GenObjects>("glGenRenderbuffers");
      deleteFramebuffers=loadGl<DeleteObjects>("glDeleteFramebuffers");
      deleteRenderbuffers=loadGl<DeleteObjects>("glDeleteRenderbuffers");
      bindFramebuffer=loadGl<BindObject>("glBindFramebuffer");
      bindRenderbuffer=loadGl<BindObject>("glBindRenderbuffer");
      renderbufferStorage=loadGl<RenderbufferStorage>("glRenderbufferStorage");
      framebufferRenderbuffer=loadGl<FramebufferRenderbuffer>("glFramebufferRenderbuffer");
      framebufferTexture2D=loadGl<FramebufferTexture2D>("glFramebufferTexture2D");
      checkFramebufferStatus=loadGl<CheckFramebufferStatus>("glCheckFramebufferStatus");
      blitFramebuffer=loadGl<BlitFramebuffer>("glBlitFramebuffer");
    }
  } gl;
  struct EyeChain {
    XrSwapchain handle=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    GLuint framebuffer=0,depth=0;
    std::uint32_t index=0;
    int width=0,height=0;
    bool acquired=false,waited=false;
  };
  HDC dc=nullptr;HGLRC context=nullptr;
  std::thread::id owner;
  XrInstance instance=XR_NULL_HANDLE;
  XrSystemId system=XR_NULL_SYSTEM_ID;
  XrSession session=XR_NULL_HANDLE;
  XrSpace appSpace=XR_NULL_HANDLE,viewSpace=XR_NULL_HANDLE;
  XrActionSet actionSet=XR_NULL_HANDLE;
  XrAction aimAction=XR_NULL_HANDLE,selectAction=XR_NULL_HANDLE,clickAction=XR_NULL_HANDLE;
  XrAction stickAction=XR_NULL_HANDLE,exitAction=XR_NULL_HANDLE,recenterAction=XR_NULL_HANDLE,hapticAction=XR_NULL_HANDLE;
  std::array<XrPath,2> handPaths{XR_NULL_PATH,XR_NULL_PATH};
  std::array<XrSpace,2> aimSpaces{XR_NULL_HANDLE,XR_NULL_HANDLE};
  std::array<bool,2> previousSelect{false,false},previousAnalogSelect{false,false};
  std::array<bool,2> previousExit{false,false},previousRecenter{false,false};
  std::array<bool,2> selectArmed{false,false},clickArmed{false,false};
  std::array<bool,2> exitArmed{false,false},recenterArmed{false,false};
  std::array<EyeChain,2> chains;
  std::array<XrView,2> views{};
  XrSessionState state=XR_SESSION_STATE_UNKNOWN;
  XrEnvironmentBlendMode blendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  bool runningFlag=false,exitFlag=false,floorLevel=false,spaceChangePending=false,legacyApi=false;
  XrTime lastDisplayTime=0,spaceChangeTime=0;
  std::string name;
  Frame frame;

  void resetInput() noexcept {
    previousSelect={};previousAnalogSelect={};previousExit={};previousRecenter={};
    selectArmed={};clickArmed={};exitArmed={};recenterArmed={};
  }
  void requireOwner() const {
    if(owner!=std::this_thread::get_id())throw std::runtime_error("OpenXR: use the session on its owning WGL thread.");
  }
  void currentContext() const {
    requireOwner();
    if(wglGetCurrentContext()!=context||wglGetCurrentDC()!=dc) {
      if(!context||!dc||!wglMakeCurrent(dc,context))throw std::runtime_error("OpenXR: could not make the bound WGL context current.");
    }
  }
  void check(XrResult result,const char* operation) {
    if(result==XR_SESSION_LOSS_PENDING||result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST)exitFlag=true;
    if(XR_SUCCEEDED(result))return;
    char symbol[XR_MAX_RESULT_STRING_SIZE]{};
    if(!instance||XR_FAILED(xrResultToString(instance,result,symbol)))std::snprintf(symbol,sizeof(symbol),"result %d",static_cast<int>(result));
    std::string message=std::string("OpenXR: ")+operation+" failed ("+symbol+"). ";
    if(result==XR_ERROR_RUNTIME_UNAVAILABLE||result==XR_ERROR_INITIALIZATION_FAILED)
      message+="Open Meta Quest Link and set its OpenXR runtime as active. Connect the headset and enter Quest Link first.";
    else if(result==XR_ERROR_FORM_FACTOR_UNAVAILABLE||result==XR_ERROR_FORM_FACTOR_UNSUPPORTED)
      message+="No ready VR headset. Connect Quest 3, enter Quest Link, and try VR again.";
    else if(result==XR_ERROR_GRAPHICS_DEVICE_INVALID)
      message+="OpenGL and the OpenXR runtime must use the same GPU. Check the Windows per-app GPU selection.";
    else if(result==XR_ERROR_EXTENSION_NOT_PRESENT)
      message+="This runtime must support XR_KHR_opengl_enable for LINK's OpenGL renderer.";
    else if(result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST)
      message+="The runtime or headset connection was lost. Reconnect Quest Link and re-enter VR.";
    else message+="VR has stopped safely; check the headset and active OpenXR runtime.";
    throw std::runtime_error(message);
  }
  XrPath path(const char* text) {
    XrPath value=XR_NULL_PATH;check(xrStringToPath(instance,text,&value),"xrStringToPath");return value;
  }
  void createAction(XrAction& action,const char* actionName,const char* localized,XrActionType type) {
    auto info=xrStruct<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
    std::snprintf(info.actionName,sizeof(info.actionName),"%s",actionName);
    std::snprintf(info.localizedActionName,sizeof(info.localizedActionName),"%s",localized);
    info.actionType=type;info.countSubactionPaths=2;info.subactionPaths=handPaths.data();
    check(xrCreateAction(actionSet,&info,&action),"xrCreateAction");
  }
  void suggest(const char* profile,const std::vector<std::pair<XrAction,const char*>>& bindings) {
    std::vector<XrActionSuggestedBinding> suggestions;
    suggestions.reserve(bindings.size());
    for(const auto& binding:bindings)suggestions.push_back({binding.first,path(binding.second)});
    auto info=xrStruct<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
    info.interactionProfile=path(profile);info.countSuggestedBindings=static_cast<std::uint32_t>(suggestions.size());info.suggestedBindings=suggestions.data();
    XrResult result=xrSuggestInteractionProfileBindings(instance,&info);
    // A runtime need not recognize every vendor's interaction profile.
    if(result!=XR_ERROR_PATH_UNSUPPORTED&&result!=XR_ERROR_PATH_INVALID)check(result,"xrSuggestInteractionProfileBindings");
  }
  void createActions();
  void createSpaces();
  void createSwapchains();
  void initialize(HDC,HGLRC);
  void pollEvents();
  void syncInput();
  Frame beginFrame();
  bool acquireEye(std::size_t);
  void finishFrame(bool submit,bool throwErrors);
  void cancelWithoutGl() noexcept;
  void cleanup() noexcept;
};

void OpenXrSession::Impl::createActions() {
  handPaths={path("/user/hand/left"),path("/user/hand/right")};
  auto setInfo=xrStruct<XrActionSetCreateInfo>(XR_TYPE_ACTION_SET_CREATE_INFO);
  std::snprintf(setInfo.actionSetName,sizeof(setInfo.actionSetName),"link_world");
  std::snprintf(setInfo.localizedActionSetName,sizeof(setInfo.localizedActionSetName),"LINK World");
  check(xrCreateActionSet(instance,&setInfo,&actionSet),"xrCreateActionSet");
  createAction(aimAction,"aim","Controller aim",XR_ACTION_TYPE_POSE_INPUT);
  createAction(selectAction,"select","Select trigger",XR_ACTION_TYPE_FLOAT_INPUT);
  createAction(clickAction,"select_click","Select click",XR_ACTION_TYPE_BOOLEAN_INPUT);
  createAction(stickAction,"thumbstick","Move and turn",XR_ACTION_TYPE_VECTOR2F_INPUT);
  createAction(exitAction,"exit_vr","Leave VR",XR_ACTION_TYPE_BOOLEAN_INPUT);
  createAction(recenterAction,"recenter","Recenter world",XR_ACTION_TYPE_BOOLEAN_INPUT);
  createAction(hapticAction,"haptic","Selection feedback",XR_ACTION_TYPE_VIBRATION_OUTPUT);
  const std::vector<std::pair<XrAction,const char*>> touch{
    {aimAction,"/user/hand/left/input/aim/pose"},{aimAction,"/user/hand/right/input/aim/pose"},
    {selectAction,"/user/hand/left/input/trigger/value"},{selectAction,"/user/hand/right/input/trigger/value"},
    {stickAction,"/user/hand/left/input/thumbstick"},{stickAction,"/user/hand/right/input/thumbstick"},
    {exitAction,"/user/hand/right/input/b/click"},
    {recenterAction,"/user/hand/left/input/x/click"},{recenterAction,"/user/hand/right/input/a/click"},
    {hapticAction,"/user/hand/left/output/haptic"},{hapticAction,"/user/hand/right/output/haptic"}
  };
  suggest("/interaction_profiles/oculus/touch_controller",touch);
  auto indexBindings=touch;
  for(auto& binding:indexBindings)if(std::strcmp(binding.second,"/user/hand/left/input/x/click")==0)binding.second="/user/hand/left/input/a/click";
  suggest("/interaction_profiles/valve/index_controller",indexBindings);
  suggest("/interaction_profiles/khr/simple_controller",{
    {aimAction,"/user/hand/left/input/aim/pose"},{aimAction,"/user/hand/right/input/aim/pose"},
    {clickAction,"/user/hand/left/input/select/click"},{clickAction,"/user/hand/right/input/select/click"},
    {hapticAction,"/user/hand/left/output/haptic"},{hapticAction,"/user/hand/right/output/haptic"}
  });
  auto attach=xrStruct<XrSessionActionSetsAttachInfo>(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);
  attach.countActionSets=1;attach.actionSets=&actionSet;
  check(xrAttachSessionActionSets(session,&attach),"xrAttachSessionActionSets");
  for(std::size_t hand=0;hand<2;++hand) {
    auto info=xrStruct<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
    info.action=aimAction;info.subactionPath=handPaths[hand];info.poseInActionSpace=identityPose();
    check(xrCreateActionSpace(session,&info,&aimSpaces[hand]),"xrCreateActionSpace");
  }
}

void OpenXrSession::Impl::createSpaces() {
  std::uint32_t count=0;check(xrEnumerateReferenceSpaces(session,0,&count,nullptr),"xrEnumerateReferenceSpaces");
  if(count>maxEnumeration)throw std::runtime_error("OpenXR: invalid reference-space count.");
  std::vector<XrReferenceSpaceType> types(count);
  check(xrEnumerateReferenceSpaces(session,count,&count,types.data()),"xrEnumerateReferenceSpaces");
  auto info=xrStruct<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
  info.poseInReferenceSpace=identityPose();
  if(!legacyApi&&std::find(types.begin(),types.end(),XR_REFERENCE_SPACE_TYPE_STAGE)!=types.end()) {
    info.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
    XrResult result=xrCreateReferenceSpace(session,&info,&appSpace);
    if(XR_SUCCEEDED(result))floorLevel=true;
    else if(result!=XR_ERROR_REFERENCE_SPACE_UNSUPPORTED)check(result,"xrCreateReferenceSpace(STAGE)");
  }
  if(!appSpace) {
    info.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    check(xrCreateReferenceSpace(session,&info,&appSpace),"xrCreateReferenceSpace(LOCAL)");
    floorLevel=false;
  }
  info.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
  check(xrCreateReferenceSpace(session,&info,&viewSpace),"xrCreateReferenceSpace(VIEW)");
}
void OpenXrSession::Impl::initialize(HDC device,HGLRC renderingContext) {
  owner=std::this_thread::get_id();dc=device;context=renderingContext;
  if(!dc||!context)throw std::runtime_error("OpenXR: a valid HDC and HGLRC are required.");
  currentContext();
  std::uint32_t count=0;
  check(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"xrEnumerateInstanceExtensionProperties");
  if(count>maxEnumeration)throw std::runtime_error("OpenXR: invalid extension count.");
  std::vector<XrExtensionProperties> extensions(count);
  for(auto& extension:extensions)extension=xrStruct<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES);
  check(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,extensions.data()),"xrEnumerateInstanceExtensionProperties");
  const bool glExtension=std::any_of(extensions.begin(),extensions.end(),[](const auto& extension) {
    return std::strcmp(extension.extensionName,XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)==0;
  });
  if(!glExtension)throw std::runtime_error("OpenXR: the active runtime lacks XR_KHR_opengl_enable. Select a desktop OpenGL-capable OpenXR runtime in Meta Quest Link.");
  const char* enabledExtension=XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
  auto info=xrStruct<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
  std::snprintf(info.applicationInfo.applicationName,sizeof(info.applicationInfo.applicationName),"LINK Browser");
  std::snprintf(info.applicationInfo.engineName,sizeof(info.applicationInfo.engineName),"LINK native");
  info.applicationInfo.applicationVersion=1;info.applicationInfo.engineVersion=1;
  info.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;
  info.enabledExtensionCount=1;info.enabledExtensionNames=&enabledExtension;
  XrResult result=xrCreateInstance(&info,&instance);
  if(result==XR_ERROR_API_VERSION_UNSUPPORTED) {
    instance=XR_NULL_HANDLE;legacyApi=true;
    info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    result=xrCreateInstance(&info,&instance);
  }
  check(result,"xrCreateInstance");
  auto properties=xrStruct<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
  check(xrGetInstanceProperties(instance,&properties),"xrGetInstanceProperties");
  name=std::string(properties.runtimeName)+" "+versionText(properties.runtimeVersion)+"."+std::to_string(XR_VERSION_PATCH(properties.runtimeVersion));
  name+=" / OpenXR API "+versionText(info.applicationInfo.apiVersion)+"."+std::to_string(XR_VERSION_PATCH(info.applicationInfo.apiVersion));
  if(legacyApi)name+=" (LOCAL compatibility)";
  auto systemInfo=xrStruct<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
  systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  check(xrGetSystem(instance,&systemInfo,&system),"xrGetSystem(HMD)");
  PFN_xrVoidFunction function=nullptr;
  check(xrGetInstanceProcAddr(instance,"xrGetOpenGLGraphicsRequirementsKHR",&function),"xrGetInstanceProcAddr(OpenGL requirements)");
  PFN_xrGetOpenGLGraphicsRequirementsKHR requirementsFunction=nullptr;
  static_assert(sizeof(function)==sizeof(requirementsFunction));
  std::memcpy(&requirementsFunction,&function,sizeof(function));
  if(!requirementsFunction)throw std::runtime_error("OpenXR: xrGetOpenGLGraphicsRequirementsKHR is unavailable.");
  auto requirements=xrStruct<XrGraphicsRequirementsOpenGLKHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR);
  check(requirementsFunction(instance,system,&requirements),"xrGetOpenGLGraphicsRequirementsKHR");
  const auto* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
  int major=0,minor=0;char dot=0;
  if(!version||!(std::istringstream(version)>>major>>dot>>minor)||dot!='.'||major<1||minor<0)
    throw std::runtime_error("OpenXR: cannot read the current desktop OpenGL version.");
  const XrVersion actualVersion=XR_MAKE_VERSION(static_cast<std::uint64_t>(major),static_cast<std::uint64_t>(minor),0);
  if(actualVersion<requirements.minApiVersionSupported)
    throw std::runtime_error("OpenXR requires OpenGL "+versionText(requirements.minApiVersionSupported)+" or newer; current context is "+versionText(actualVersion)+". Update the GPU driver and use a compatibility context.");
  if(major>3||(major==3&&minor>=2)) {
    GLint profile=0;glGetIntegerv(contextProfileMask,&profile);
    bool compatible=(profile&compatibilityProfileBit)!=0;
    // Legacy wglCreateContext must preserve compatibility. Some drivers return
    // a zero profile mask for these contexts; verify the advertised extension
    // instead of misidentifying them as core-only. Explicit core masks fail.
    if(!compatible&&profile==0){
      const auto* extensionsText=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
      std::istringstream names(extensionsText?extensionsText:"");std::string extension;
      while(names>>extension)if(extension=="GL_ARB_compatibility"){compatible=true;break;}
    }
    if(!compatible)throw std::runtime_error("OpenXR: LINK's fixed-function renderer needs an OpenGL compatibility profile, not a core-only context.");
  }
  // maxApiVersionSupported is the highest TESTED version, not a hard upper bound:
  // a newer compatible context is explicitly allowed by XR_KHR_opengl_enable.
  gl.load();
  auto binding=xrStruct<XrGraphicsBindingOpenGLWin32KHR>(XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR);
  binding.hDC=dc;binding.hGLRC=context;
  auto sessionInfo=xrStruct<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
  sessionInfo.next=&binding;sessionInfo.systemId=system;
  check(xrCreateSession(instance,&sessionInfo,&session),"xrCreateSession(OpenGL Win32)");
  count=0;check(xrEnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"xrEnumerateEnvironmentBlendModes");
  if(!count||count>maxEnumeration)throw std::runtime_error("OpenXR: no supported stereo environment blend mode.");
  std::vector<XrEnvironmentBlendMode> modes(count);
  check(xrEnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()),"xrEnumerateEnvironmentBlendModes");
  blendMode=std::find(modes.begin(),modes.end(),XR_ENVIRONMENT_BLEND_MODE_OPAQUE)!=modes.end()?XR_ENVIRONMENT_BLEND_MODE_OPAQUE:modes.front();
  createSpaces();createActions();createSwapchains();
  gl.bindFramebuffer(framebufferTarget,0);gl.bindRenderbuffer(renderbufferTarget,0);
}

void OpenXrSession::Impl::createSwapchains() {
  std::uint32_t count=0;
  check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"xrEnumerateViewConfigurationViews");
  if(count!=2)throw std::runtime_error("OpenXR: LINK requires a primary stereo headset with exactly two views.");
  std::array<XrViewConfigurationView,2> configurations;
  for(auto& configuration:configurations)configuration=xrStruct<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
  check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,configurations.data()),"xrEnumerateViewConfigurationViews");
  if(count!=2)throw std::runtime_error("OpenXR: stereo view count changed unexpectedly.");
  count=0;check(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"xrEnumerateSwapchainFormats");
  if(!count||count>maxEnumeration)throw std::runtime_error("OpenXR: invalid swapchain-format count.");
  std::vector<std::int64_t> formats(count);
  check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"xrEnumerateSwapchainFormats");
  if(std::find(formats.begin(),formats.end(),rgba8)==formats.end())
    throw std::runtime_error("OpenXR: runtime does not offer GL_RGBA8. This renderer requires a non-sRGB RGBA8 swapchain.");
  GLint maximumTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximumTexture);
  for(std::size_t eye=0;eye<2;++eye) {
    auto& chain=chains[eye];const auto& configuration=configurations[eye];
    if(!configuration.recommendedImageRectWidth||!configuration.recommendedImageRectHeight||
       configuration.recommendedImageRectWidth>static_cast<std::uint32_t>(std::numeric_limits<int>::max())||
       configuration.recommendedImageRectHeight>static_cast<std::uint32_t>(std::numeric_limits<int>::max())||maximumTexture<=0||
       configuration.recommendedImageRectWidth>static_cast<std::uint32_t>(maximumTexture)||
       configuration.recommendedImageRectHeight>static_cast<std::uint32_t>(maximumTexture))
      throw std::runtime_error("OpenXR: recommended eye texture dimensions exceed the current GPU limits.");
    chain.width=static_cast<int>(configuration.recommendedImageRectWidth);chain.height=static_cast<int>(configuration.recommendedImageRectHeight);
    auto info=xrStruct<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    info.format=rgba8;info.sampleCount=1;info.width=configuration.recommendedImageRectWidth;info.height=configuration.recommendedImageRectHeight;
    info.faceCount=1;info.arraySize=1;info.mipCount=1;
    check(xrCreateSwapchain(session,&info,&chain.handle),"xrCreateSwapchain");
    count=0;check(xrEnumerateSwapchainImages(chain.handle,0,&count,nullptr),"xrEnumerateSwapchainImages");
    if(!count||count>maxEnumeration)throw std::runtime_error("OpenXR: invalid swapchain image count.");
    chain.images.resize(count);
    for(auto& image:chain.images)image=xrStruct<XrSwapchainImageOpenGLKHR>(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR);
    check(xrEnumerateSwapchainImages(chain.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data())),"xrEnumerateSwapchainImages");
    gl.genFramebuffers(1,&chain.framebuffer);gl.genRenderbuffers(1,&chain.depth);
    if(!chain.framebuffer||!chain.depth)throw std::runtime_error("OpenXR: failed to allocate eye framebuffer/depth storage.");
    gl.bindFramebuffer(framebufferTarget,chain.framebuffer);gl.bindRenderbuffer(renderbufferTarget,chain.depth);
    gl.renderbufferStorage(renderbufferTarget,depthComponent24,chain.width,chain.height);
    gl.framebufferRenderbuffer(framebufferTarget,depthAttachment,renderbufferTarget,chain.depth);
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenXR: OpenGL could not allocate an eye depth buffer.");
  }
}

void OpenXrSession::Impl::pollEvents() {
  requireOwner();
  if(!instance)return;
  for(;;) {
    auto event=xrStruct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
    const XrResult result=xrPollEvent(instance,&event);
    if(result==XR_EVENT_UNAVAILABLE)return;
    check(result,"xrPollEvent");
    if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      exitFlag=true;runningFlag=false;return;
    }
    if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
      const auto& change=*reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
      const auto usedType=floorLevel?XR_REFERENCE_SPACE_TYPE_STAGE:XR_REFERENCE_SPACE_TYPE_LOCAL;
      if(change.session==session&&change.referenceSpaceType==usedType) {
        spaceChangePending=true;spaceChangeTime=change.changeTime;
      }
    }
    if(event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)continue;
    const auto& changed=*reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
    if(changed.session!=session)continue;
    state=changed.state;
    if(state!=XR_SESSION_STATE_FOCUSED) {
      resetInput();
    }
    switch(state) {
      case XR_SESSION_STATE_READY: {
        if(!runningFlag&&!exitFlag) {
          auto info=xrStruct<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
          info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          check(xrBeginSession(session,&info),"xrBeginSession");runningFlag=true;lastDisplayTime=0;
        }
        break;
      }
      case XR_SESSION_STATE_STOPPING:
        if(frame.begun)finishFrame(false,false);
        if(runningFlag) { runningFlag=false;check(xrEndSession(session),"xrEndSession"); }
        break;
      case XR_SESSION_STATE_EXITING:
      case XR_SESSION_STATE_LOSS_PENDING:
        exitFlag=true;runningFlag=false;break;
      default:break;
    }
  }
}
void OpenXrSession::Impl::syncInput() {
  if(state!=XR_SESSION_STATE_FOCUSED) { resetInput();return; }
  const XrActiveActionSet active{actionSet,XR_NULL_PATH};
  auto info=xrStruct<XrActionsSyncInfo>(XR_TYPE_ACTIONS_SYNC_INFO);
  info.countActiveActionSets=1;info.activeActionSets=&active;
  const XrResult result=xrSyncActions(session,&info);
  if(result==XR_SESSION_NOT_FOCUSED) { resetInput();return; }
  check(result,"xrSyncActions");
  frame.inputFocused=true;
  for(std::size_t hand=0;hand<2;++hand) {
    auto& input=frame.hands[hand];
    auto get=xrStruct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    get.subactionPath=handPaths[hand];get.action=aimAction;
    auto aim=xrStruct<XrActionStatePose>(XR_TYPE_ACTION_STATE_POSE);
    check(xrGetActionStatePose(session,&get,&aim),"xrGetActionStatePose");
    input.active=aim.isActive==XR_TRUE;
    if(input.active) {
      auto location=xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
      check(xrLocateSpace(aimSpaces[hand],appSpace,frame.predictedDisplayTime,&location),"xrLocateSpace(controller aim)");
      input.aim=copyPose(location.pose,location.locationFlags);
    }
    get.action=selectAction;
    auto select=xrStruct<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
    check(xrGetActionStateFloat(session,&get,&select),"xrGetActionStateFloat");
    get.action=clickAction;
    auto click=xrStruct<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
    check(xrGetActionStateBoolean(session,&get,&click),"xrGetActionStateBoolean(select)");
    const bool analogActive=select.isActive==XR_TRUE&&std::isfinite(select.currentState);
    const bool clickActive=click.isActive==XR_TRUE;
    const float value=analogActive?std::clamp(select.currentState,0.0f,1.0f):0.0f;
    const bool clicked=clickActive&&click.currentState==XR_TRUE;
    input.selectValue=clicked?1.0f:value;
    const bool usable=input.active&&input.aim.valid()&&input.aim.positionTracked&&input.aim.orientationTracked;
    // Each bound source must observe its own active neutral sample. An inactive
    // or nonfinite channel cannot arm a different source that later returns held.
    if(!usable||!analogActive)selectArmed[hand]=false;
    else if(value<.40f)selectArmed[hand]=true;
    if(!usable||!clickActive)clickArmed[hand]=false;
    else if(!clicked)clickArmed[hand]=true;
    const bool analogDown=usable&&analogActive&&selectArmed[hand]&&value>=(previousAnalogSelect[hand]?.40f:.55f);
    previousAnalogSelect[hand]=analogDown;
    const bool down=analogDown||(usable&&clickActive&&clickArmed[hand]&&clicked);
    input.selectDown=down;input.selectPressed=down&&!previousSelect[hand];input.selectReleased=!down&&previousSelect[hand];
    previousSelect[hand]=down;
    get.action=stickAction;
    auto stick=xrStruct<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
    check(xrGetActionStateVector2f(session,&get,&stick),"xrGetActionStateVector2f");
    input.stickActive=usable&&stick.isActive&&std::isfinite(stick.currentState.x)&&std::isfinite(stick.currentState.y);
    if(input.stickActive)input.thumbstick={std::clamp(stick.currentState.x,-1.0f,1.0f),std::clamp(stick.currentState.y,-1.0f,1.0f)};
    auto booleanState=[&](XrAction action) {
      get.action=action;
      auto valueState=xrStruct<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
      check(xrGetActionStateBoolean(session,&get,&valueState),"xrGetActionStateBoolean(button)");
      return valueState;
    };
    const auto pressed=[&](const XrActionStateBoolean& button,bool& armed,bool& previous) {
      if(!usable||button.isActive!=XR_TRUE) { armed=false;previous=false;return false; }
      const bool down=button.currentState==XR_TRUE;
      if(!down)armed=true;
      const bool edge=armed&&down&&!previous;previous=down;return edge;
    };
    const auto exitState=booleanState(exitAction),recenterState=booleanState(recenterAction);
    // Keep independent latches even when another hand already requested an edge.
    const bool exitPressed=pressed(exitState,exitArmed[hand],previousExit[hand]);
    const bool recenterPressed=pressed(recenterState,recenterArmed[hand],previousRecenter[hand]);
    frame.exitPressed=frame.exitPressed||exitPressed;
    frame.recenterPressed=frame.recenterPressed||recenterPressed;
  }
}

bool OpenXrSession::Impl::acquireEye(std::size_t eye) {
  auto& chain=chains[eye];
  if(!chain.acquired) {
    auto acquire=xrStruct<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
    check(xrAcquireSwapchainImage(chain.handle,&acquire,&chain.index),"xrAcquireSwapchainImage");
    chain.acquired=true;chain.waited=false;
    if(chain.index>=chain.images.size())throw std::runtime_error("OpenXR: runtime returned an invalid swapchain image index.");
  }
  if(!chain.waited) {
    auto wait=xrStruct<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wait.timeout=imageWaitTimeout;
    const XrResult result=xrWaitSwapchainImage(chain.handle,&wait);
    if(result==XR_TIMEOUT_EXPIRED) { frame.swapchainWaitTimedOut=true;return false; }
    check(result,"xrWaitSwapchainImage");chain.waited=true;
  }
  gl.bindFramebuffer(framebufferTarget,chain.framebuffer);
  gl.framebufferTexture2D(framebufferTarget,colorAttachment,GL_TEXTURE_2D,chain.images[chain.index].image,0);
  glDrawBuffer(colorAttachment);glReadBuffer(colorAttachment);
  if(gl.checkFramebufferStatus(framebufferTarget)!=framebufferComplete)
    throw std::runtime_error("OpenXR: eye framebuffer is incomplete. Check GPU/OpenGL compatibility.");
  frame.eyes[eye].colorTexture=chain.images[chain.index].image;
  return true;
}

OpenXrSession::Frame OpenXrSession::Impl::beginFrame() {
  requireOwner();
  if(!session)throw std::runtime_error("OpenXR: initialize the session before beginning a frame.");
  if(frame.begun)throw std::runtime_error("OpenXR: the previous frame must end before beginning another.");
  frame=Frame{};frame.floorLevel=floorLevel;
  if(!runningFlag)return frame;
  currentContext();
  auto wait=xrStruct<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
  auto timing=xrStruct<XrFrameState>(XR_TYPE_FRAME_STATE);
  check(xrWaitFrame(session,&wait,&timing),"xrWaitFrame");
  auto begin=xrStruct<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
  check(xrBeginFrame(session,&begin),"xrBeginFrame");
  frame.begun=true;frame.predictedDisplayTime=timing.predictedDisplayTime;
  frame.shouldRender=timing.shouldRender==XR_TRUE&&!exitFlag;
  const XrDuration elapsed=lastDisplayTime>0&&timing.predictedDisplayTime>=lastDisplayTime?timing.predictedDisplayTime-lastDisplayTime:timing.predictedDisplayPeriod;
  frame.dt=std::clamp(static_cast<double>(elapsed)*1e-9,0.0,.1);lastDisplayTime=timing.predictedDisplayTime;
  try {
    if(spaceChangePending&&timing.predictedDisplayTime>=spaceChangeTime) {
      frame.referenceSpaceChanged=true;spaceChangePending=false;
      resetInput();
    }
    auto head=xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    check(xrLocateSpace(viewSpace,appSpace,timing.predictedDisplayTime,&head),"xrLocateSpace(head)");
    frame.head=copyPose(head.pose,head.locationFlags);
    auto locate=xrStruct<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=timing.predictedDisplayTime;locate.space=appSpace;
    auto viewState=xrStruct<XrViewState>(XR_TYPE_VIEW_STATE);
    for(auto& view:views)view=xrStruct<XrView>(XR_TYPE_VIEW);
    std::uint32_t count=0;
    check(xrLocateViews(session,&locate,&viewState,2,&count,views.data()),"xrLocateViews");
    if(count!=2)throw std::runtime_error("OpenXR: runtime did not locate two stereo views.");
    for(std::size_t eye=0;eye<2;++eye) {
      auto& target=frame.eyes[eye];const auto& source=views[eye];const auto& chain=chains[eye];
      target.pose=copyPose(source.pose,locationFlags(viewState.viewStateFlags));
      target.fov={source.fov.angleLeft,source.fov.angleRight,source.fov.angleUp,source.fov.angleDown};
      target.width=chain.width;target.height=chain.height;target.framebuffer=chain.framebuffer;
      const bool validFov=std::isfinite(source.fov.angleLeft)&&std::isfinite(source.fov.angleRight)&&std::isfinite(source.fov.angleUp)&&std::isfinite(source.fov.angleDown)&&source.fov.angleLeft<source.fov.angleRight&&source.fov.angleDown<source.fov.angleUp;
      if(!target.pose.valid()||!validFov)frame.shouldRender=false;
    }
    syncInput();
    if(frame.shouldRender) {
      if(!acquireEye(0)||!acquireEye(1))frame.shouldRender=false;
    } else {
      // Drain only already-acquired images after a previous timeout. Never acquire
      // a fresh image for shouldRender=false, and never release an unwaited image.
      for(std::size_t eye=0;eye<2;++eye)if(chains[eye].acquired&&!chains[eye].waited)acquireEye(eye);
    }
    gl.bindFramebuffer(framebufferTarget,0);
    return frame;
  } catch(...) {
    finishFrame(false,false);
    throw;
  }
}

void OpenXrSession::Impl::finishFrame(bool submit,bool throwErrors) {
  if(!frame.begun)return;
  const bool complete=chains[0].acquired&&chains[0].waited&&chains[1].acquired&&chains[1].waited;
  XrResult firstError=XR_SUCCESS;
  if(gl.bindFramebuffer)gl.bindFramebuffer(framebufferTarget,0);
  if(chains[0].waited||chains[1].waited)glFlush();
  for(auto& chain:chains)if(chain.acquired&&chain.waited) {
    auto release=xrStruct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
    const XrResult result=xrReleaseSwapchainImage(chain.handle,&release);
    if(XR_FAILED(result)&&XR_SUCCEEDED(firstError))firstError=result;
    if(XR_SUCCEEDED(result)) { chain.acquired=false;chain.waited=false; }
    if(result==XR_SESSION_LOSS_PENDING)exitFlag=true;
  }
  std::array<XrCompositionLayerProjectionView,2> projectionViews;
  for(std::size_t eye=0;eye<2;++eye) {
    auto& view=projectionViews[eye];view=xrStruct<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
    view.pose=views[eye].pose;view.fov=views[eye].fov;
    view.subImage.swapchain=chains[eye].handle;view.subImage.imageRect.offset={0,0};
    view.subImage.imageRect.extent={chains[eye].width,chains[eye].height};view.subImage.imageArrayIndex=0;
  }
  auto projection=xrStruct<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
  projection.space=appSpace;projection.viewCount=2;projection.views=projectionViews.data();
  const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
  auto end=xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
  end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=blendMode;
  if(submit&&frame.shouldRender&&complete&&XR_SUCCEEDED(firstError)&&!exitFlag) { end.layerCount=1;end.layers=&layer; }
  const XrResult result=xrEndFrame(session,&end);
  frame.begun=false;frame.shouldRender=false;
  if(result==XR_SESSION_LOSS_PENDING||result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST||firstError==XR_ERROR_SESSION_LOST||firstError==XR_ERROR_INSTANCE_LOST)exitFlag=true;
  if(throwErrors) { check(firstError,"xrReleaseSwapchainImage");check(result,"xrEndFrame"); }
}

void OpenXrSession::Impl::cancelWithoutGl() noexcept {
  if(!frame.begun)return;
  auto end=xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
  end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=blendMode;
  xrEndFrame(session,&end);frame.begun=false;frame.shouldRender=false;
}

void OpenXrSession::Impl::cleanup() noexcept {
  bool glCurrent=false;
  if(context&&dc&&owner==std::this_thread::get_id()) {
    glCurrent=wglGetCurrentContext()==context&&wglGetCurrentDC()==dc;
    if(!glCurrent)glCurrent=wglMakeCurrent(dc,context)!=FALSE;
  }
  // Teardown never waits indefinitely for a timed-out image. Destroying a
  // swapchain releases runtime ownership, including any outstanding acquisition.
  if(frame.begun) {
    try { if(glCurrent)finishFrame(false,false);else cancelWithoutGl(); } catch(...) { cancelWithoutGl(); }
  }
  if(glCurrent) {
    if(gl.bindFramebuffer)gl.bindFramebuffer(framebufferTarget,0);
    glFinish(); // xrDestroySwapchain requires all submitted image GL work complete.
  }
  for(auto& chain:chains) {
    if(glCurrent&&chain.framebuffer&&gl.deleteFramebuffers)gl.deleteFramebuffers(1,&chain.framebuffer);
    if(glCurrent&&chain.depth&&gl.deleteRenderbuffers)gl.deleteRenderbuffers(1,&chain.depth);
    chain.framebuffer=chain.depth=0;
    if(chain.handle)xrDestroySwapchain(chain.handle);
    chain.handle=XR_NULL_HANDLE;chain.images.clear();chain.acquired=chain.waited=false;
  }
  for(auto& space:aimSpaces) { if(space)xrDestroySpace(space);space=XR_NULL_HANDLE; }
  if(viewSpace)xrDestroySpace(viewSpace);viewSpace=XR_NULL_HANDLE;
  if(appSpace)xrDestroySpace(appSpace);appSpace=XR_NULL_HANDLE;
  if(actionSet)xrDestroyActionSet(actionSet);actionSet=XR_NULL_HANDLE;
  if(session)xrDestroySession(session);session=XR_NULL_HANDLE;
  if(instance)xrDestroyInstance(instance);instance=XR_NULL_HANDLE;
  aimAction=selectAction=clickAction=stickAction=exitAction=recenterAction=hapticAction=XR_NULL_HANDLE;
  runningFlag=false;exitFlag=false;floorLevel=false;legacyApi=false;spaceChangePending=false;
  state=XR_SESSION_STATE_UNKNOWN;system=XR_NULL_SYSTEM_ID;lastDisplayTime=0;spaceChangeTime=0;
  resetInput();handPaths={XR_NULL_PATH,XR_NULL_PATH};
  frame=Frame{};name.clear();context=nullptr;dc=nullptr;gl=Gl{};
}

OpenXrSession::OpenXrSession():impl_(std::make_unique<Impl>()) {}
OpenXrSession::~OpenXrSession() { shutdown(); }
void OpenXrSession::initialize(HDC dc,HGLRC context) {
  impl_->cleanup();
  try { impl_->initialize(dc,context); } catch(...) { impl_->cleanup();throw; }
}
void OpenXrSession::shutdown() noexcept { impl_->cleanup(); }
void OpenXrSession::pollEvents() { if(impl_->instance)impl_->pollEvents(); }
bool OpenXrSession::running() const noexcept { return impl_->runningFlag; }
bool OpenXrSession::focused() const noexcept { return impl_->runningFlag&&impl_->state==XR_SESSION_STATE_FOCUSED; }
const char* OpenXrSession::sessionStateName() const noexcept {
  switch(impl_->state){
    case XR_SESSION_STATE_IDLE:return "IDLE";
    case XR_SESSION_STATE_READY:return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED:return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING:return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:return "EXITING";
    default:return "UNKNOWN";
  }
}
bool OpenXrSession::exitRequested() const noexcept { return impl_->exitFlag; }
const std::string& OpenXrSession::runtimeName() const noexcept { return impl_->name; }
OpenXrSession::Frame OpenXrSession::beginFrame() { return impl_->beginFrame(); }
void OpenXrSession::bindEye(std::size_t eyeIndex) {
  impl_->currentContext();
  if(eyeIndex>=2||!impl_->frame.begun||!impl_->frame.shouldRender)throw std::runtime_error("OpenXR: bindEye requires a renderable acquired stereo frame.");
  const auto& eye=impl_->chains[eyeIndex];
  if(!eye.acquired||!eye.waited)throw std::runtime_error("OpenXR: eye image has not finished waiting.");
  impl_->gl.bindFramebuffer(framebufferTarget,eye.framebuffer);glDrawBuffer(colorAttachment);glReadBuffer(colorAttachment);
  glDisable(framebufferSrgb);glViewport(0,0,eye.width,eye.height);
}
void OpenXrSession::mirror(int width,int height) {
  if(width<=0||height<=0||!impl_->frame.begun||!impl_->frame.shouldRender)return;
  impl_->currentContext();const auto& eye=impl_->chains[0];
  if(!eye.acquired||!eye.waited)return;
  impl_->gl.bindFramebuffer(readFramebuffer,eye.framebuffer);glReadBuffer(colorAttachment);
  impl_->gl.bindFramebuffer(drawFramebuffer,0);glDrawBuffer(GL_BACK);
  glDisable(GL_SCISSOR_TEST);glDisable(framebufferSrgb);glViewport(0,0,width,height);
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  const double scale=std::min(static_cast<double>(width)/eye.width,static_cast<double>(height)/eye.height);
  const int fittedWidth=std::max(1,static_cast<int>(eye.width*scale)),fittedHeight=std::max(1,static_cast<int>(eye.height*scale));
  const int left=(width-fittedWidth)/2,bottom=(height-fittedHeight)/2;
  impl_->gl.blitFramebuffer(0,0,eye.width,eye.height,left,bottom,left+fittedWidth,bottom+fittedHeight,GL_COLOR_BUFFER_BIT,GL_LINEAR);
  impl_->gl.bindFramebuffer(framebufferTarget,0);
}
void OpenXrSession::endFrame(bool submit) {
  impl_->requireOwner();if(!impl_->frame.begun)return;
  impl_->currentContext();impl_->finishFrame(submit,true);
}
void OpenXrSession::abortFrame() noexcept {
  if(!impl_->frame.begun)return;
  try { impl_->currentContext();impl_->finishFrame(false,false); } catch(...) { impl_->cancelWithoutGl(); }
}
void OpenXrSession::requestExit() {
  impl_->requireOwner();impl_->exitFlag=true;
  if(impl_->session&&impl_->runningFlag)impl_->check(xrRequestExitSession(impl_->session),"xrRequestExitSession");
}
void OpenXrSession::haptic(std::size_t hand,float amplitude,double seconds) noexcept {
  if(hand>=2||!focused()||!impl_->session||impl_->owner!=std::this_thread::get_id()||!std::isfinite(amplitude)||!std::isfinite(seconds))return;
  auto info=xrStruct<XrHapticActionInfo>(XR_TYPE_HAPTIC_ACTION_INFO);
  info.action=impl_->hapticAction;info.subactionPath=impl_->handPaths[hand];
  auto vibration=xrStruct<XrHapticVibration>(XR_TYPE_HAPTIC_VIBRATION);
  vibration.amplitude=std::clamp(amplitude,0.0f,1.0f);vibration.duration=static_cast<XrDuration>(std::clamp(seconds,.001,1.0)*1e9);vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
  xrApplyHapticFeedback(impl_->session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}
} // namespace linkworld