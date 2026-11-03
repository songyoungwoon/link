#include "link/vr.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace linkworld {
namespace {
constexpr double pi=3.14159265358979323846;
bool finite(Vec3 p){return std::all_of(p.begin(),p.end(),[](double v){return std::isfinite(v)&&std::abs(v)<=1e6;});}
Vec3 add(Vec3 a,Vec3 b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
Vec3 negate(Vec3 a){return {-a[0],-a[1],-a[2]};}
double quaternionLength(VrQuaternion q){return std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));}
VrQuaternion normalized(VrQuaternion q){
  const double length=quaternionLength(q);
  if(!std::isfinite(length)||length<1e-12)throw std::invalid_argument("VR orientation must be a finite nonzero quaternion");
  return {q.x/length,q.y/length,q.z/length,q.w/length};
}
VrQuaternion multiply(VrQuaternion a,VrQuaternion b){return {
  a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
  a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
  a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
  a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
void requirePose(const VrPose& pose){if(!vrValidPose(pose))throw std::invalid_argument("VR pose must have finite position and valid orientation");}
double wrapped(double angle){return std::remainder(angle,2*pi);}
double flatYaw(VrQuaternion q,double fallback){
  const auto forward=vrRotate(q,{0,0,-1});
  return std::hypot(forward[0],forward[2])>1e-6?std::atan2(-forward[0],-forward[2]):fallback;
}
double bounded(double value,double fallback,double low,double high){return std::clamp(std::isfinite(value)?value:fallback,low,high);}
double limitedMovement(double position,double delta,double low,double high){
  // A physical lean may already cross a virtual boundary. Never snap tracking
  // back: permit only an artificial movement toward the valid region.
  if(position<low)return std::clamp(delta,0.,high-position);
  if(position>high)return std::clamp(delta,low-position,0.);
  return std::clamp(position+delta,low,high)-position;
}
}

Vec3 VrMatrix::point(Vec3 v) const{return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12],m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]};}
Vec3 VrMatrix::direction(Vec3 v) const{return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};}
bool vrValidPose(const VrPose& pose) noexcept{
  const auto& q=pose.orientation;
  if(!finite(pose.position)||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w))return false;
  const double length=quaternionLength(q);return std::isfinite(length)&&length>1e-12;
}
VrQuaternion vrQuaternionFromYaw(double radians){
  if(!std::isfinite(radians))throw std::invalid_argument("VR yaw must be finite");
  const double half=wrapped(radians)/2;return {0,std::sin(half),0,std::cos(half)};
}
Vec3 vrRotate(VrQuaternion orientation,Vec3 value){
  const auto q=normalized(orientation);
  const Vec3 cross{q.y*value[2]-q.z*value[1],q.z*value[0]-q.x*value[2],q.x*value[1]-q.y*value[0]};
  return {value[0]+2*(q.w*cross[0]+q.y*cross[2]-q.z*cross[1]),value[1]+2*(q.w*cross[1]+q.z*cross[0]-q.x*cross[2]),value[2]+2*(q.w*cross[2]+q.x*cross[1]-q.y*cross[0])};
}
VrPose vrCompose(const VrPose& parent,const VrPose& child){requirePose(parent);requirePose(child);return {add(parent.position,vrRotate(parent.orientation,child.position)),normalized(multiply(normalized(parent.orientation),normalized(child.orientation)))};}
VrPose vrInverse(const VrPose& pose){requirePose(pose);const auto q=normalized(pose.orientation);const VrQuaternion inverse{-q.x,-q.y,-q.z,q.w};return {vrRotate(inverse,negate(pose.position)),inverse};}
Vec3 vrTransformPoint(const VrPose& pose,Vec3 point){requirePose(pose);return add(pose.position,vrRotate(pose.orientation,point));}
VrMatrix vrViewMatrix(const VrPose& worldEye){
  const auto inverse=vrInverse(worldEye);const auto q=inverse.orientation;VrMatrix result;
  result.m={1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y),0,
    2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x),0,
    2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y),0,
    inverse.position[0],inverse.position[1],inverse.position[2],1};
  return result;
}
VrMatrix vrProjectionMatrix(const VrFov& fov,double nearPlane,double farPlane){
  for(const double angle:{fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown})
    if(!std::isfinite(angle)||std::abs(angle)>=pi/2)throw std::invalid_argument("VR FOV angles must be finite and inside (-pi/2, pi/2)");
  if(!std::isfinite(nearPlane)||!std::isfinite(farPlane)||nearPlane<=0||farPlane<=nearPlane)throw std::invalid_argument("VR clipping planes must satisfy 0 < near < far");
  const auto left=std::tan(fov.angleLeft),right=std::tan(fov.angleRight),up=std::tan(fov.angleUp),down=std::tan(fov.angleDown);
  if(right<=left||up<=down)throw std::invalid_argument("VR FOV must have positive width and height");
  VrMatrix result;result.m.fill(0);result.m[0]=2/(right-left);result.m[5]=2/(up-down);
  result.m[8]=(right+left)/(right-left);result.m[9]=(up+down)/(up-down);
  result.m[10]=-(farPlane+nearPlane)/(farPlane-nearPlane);result.m[11]=-1;result.m[14]=-2*farPlane*nearPlane/(farPlane-nearPlane);
  return result;
}
VrPose vrStageToWorld(const VrRig& rig,const VrPose& stagePose){
  if(!finite(rig.position)||!std::isfinite(rig.yaw))throw std::invalid_argument("VR rig transform must be finite");
  return vrCompose({rig.position,vrQuaternionFromYaw(rig.yaw)},stagePose);
}
std::optional<VrRay> vrControllerRay(const VrRig& rig,const VrPose& stageAim,bool valid) noexcept{
  if(!valid||!vrValidPose(stageAim))return std::nullopt;
  try{const auto pose=vrStageToWorld(rig,stageAim);return VrRay{pose.position,vrRotate(pose.orientation,{0,0,-1})};}catch(...){return std::nullopt;}
}
VrRig vrResetRig(const World& world,const VrPose& initialStageHead){
  requirePose(initialStageHead);if(!finite(world.eye)||!finite(world.target))throw std::invalid_argument("VR world entry must be finite");
  const double dx=world.target[0]-world.eye[0],dz=world.target[2]-world.eye[2];
  const double desiredYaw=std::hypot(dx,dz)>1e-6?std::atan2(-dx,-dz):0;
  VrRig rig;rig.yaw=wrapped(desiredYaw-flatYaw(initialStageHead.orientation,0));
  const auto initial=vrRotate(vrQuaternionFromYaw(rig.yaw),initialStageHead.position);
  const double floor=world.walk?world.eye[1]-world.walk->eyeHeight:world.eye[1]-initialStageHead.position[1];
  if(!std::isfinite(floor))throw std::invalid_argument("VR entry floor must be finite");
  rig.position={world.eye[0]-initial[0],floor,world.eye[2]-initial[2]};return rig;
}

bool vrAdvanceRig(VrRig& rig,const World& world,const VrPose& stageHead,const VrInput& input,double deltaSeconds,const VrLocomotionOptions& options){
  if(!input.focused||!input.headTracked||!vrValidPose(stageHead)||!finite(rig.position)||!std::isfinite(rig.yaw)){rig.turnArmed=false;return false;}
  const double dt=std::isfinite(deltaSeconds)?std::clamp(deltaSeconds,0.,.05):0;
  const auto before=rig.position;const double beforeYaw=rig.yaw;
  const double activate=bounded(options.turnActivate,.7,.1,1),release=bounded(options.turnRelease,.25,0,activate-.01);
  double turn=0;
  if(input.turnActive&&std::isfinite(input.turn)){
    turn=std::clamp(input.turn,-1.,1.);
    if(std::abs(turn)<=release)rig.turnArmed=true;
  }else rig.turnArmed=false;
  double turnAngle=0;
  if(dt>0&&std::abs(turn)>release){
    if(options.turnMode==VrTurnMode::Snap){
      if(rig.turnArmed&&std::abs(turn)>=activate){turnAngle=-(turn>0?1.:-1.)*bounded(options.snapDegrees,45,0,180)*pi/180;rig.turnArmed=false;}
    }else{
      const double amount=(std::abs(turn)-release)/(1-release);
      turnAngle=-(turn>0?1.:-1.)*amount*bounded(options.smoothDegreesPerSecond,90,0,360)*pi/180*dt;
    }
  }
  if(turnAngle!=0){
    const auto pivot=vrStageToWorld(rig,stageHead).position;rig.yaw=wrapped(rig.yaw+turnAngle);
    const auto offset=vrRotate(vrQuaternionFromYaw(rig.yaw),stageHead.position);
    rig.position[0]=pivot[0]-offset[0];rig.position[2]=pivot[2]-offset[2];
  }
  if(world.walk&&input.moveActive&&dt>0&&std::isfinite(input.move[0])&&std::isfinite(input.move[1])){
    const auto& walk=*world.walk;
    if(finite(walk.min)&&finite(walk.max)&&walk.min[0]<walk.max[0]&&walk.min[2]<walk.max[2]&&std::isfinite(walk.speed)&&walk.speed>0){
      double right=std::clamp(input.move[0],-1.,1.),forward=std::clamp(input.move[1],-1.,1.);
      const double magnitude=std::hypot(right,forward),deadzone=bounded(options.moveDeadzone,.18,0,.95);
      if(magnitude>deadzone){
        const double amount=(std::min(magnitude,1.)-deadzone)/(1-deadzone);right=right/magnitude*amount;forward=forward/magnitude*amount;
        const auto head=vrStageToWorld(rig,stageHead);const double heading=flatYaw(head.orientation,rig.yaw);
        const double distance=std::min(walk.speed,20.)*dt;
        const double dx=distance*(-std::sin(heading)*forward+std::cos(heading)*right),dz=distance*(-std::cos(heading)*forward-std::sin(heading)*right);
        rig.position[0]+=limitedMovement(head.position[0],dx,walk.min[0],walk.max[0]);
        rig.position[2]+=limitedMovement(head.position[2],dz,walk.min[2],walk.max[2]);
      }
    }
  }
  return before!=rig.position||beforeYaw!=rig.yaw;
}
} // namespace linkworld