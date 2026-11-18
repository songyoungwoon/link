#include "link/xr.hpp"
#include <stdexcept>
namespace linkworld {
struct OpenXrSession::Impl {};
OpenXrSession::OpenXrSession():impl_(std::make_unique<Impl>()){}
OpenXrSession::~OpenXrSession()=default;
void OpenXrSession::initialize(HDC,HGLRC){throw std::runtime_error("This build has no OpenXR support. Rebuild with -DLINK_OPENXR=ON.");}
void OpenXrSession::shutdown() noexcept{}
void OpenXrSession::pollEvents(){}
bool OpenXrSession::running() const noexcept{return false;}
bool OpenXrSession::focused() const noexcept{return false;}
const char* OpenXrSession::sessionStateName() const noexcept{return "DISABLED";}
bool OpenXrSession::exitRequested() const noexcept{return false;}
const std::string& OpenXrSession::runtimeName() const noexcept{static const std::string name;return name;}
const std::array<std::string,2>& OpenXrSession::interactionProfiles() const noexcept{static const std::array<std::string,2> profiles;return profiles;}
const std::string& OpenXrSession::bindingStatus() const noexcept{static const std::string status="OpenXR disabled";return status;}
OpenXrSession::Frame OpenXrSession::beginFrame(){return {};}
void OpenXrSession::bindEye(std::size_t){}
void OpenXrSession::mirror(int,int){}
void OpenXrSession::endFrame(bool){}
void OpenXrSession::abortFrame() noexcept{}
void OpenXrSession::requestExit(){}
void OpenXrSession::haptic(std::size_t,float,double) noexcept{}
}
