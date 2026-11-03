#pragma once
#include "core.hpp"

namespace linkworld {
// OpenXR coordinates: right handed, +Y up, -Z forward. Angles are radians.
struct VrQuaternion { double x=0,y=0,z=0,w=1; };
struct VrPose { Vec3 position{0,0,0};VrQuaternion orientation; };
struct VrFov { double angleLeft=-.7,angleRight=.7,angleUp=.7,angleDown=-.7; };
struct VrMatrix {
  // OpenGL column-major matrix; projection depth is [-1, 1].
  std::array<double,16> m{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  Vec3 point(Vec3) const;
  Vec3 direction(Vec3) const;
};
bool vrValidPose(const VrPose&) noexcept;
VrQuaternion vrQuaternionFromYaw(double radians);
Vec3 vrRotate(VrQuaternion,Vec3);
VrPose vrCompose(const VrPose& parent,const VrPose& child);
VrPose vrInverse(const VrPose&);
Vec3 vrTransformPoint(const VrPose&,Vec3);
VrMatrix vrViewMatrix(const VrPose& worldEye);
VrMatrix vrProjectionMatrix(const VrFov&,double nearPlane=.05,double farPlane=500);

struct VrRig {
  Vec3 position{0,0,0};double yaw=0;bool turnArmed=false;
};
struct VrRay { Vec3 origin,direction; };
VrPose vrStageToWorld(const VrRig&,const VrPose& stagePose);
std::optional<VrRay> vrControllerRay(const VrRig&,const VrPose& stageAim,bool valid=true) noexcept;
// Floor worlds retain physical headset height; elevated camera-only worlds retain
// the initial entry altitude and all subsequent physical vertical displacement.
VrRig vrResetRig(const World&,const VrPose& initialStageHead);

enum class VrTurnMode { Snap,Smooth };
struct VrLocomotionOptions {
  VrTurnMode turnMode=VrTurnMode::Snap;
  double snapDegrees=45,smoothDegreesPerSecond=90;
  double moveDeadzone=.18,turnActivate=.7,turnRelease=.25;
};
struct VrInput {
  bool focused=false,headTracked=false,moveActive=false,turnActive=false;
  // Left stick: +X right, +Y forward. Right stick: positive means turn right.
  std::array<double,2> move{0,0};double turn=0;
};
// No artificial translation without world.walk. Turns pivot on the actual head.
// Bounds constrain artificial movement, never modify a physical tracked pose.
// Lost focus/tracking/actions clear the snap latch; neutral input must rearm it.
// Invalid channels are neutral; time is finite and clamped to 0..0.05 seconds.
bool vrAdvanceRig(VrRig&,const World&,const VrPose& stageHead,const VrInput&,double deltaSeconds,const VrLocomotionOptions& = {});
} // namespace linkworld