// Copyright 2026 Song Youngwoon
// SPDX-License-Identifier: Apache-2.0
// QA ONLY: deterministic software OpenXR runtime. This is never headset validation.
#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#include <unknwn.h>
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr const char* runtimeName="LINK Synthetic Test Runtime";
constexpr XrDuration period=11111111;
constexpr XrSpaceLocationFlags tracked=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
struct Action {std::string name;XrActionType type;};
struct Space {XrReferenceSpaceType type=XR_REFERENCE_SPACE_TYPE_LOCAL;int hand=-1;};
struct Chain {std::array<GLuint,2> images{};unsigned width=0,height=0,index=0,number=0;int phase=0;};
struct State {
  bool session=false,actionSet=false,running=false,focused=false,focusAnnounced=false,visibleOnly=false;int framePhase=0;
  unsigned frames=0,waits=0,begins=0,ends=0,submitted=0,acquired=0,imageWaits=0,released=0,haptics=0,errors=0;
  unsigned trackedLossSamples=0,heldLossSamples=0,neutralSamples=0;
  unsigned unfocusedFrames=0,focusedFrames=0,focusedSyncs=0,unfocusedInputCalls=0,focusHeldSamples=0,focusNeutralSamples=0;
  unsigned createdChains=0,destroyedChains=0,createdSpaces=0,destroyedSpaces=0;
  std::array<std::uint64_t,2> eyeHash{};std::array<bool,2> eyeWritten{};std::string firstError;
  std::deque<XrSessionState> events;std::vector<Chain*> chains;std::vector<Space*> spaces;std::vector<Action*> actions;
  std::map<std::string,XrPath> paths;
};
State* state=nullptr;
XrInstance instance(){return reinterpret_cast<XrInstance>(state);}
XrSession session(){return reinterpret_cast<XrSession>(state);}
XrTime displayTime(){return 1000000000ll+static_cast<XrTime>(state->frames)*period;}
XrPosef pose(float x=0,float y=0,float z=0){XrPosef p{};p.orientation.w=1;p.position={x,y,z};return p;}
XrResult failure(const char* message,XrResult result=XR_ERROR_CALL_ORDER_INVALID){if(state){++state->errors;if(state->firstError.empty())state->firstError=message;}return result;}
XrResult enumeration(std::uint32_t capacity,std::uint32_t* count,std::uint32_t expected){if(!count)return XR_ERROR_VALIDATION_FAILURE;*count=expected;return capacity&&capacity<expected?XR_ERROR_SIZE_INSUFFICIENT:XR_SUCCESS;}
bool lost(){return state->frames>=40&&state->frames<=45;}
bool held(){const auto f=state->frames;return (f>=1&&f<=14)||(f>=20&&f<=35)||(f>=40&&f<=55)||(f>=66&&f<=75);}
int hand(XrPath path){auto found=state->paths.find("/user/hand/right");return found!=state->paths.end()&&found->second==path?1:0;}
void report(){
  wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"LINK_XR_TEST_REPORT",path,32768);if(!length||length>=32768)return;
  const bool clean=state->chains.empty()&&state->spaces.empty()&&state->actions.empty()&&!state->session&&!state->actionSet&&state->framePhase==0;
  std::ofstream out{std::filesystem::path(path)};
  out<<"{\n  \"runtime\": \""<<runtimeName<<"\",\n  \"syntheticRuntime\": true,\n  \"headsetFramesVerified\": false,\n";
  out<<"  \"waitFrames\": "<<state->waits<<", \"beginFrames\": "<<state->begins<<", \"endFrames\": "<<state->ends<<",\n";
  out<<"  \"submittedFrames\": "<<state->submitted<<", \"acquiredImages\": "<<state->acquired<<", \"waitedImages\": "<<state->imageWaits<<", \"releasedImages\": "<<state->released<<",\n";
  out<<"  \"visibleOnly\": "<<(state->visibleOnly?"true":"false")<<",\n";
  out<<"  \"unfocusedFrames\": "<<state->unfocusedFrames<<", \"focusedFrames\": "<<state->focusedFrames<<", \"focusedSyncs\": "<<state->focusedSyncs<<", \"unfocusedInputCalls\": "<<state->unfocusedInputCalls<<", \"focusHeldSamples\": "<<state->focusHeldSamples<<", \"focusNeutralSamples\": "<<state->focusNeutralSamples<<",\n";
  out<<"  \"haptics\": "<<state->haptics<<", \"trackingLossSamples\": "<<state->trackedLossSamples<<", \"heldAfterLossSamples\": "<<state->heldLossSamples<<", \"neutralSamples\": "<<state->neutralSamples<<",\n";
  out<<"  \"createdSwapchains\": "<<state->createdChains<<", \"destroyedSwapchains\": "<<state->destroyedChains<<", \"createdSpaces\": "<<state->createdSpaces<<", \"destroyedSpaces\": "<<state->destroyedSpaces<<",\n";
  out<<"  \"leftEyeHash\": \""<<state->eyeHash[0]<<"\", \"rightEyeHash\": \""<<state->eyeHash[1]<<"\",\n";
  out<<"  \"eyeTexturesWritten\": "<<(state->eyeWritten[0]&&state->eyeWritten[1]?"true":"false")<<",\n";
  out<<"  \"orderErrors\": "<<state->errors<<", \"firstError\": \""<<state->firstError<<"\", \"resourcesReleased\": "<<(clean?"true":"false")<<"\n}\n";
}
#define RT(name,...) XrResult XRAPI_CALL rt##name(__VA_ARGS__)
RT(EnumerateInstanceExtensionProperties,const char* layer,uint32_t cap,uint32_t* count,XrExtensionProperties* values){
  if(layer)return XR_ERROR_API_LAYER_NOT_PRESENT;auto result=enumeration(cap,count,1);if(XR_FAILED(result)||!cap)return result;if(!values)return XR_ERROR_VALIDATION_FAILURE;
  values[0].type=XR_TYPE_EXTENSION_PROPERTIES;values[0].next=nullptr;std::strcpy(values[0].extensionName,XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);values[0].extensionVersion=XR_KHR_opengl_enable_SPEC_VERSION;return XR_SUCCESS;
}
RT(CreateInstance,const XrInstanceCreateInfo* info,XrInstance* value){if(!info||!value||state)return XR_ERROR_INITIALIZATION_FAILED;state=new State;wchar_t visibleOnly[2]{};state->visibleOnly=GetEnvironmentVariableW(L"LINK_XR_TEST_VISIBLE_ONLY",visibleOnly,2)==1&&visibleOnly[0]==L'1';*value=instance();return XR_SUCCESS;}
RT(DestroyInstance,XrInstance){if(!state)return XR_ERROR_HANDLE_INVALID;report();for(auto p:state->chains){glDeleteTextures(2,p->images.data());delete p;}for(auto p:state->spaces)delete p;for(auto p:state->actions)delete p;delete state;state=nullptr;return XR_SUCCESS;}
RT(GetInstanceProperties,XrInstance,XrInstanceProperties* value){value->runtimeVersion=XR_MAKE_VERSION(1,0,0);std::strcpy(value->runtimeName,runtimeName);return XR_SUCCESS;}
RT(ResultToString,XrInstance,XrResult result,char text[XR_MAX_RESULT_STRING_SIZE]){std::snprintf(text,XR_MAX_RESULT_STRING_SIZE,"LINK_SYNTHETIC_RESULT_%d",static_cast<int>(result));return XR_SUCCESS;}
RT(StructureTypeToString,XrInstance,XrStructureType type,char text[XR_MAX_STRUCTURE_NAME_SIZE]){std::snprintf(text,XR_MAX_STRUCTURE_NAME_SIZE,"LINK_SYNTHETIC_TYPE_%d",static_cast<int>(type));return XR_SUCCESS;}
RT(GetSystem,XrInstance,const XrSystemGetInfo* info,XrSystemId* value){if(info->formFactor!=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)return XR_ERROR_FORM_FACTOR_UNSUPPORTED;*value=1;return XR_SUCCESS;}
RT(GetSystemProperties,XrInstance,XrSystemId,XrSystemProperties* value){value->systemId=1;value->vendorId=0;std::strcpy(value->systemName,runtimeName);value->graphicsProperties={4096,4096,1};value->trackingProperties={XR_TRUE,XR_TRUE};return XR_SUCCESS;}
RT(GetOpenGLGraphicsRequirementsKHR,XrInstance,XrSystemId,XrGraphicsRequirementsOpenGLKHR* value){GLint profile=0;glGetIntegerv(0x9126,&profile);std::fprintf(stdout,"[synthetic] GL profile mask=%d error=0x%x context=%p\n",profile,glGetError(),wglGetCurrentContext());value->minApiVersionSupported=XR_MAKE_VERSION(3,0,0);value->maxApiVersionSupported=XR_MAKE_VERSION(4,6,0);return XR_SUCCESS;}
RT(EnumerateEnvironmentBlendModes,XrInstance,XrSystemId,XrViewConfigurationType,uint32_t cap,uint32_t* count,XrEnvironmentBlendMode* values){auto r=enumeration(cap,count,1);if(XR_SUCCEEDED(r)&&cap)values[0]=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;return r;}
RT(EnumerateViewConfigurations,XrInstance,XrSystemId,uint32_t cap,uint32_t* count,XrViewConfigurationType* values){auto r=enumeration(cap,count,1);if(XR_SUCCEEDED(r)&&cap)values[0]=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;return r;}
RT(GetViewConfigurationProperties,XrInstance,XrSystemId,XrViewConfigurationType,XrViewConfigurationProperties* value){value->viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;value->fovMutable=XR_FALSE;return XR_SUCCESS;}
RT(EnumerateViewConfigurationViews,XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* values){if(type!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;auto r=enumeration(cap,count,2);if(XR_FAILED(r)||!cap)return r;for(unsigned i=0;i<2;++i){values[i].recommendedImageRectWidth=512;values[i].maxImageRectWidth=4096;values[i].recommendedImageRectHeight=512;values[i].maxImageRectHeight=4096;values[i].recommendedSwapchainSampleCount=1;values[i].maxSwapchainSampleCount=1;}return XR_SUCCESS;}
RT(CreateSession,XrInstance,const XrSessionCreateInfo* info,XrSession* value){auto binding=static_cast<const XrGraphicsBindingOpenGLWin32KHR*>(info->next);if(!binding||binding->type!=XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR||wglGetCurrentContext()!=binding->hGLRC||wglGetCurrentDC()!=binding->hDC)return XR_ERROR_GRAPHICS_DEVICE_INVALID;state->session=true;state->events.push_back(XR_SESSION_STATE_READY);*value=session();return XR_SUCCESS;}
RT(DestroySession,XrSession){if(!state->chains.empty()||!state->spaces.empty())return failure("DestroySession before child cleanup");if(state->framePhase)return failure("DestroySession with outstanding frame");state->session=false;state->running=false;return XR_SUCCESS;}
RT(BeginSession,XrSession,const XrSessionBeginInfo*){if(state->running)return failure("BeginSession twice");state->running=true;state->events.push_back(XR_SESSION_STATE_SYNCHRONIZED);state->events.push_back(XR_SESSION_STATE_VISIBLE);return XR_SUCCESS;}
RT(EndSession,XrSession){if(state->framePhase)return failure("EndSession with outstanding frame");state->running=false;state->events.push_back(XR_SESSION_STATE_EXITING);return XR_SUCCESS;}
RT(RequestExitSession,XrSession){state->events.push_back(XR_SESSION_STATE_STOPPING);return XR_SUCCESS;}
RT(PollEvent,XrInstance,XrEventDataBuffer* buffer){if(state->running&&state->frames>=12&&!state->focusAnnounced&&!state->visibleOnly){state->focusAnnounced=true;state->events.push_back(XR_SESSION_STATE_FOCUSED);}if(state->events.empty())return XR_EVENT_UNAVAILABLE;XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};event.session=session();event.state=state->events.front();event.time=displayTime();state->focused=event.state==XR_SESSION_STATE_FOCUSED;state->events.pop_front();std::memcpy(buffer,&event,sizeof(event));return XR_SUCCESS;}
RT(EnumerateReferenceSpaces,XrSession,uint32_t cap,uint32_t* count,XrReferenceSpaceType* values){auto r=enumeration(cap,count,3);if(XR_SUCCEEDED(r)&&cap){values[0]=XR_REFERENCE_SPACE_TYPE_STAGE;values[1]=XR_REFERENCE_SPACE_TYPE_LOCAL;values[2]=XR_REFERENCE_SPACE_TYPE_VIEW;}return r;}
RT(CreateReferenceSpace,XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* value){auto space=new Space;space->type=info->referenceSpaceType;state->spaces.push_back(space);++state->createdSpaces;*value=reinterpret_cast<XrSpace>(space);return XR_SUCCESS;}
RT(GetReferenceSpaceBoundsRect,XrSession,XrReferenceSpaceType,XrExtent2Df* value){*value={4,4};return XR_SUCCESS;}
RT(CreateActionSpace,XrSession,const XrActionSpaceCreateInfo* info,XrSpace* value){auto space=new Space;space->hand=hand(info->subactionPath);state->spaces.push_back(space);++state->createdSpaces;*value=reinterpret_cast<XrSpace>(space);return XR_SUCCESS;}
RT(DestroySpace,XrSpace value){auto p=reinterpret_cast<Space*>(value);auto it=std::find(state->spaces.begin(),state->spaces.end(),p);if(it==state->spaces.end())return XR_ERROR_HANDLE_INVALID;state->spaces.erase(it);delete p;++state->destroyedSpaces;return XR_SUCCESS;}
RT(LocateSpace,XrSpace value,XrSpace,XrTime time,XrSpaceLocation* location){if(time!=displayTime())return failure("LocateSpace wrong predicted display time");auto space=reinterpret_cast<Space*>(value);location->locationFlags=tracked;if(space->hand<0)location->pose=pose(.005f*std::sin(state->frames*.03f),1.65f,0);else{location->pose=pose(space->hand?0.f:-.25f,1.5f,-.25f);if(lost())location->locationFlags=0;}return XR_SUCCESS;}
RT(LocateViews,XrSession,const XrViewLocateInfo* info,XrViewState* viewState,uint32_t cap,uint32_t* count,XrView* views){if(info->displayTime!=displayTime())return failure("LocateViews wrong predicted display time");auto r=enumeration(cap,count,2);if(XR_FAILED(r)||!cap)return r;viewState->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;for(unsigned i=0;i<2;++i){views[i].pose=pose((i?.032f:-.032f)+.005f*std::sin(state->frames*.03f),1.65f,0);views[i].fov={-.70f,.70f,.70f,-.70f};}return XR_SUCCESS;}
RT(EnumerateSwapchainFormats,XrSession,uint32_t cap,uint32_t* count,int64_t* values){auto r=enumeration(cap,count,1);if(XR_SUCCEEDED(r)&&cap)values[0]=GL_RGBA8;return r;}
RT(CreateSwapchain,XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* value){if(info->format!=GL_RGBA8||info->sampleCount!=1||info->arraySize!=1||info->faceCount!=1||info->mipCount!=1)return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;auto chain=new Chain;chain->width=info->width;chain->height=info->height;chain->number=state->createdChains++;GLint old=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);glGenTextures(2,chain->images.data());const std::vector<unsigned char> initial(static_cast<std::size_t>(info->width)*info->height*4,0);for(auto texture:chain->images){glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,info->width,info->height,0,GL_RGBA,GL_UNSIGNED_BYTE,initial.data());}glBindTexture(GL_TEXTURE_2D,old);if(glGetError()!=GL_NO_ERROR){glDeleteTextures(2,chain->images.data());delete chain;return failure("CreateSwapchain OpenGL allocation",XR_ERROR_RUNTIME_FAILURE);}state->chains.push_back(chain);*value=reinterpret_cast<XrSwapchain>(chain);return XR_SUCCESS;}
RT(DestroySwapchain,XrSwapchain value){auto chain=reinterpret_cast<Chain*>(value);auto it=std::find(state->chains.begin(),state->chains.end(),chain);if(it==state->chains.end())return XR_ERROR_HANDLE_INVALID;if(chain->phase)return failure("DestroySwapchain with acquired image");glDeleteTextures(2,chain->images.data());state->chains.erase(it);delete chain;++state->destroyedChains;return XR_SUCCESS;}
RT(EnumerateSwapchainImages,XrSwapchain value,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images){auto r=enumeration(cap,count,2);if(XR_FAILED(r)||!cap)return r;auto chain=reinterpret_cast<Chain*>(value);auto glImages=reinterpret_cast<XrSwapchainImageOpenGLKHR*>(images);for(unsigned i=0;i<2;++i){if(glImages[i].type!=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR)return XR_ERROR_VALIDATION_FAILURE;glImages[i].image=chain->images[i];}return XR_SUCCESS;}
RT(AcquireSwapchainImage,XrSwapchain value,const XrSwapchainImageAcquireInfo*,uint32_t* index){auto c=reinterpret_cast<Chain*>(value);if(state->framePhase!=2||c->phase)return failure("Acquire outside begun frame or twice");c->index=(state->frames-1)%2;c->phase=1;*index=c->index;++state->acquired;return XR_SUCCESS;}
RT(WaitSwapchainImage,XrSwapchain value,const XrSwapchainImageWaitInfo*){auto c=reinterpret_cast<Chain*>(value);if(c->phase!=1)return failure("Wait image without acquire");c->phase=2;++state->imageWaits;return XR_SUCCESS;}
RT(ReleaseSwapchainImage,XrSwapchain value,const XrSwapchainImageReleaseInfo*){auto c=reinterpret_cast<Chain*>(value);if(c->phase!=2)return failure("Release before successful image wait");if(state->frames>=120&&c->number<2){GLint old=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);glBindTexture(GL_TEXTURE_2D,c->images[c->index]);std::vector<unsigned char> pixels(static_cast<std::size_t>(c->width)*c->height*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glBindTexture(GL_TEXTURE_2D,old);std::uint64_t hash=1469598103934665603ull;for(auto v:pixels){hash^=v;hash*=1099511628211ull;}state->eyeHash[c->number]=hash;state->eyeWritten[c->number]=std::any_of(pixels.begin(),pixels.end(),[](unsigned char value){return value!=0;});}c->phase=0;++state->released;return XR_SUCCESS;}
RT(WaitFrame,XrSession,const XrFrameWaitInfo*,XrFrameState* value){if(!state->running||state->framePhase)return failure("WaitFrame before session or previous EndFrame");state->framePhase=1;++state->frames;++state->waits;if(state->focused)++state->focusedFrames;else ++state->unfocusedFrames;value->predictedDisplayTime=displayTime();value->predictedDisplayPeriod=period;value->shouldRender=XR_TRUE;return XR_SUCCESS;}
RT(BeginFrame,XrSession,const XrFrameBeginInfo*){if(state->framePhase!=1)return failure("BeginFrame before WaitFrame");state->framePhase=2;++state->begins;return XR_SUCCESS;}
RT(EndFrame,XrSession,const XrFrameEndInfo* info){if(state->framePhase!=2)return failure("EndFrame before BeginFrame");if(info->displayTime!=displayTime())return failure("EndFrame wrong predicted display time");for(auto c:state->chains)if(c->phase)return failure("EndFrame before releasing image");if(info->layerCount){if(info->layerCount!=1||!info->layers||info->layers[0]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION)return failure("Expected single projection layer");auto layer=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);if(layer->viewCount!=2)return failure("Expected stereo projection views");for(unsigned i=0;i<2;++i){auto c=reinterpret_cast<Chain*>(layer->views[i].subImage.swapchain);if(std::find(state->chains.begin(),state->chains.end(),c)==state->chains.end()||c->number!=i)return failure("Projection references wrong eye swapchain");if(layer->views[i].subImage.imageRect.extent.width!=static_cast<int>(c->width)||layer->views[i].subImage.imageRect.extent.height!=static_cast<int>(c->height))return failure("Projection image rectangle mismatch");}++state->submitted;}state->framePhase=0;++state->ends;return XR_SUCCESS;}
RT(StringToPath,XrInstance,const char* text,XrPath* value){auto [it,inserted]=state->paths.emplace(text,state->paths.size()+1);*value=it->second;return XR_SUCCESS;}
RT(PathToString,XrInstance,XrPath path,uint32_t cap,uint32_t* count,char* value){for(auto& [key,p]:state->paths)if(p==path){auto r=enumeration(cap,count,static_cast<uint32_t>(key.size()+1));if(XR_SUCCEEDED(r)&&cap)std::strcpy(value,key.c_str());return r;}return XR_ERROR_PATH_INVALID;}
RT(CreateActionSet,XrInstance,const XrActionSetCreateInfo*,XrActionSet* value){state->actionSet=true;*value=reinterpret_cast<XrActionSet>(state);return XR_SUCCESS;}
RT(DestroyActionSet,XrActionSet){for(auto p:state->actions)delete p;state->actions.clear();state->actionSet=false;return XR_SUCCESS;}
RT(CreateAction,XrActionSet,const XrActionCreateInfo* info,XrAction* value){auto p=new Action{info->actionName,info->actionType};state->actions.push_back(p);*value=reinterpret_cast<XrAction>(p);return XR_SUCCESS;}
RT(DestroyAction,XrAction value){auto p=reinterpret_cast<Action*>(value);auto it=std::find(state->actions.begin(),state->actions.end(),p);if(it==state->actions.end())return XR_ERROR_HANDLE_INVALID;state->actions.erase(it);delete p;return XR_SUCCESS;}
RT(SuggestInteractionProfileBindings,XrInstance,const XrInteractionProfileSuggestedBinding*){return XR_SUCCESS;}
RT(AttachSessionActionSets,XrSession,const XrSessionActionSetsAttachInfo*){return XR_SUCCESS;}
RT(GetCurrentInteractionProfile,XrSession,XrPath,XrInteractionProfileState* value){return rtStringToPath(instance(),"/interaction_profiles/oculus/touch_controller",&value->interactionProfile);}
RT(SyncActions,XrSession,const XrActionsSyncInfo*){if(!state->focused){++state->unfocusedInputCalls;return failure("SyncActions while unfocused",XR_SESSION_NOT_FOCUSED);}++state->focusedSyncs;if(state->framePhase!=2)return failure("SyncActions outside begun frame");return XR_SUCCESS;}
RT(GetActionStatePose,XrSession,const XrActionStateGetInfo*,XrActionStatePose* value){if(!state->focused){++state->unfocusedInputCalls;return failure("Action state queried while unfocused",XR_SESSION_NOT_FOCUSED);}value->isActive=XR_TRUE;if(lost())++state->trackedLossSamples;return XR_SUCCESS;}
RT(GetActionStateFloat,XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* value){if(!state->focused){++state->unfocusedInputCalls;return failure("Action state queried while unfocused",XR_SESSION_NOT_FOCUSED);}value->isActive=XR_TRUE;value->currentState=hand(info->subactionPath)&&held()?1.f:0.f;value->changedSinceLastSync=XR_TRUE;value->lastChangeTime=displayTime();if(hand(info->subactionPath)){if(state->frames>=13&&state->frames<=14)++state->focusHeldSamples;if(state->frames>=15&&state->frames<=19)++state->focusNeutralSamples;if(state->frames>=46&&state->frames<=55)++state->heldLossSamples;if(state->frames>=56&&state->frames<=65)++state->neutralSamples;}return XR_SUCCESS;}
RT(GetActionStateBoolean,XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* value){if(!state->focused){++state->unfocusedInputCalls;return failure("Action state queried while unfocused",XR_SESSION_NOT_FOCUSED);}value->isActive=XR_TRUE;value->currentState=XR_FALSE;value->changedSinceLastSync=XR_FALSE;value->lastChangeTime=displayTime();return XR_SUCCESS;}
RT(GetActionStateVector2f,XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* value){if(!state->focused){++state->unfocusedInputCalls;return failure("Action state queried while unfocused",XR_SESSION_NOT_FOCUSED);}value->isActive=XR_TRUE;value->currentState={0,0};value->changedSinceLastSync=XR_FALSE;value->lastChangeTime=displayTime();return XR_SUCCESS;}
RT(ApplyHapticFeedback,XrSession,const XrHapticActionInfo*,const XrHapticBaseHeader*){if(!state->focused){++state->unfocusedInputCalls;return failure("Haptics while unfocused",XR_SESSION_NOT_FOCUSED);}++state->haptics;return XR_SUCCESS;}
RT(StopHapticFeedback,XrSession,const XrHapticActionInfo*){return XR_SUCCESS;}
RT(GetInstanceProcAddr,XrInstance,const char* name,PFN_xrVoidFunction* value){
  if(!name||!value)return XR_ERROR_VALIDATION_FAILURE;*value=nullptr;
#define ENTRY(n) if(std::strcmp(name,"xr" #n)==0){*value=reinterpret_cast<PFN_xrVoidFunction>(rt##n);return XR_SUCCESS;}
  ENTRY(GetInstanceProcAddr) ENTRY(EnumerateInstanceExtensionProperties) ENTRY(CreateInstance) ENTRY(DestroyInstance)
  ENTRY(GetInstanceProperties) ENTRY(ResultToString) ENTRY(StructureTypeToString) ENTRY(GetSystem) ENTRY(GetSystemProperties)
  ENTRY(GetOpenGLGraphicsRequirementsKHR) ENTRY(EnumerateEnvironmentBlendModes) ENTRY(EnumerateViewConfigurations) ENTRY(GetViewConfigurationProperties) ENTRY(EnumerateViewConfigurationViews)
  ENTRY(CreateSession) ENTRY(DestroySession) ENTRY(BeginSession) ENTRY(EndSession) ENTRY(RequestExitSession) ENTRY(PollEvent)
  ENTRY(EnumerateReferenceSpaces) ENTRY(CreateReferenceSpace) ENTRY(GetReferenceSpaceBoundsRect) ENTRY(CreateActionSpace) ENTRY(DestroySpace) ENTRY(LocateSpace) ENTRY(LocateViews)
  ENTRY(EnumerateSwapchainFormats) ENTRY(CreateSwapchain) ENTRY(DestroySwapchain) ENTRY(EnumerateSwapchainImages) ENTRY(AcquireSwapchainImage) ENTRY(WaitSwapchainImage) ENTRY(ReleaseSwapchainImage)
  ENTRY(WaitFrame) ENTRY(BeginFrame) ENTRY(EndFrame) ENTRY(StringToPath) ENTRY(PathToString) ENTRY(CreateActionSet) ENTRY(DestroyActionSet) ENTRY(CreateAction) ENTRY(DestroyAction)
  ENTRY(SuggestInteractionProfileBindings) ENTRY(AttachSessionActionSets) ENTRY(GetCurrentInteractionProfile) ENTRY(SyncActions) ENTRY(GetActionStatePose) ENTRY(GetActionStateFloat) ENTRY(GetActionStateBoolean) ENTRY(GetActionStateVector2f) ENTRY(ApplyHapticFeedback) ENTRY(StopHapticFeedback)
#undef ENTRY
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}
} // namespace
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loader,XrNegotiateRuntimeRequest* request){
  if(!loader||!request||loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize!=sizeof(XrNegotiateLoaderInfo)||request->structType!=XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST||request->structVersion!=XR_RUNTIME_INFO_STRUCT_VERSION||request->structSize!=sizeof(XrNegotiateRuntimeRequest)||loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||XR_VERSION_MAJOR(loader->maxApiVersion)<1)return XR_ERROR_INITIALIZATION_FAILED;
  request->runtimeInterfaceVersion=1;request->runtimeApiVersion=XR_CURRENT_API_VERSION;request->getInstanceProcAddr=rtGetInstanceProcAddr;return XR_SUCCESS;
}
