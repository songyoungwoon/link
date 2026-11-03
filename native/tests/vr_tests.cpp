#include "link/vr.hpp"
#include "link/loader.hpp"
#include "link/runtime.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace linkworld;
constexpr double pi=3.14159265358979323846;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool close(double a,double b,double epsilon=1e-9){return std::abs(a-b)<=epsilon;}
bool close(Vec3 a,Vec3 b,double epsilon=1e-9){return close(a[0],b[0],epsilon)&&close(a[1],b[1],epsilon)&&close(a[2],b[2],epsilon);}
double distance(Vec3 a,Vec3 b){return std::hypot(std::hypot(a[0]-b[0],a[1]-b[1]),a[2]-b[2]);}
void rejects(const std::function<void()>& operation){bool caught=false;try{operation();}catch(const std::invalid_argument&){caught=true;}check(caught,"Invalid VR math input must be rejected");}
World walkingWorld(){World world;world.eye={0,1.65,0};world.target={0,1.65,-1};world.walk=WalkSettings{};world.walk->min={-100,0,-100};world.walk->max={100,4,100};return world;}
VrInput activeInput(){VrInput input;input.focused=true;input.headTracked=true;input.moveActive=true;input.turnActive=true;return input;}
VrPose standing(){return {{0,1.82,0},{}};}
Vec3 ndc(const VrMatrix& matrix,Vec3 p){const auto q=matrix.point(p);const double w=matrix.m[3]*p[0]+matrix.m[7]*p[1]+matrix.m[11]*p[2]+matrix.m[15];return {q[0]/w,q[1]/w,q[2]/w};}
}

int main(){
  try{
    using namespace linkworld;
    {
      const VrPose yaw{{3,2,-4},vrQuaternionFromYaw(.73)};
      const VrPose pitch{{-.2,.3,.4},{std::sin(.21),0,0,std::cos(.21)}};
      const VrPose roll{{.1,-.1,.2},{0,0,std::sin(-.17),std::cos(-.17)}};
      const auto pose=vrCompose(vrCompose(yaw,pitch),roll);
      const auto view=vrViewMatrix(pose);const Vec3 local{.8,-.7,-2.4};
      check(close(view.point(pose.position),{0,0,0}),"View matrix maps the full six degree eye pose to the origin");
      check(close(view.point(vrTransformPoint(pose,local)),local),"View matrix is the inverse rigid pose, including pitch and roll");
      check(close(vrTransformPoint(vrInverse(pose),vrTransformPoint(pose,local)),local),"Pose inverse and composition agree independently of matrices");
      const auto roundTrip=vrCompose(pose,vrInverse(pose));
      check(close(roundTrip.position,{0,0,0})&&close(vrRotate(roundTrip.orientation,local),local),"Composed inverse is identity");
      VrPose scaled=pose;scaled.orientation.x*=2;scaled.orientation.y*=2;scaled.orientation.z*=2;scaled.orientation.w*=2;
      check(close(vrViewMatrix(scaled).point(vrTransformPoint(pose,local)),local),"Small quaternion normalization differences cannot scale the world");
      check(!vrValidPose({{0,0,0},{0,0,0,0}}),"A zero quaternion cannot become a tracked pose");
    }
    {
      const VrFov fov{-.93,.64,.78,-.71};const auto projection=vrProjectionMatrix(fov,.07,180);
      check(close(ndc(projection,{.07*std::tan(fov.angleLeft),0,-.07})[0],-1),"Asymmetric left frustum boundary projects to -1");
      check(close(ndc(projection,{.07*std::tan(fov.angleRight),0,-.07})[0],1),"Asymmetric right frustum boundary projects to 1");
      check(close(ndc(projection,{0,.07*std::tan(fov.angleUp),-.07})[1],1),"Upper frustum boundary has the OpenGL vertical sign");
      check(close(ndc(projection,{0,.07*std::tan(fov.angleDown),-.07})[1],-1),"Lower frustum boundary has the OpenGL vertical sign");
      check(close(ndc(projection,{0,0,-.07})[2],-1)&&close(ndc(projection,{0,0,-180})[2],1),"Projection maps the clipping planes to OpenGL depth");
      check(std::abs(projection.m[8])>.1,"Per-eye asymmetry remains present instead of a symmetric desktop FOV");
      rejects([&]{vrProjectionMatrix(fov,0,180);});rejects([&]{vrProjectionMatrix(fov,1,.5);});
      rejects([]{vrProjectionMatrix({-.7,.7,pi/2,-.7});});
      rejects([]{vrProjectionMatrix({.4,-.4,.7,-.7});});
    }
    {
      const VrRig rig{{5,0,-7},.91};const auto head=standing();
      const auto left=vrStageToWorld(rig,vrCompose(head,{{-.032,0,0},{}}));
      const auto right=vrStageToWorld(rig,vrCompose(head,{{.032,0,0},{}}));
      check(close(distance(left.position,right.position),.064),"Rig transforms preserve the runtime's physical IPD");
      check(close((left.position[1]+right.position[1])/2,head.position[1]),"Stereo eyes retain physical headset height");
      const auto ray=vrControllerRay({{2,0,3},-pi/2},{{.2,1.1,-.3},{}},true);
      check(ray&&close(ray->direction,{1,0,0})&&close(distance(ray->direction,{0,0,0}),1),"Controller aim uses the same rig and normalized -Z direction");
      check(!vrControllerRay(rig,head,false),"An inactive controller cannot produce an interaction ray");
      check(!vrControllerRay(rig,{{0,0,0},{0,0,0,0}},true),"Invalid controller orientation cannot dispatch a ray");
    }
    {
      auto world=walkingWorld();world.eye={4,1.65,9};world.target={4,1.65,-9};
      VrPose head{{.5,1.82,-.7},vrQuaternionFromYaw(.54)};
      const auto rig=vrResetRig(world,head);const auto initial=vrStageToWorld(rig,head);
      check(close(initial.position,{4,1.82,9}),"Entry places the tracked head horizontally while retaining actual height above the floor");
      check(close(vrRotate(initial.orientation,{0,0,-1}),{0,0,-1}),"Recenter accounts for the initial physical head yaw and world entry heading");
      head.position[1]=.91;check(close(vrStageToWorld(rig,head).position[1],.91),"Crouching preserves physical head height rather than desktop eyeHeight");
      World elevated; elevated.eye={7,6,9};elevated.target={0,1,0};
      head=standing();const auto elevatedRig=vrResetRig(elevated,head);
      check(close(vrStageToWorld(elevatedRig,head).position,elevated.eye),"Camera-only worlds keep their intended initial altitude");
      head.position[1]-=.4;check(close(vrStageToWorld(elevatedRig,head).position[1],5.6),"Camera-only worlds still retain subsequent physical vertical motion");
    }
    {
      auto world=walkingWorld();auto head=standing();auto input=activeInput();input.move={0,1};
      VrRig forward;check(vrAdvanceRig(forward,world,head,input,.05),"Active thumbstick movement advances the rig");
      check(close(forward.position,{0,0,-.17})&&close(vrStageToWorld(forward,head).position[1],1.82),"Artificial movement stays horizontal and preserves physical height");
      VrRig diagonal;input.move={1,1};vrAdvanceRig(diagonal,world,head,input,.05);
      check(close(distance(diagonal.position,{0,0,0}),.17),"Diagonal thumbstick movement has no speed advantage");
      input.move={0,1};head.orientation=vrQuaternionFromYaw(-pi/2);VrRig headRelative;vrAdvanceRig(headRelative,world,head,input,.05);
      check(close(headRelative.position,{.17,0,0}),"Movement is relative to the current head orientation");
      head=standing();head.orientation={std::sin(pi/3),0,0,std::cos(pi/3)};VrRig pitched;vrAdvanceRig(pitched,world,head,input,.05);
      check(close(distance(pitched.position,{0,0,0}),.17)&&pitched.position[1]==0,"Looking up or down never changes horizontal movement speed or altitude");
      head=standing();VrRig coarse,fine;for(int i=0;i<20;++i)vrAdvanceRig(coarse,world,head,input,.05);for(int i=0;i<100;++i)vrAdvanceRig(fine,world,head,input,.01);
      check(close(coarse.position,fine.position)&&close(coarse.position[2],-3.4),"Locomotion follows elapsed time across frame rates");
      VrRig paused;vrAdvanceRig(paused,world,head,input,8);check(close(paused.position,{0,0,-.17}),"Paused frames cannot produce a giant movement jump");
      VrRig invalid;vrAdvanceRig(invalid,world,head,input,std::numeric_limits<double>::quiet_NaN());vrAdvanceRig(invalid,world,head,input,-1);
      check(close(invalid.position,{0,0,0}),"Nonfinite and negative deltas are neutral");
      input.move={.1,.1};check(!vrAdvanceRig(invalid,world,head,input,.05),"Thumbstick jitter below the radial deadzone cannot move the world");
      input.move={std::numeric_limits<double>::quiet_NaN(),1};check(!vrAdvanceRig(invalid,world,head,input,.05),"A malformed input vector is neutral in both axes");
      input.move={0,1};input.moveActive=false;check(!vrAdvanceRig(invalid,world,head,input,.05),"An inactive move action immediately becomes neutral");
      World noWalk;input.moveActive=true;check(!vrAdvanceRig(invalid,noWalk,head,input,.05),"A world without walk settings permits room-scale motion only, not artificial translation");
      const auto before=vrStageToWorld(invalid,head);head.position={.3,1.2,.4};const auto after=vrStageToWorld(invalid,head);
      check(close(after.position,{.3,1.2,.4})&&!close(before.position,after.position),"Room-scale head tracking remains direct without artificial movement");
    }
    {
      auto world=walkingWorld();auto input=activeInput();const VrPose head{{1.3,1.76,-.4},vrQuaternionFromYaw(.2)};VrRig rig{{4,0,-6},0};
      const auto pivot=vrStageToWorld(rig,head).position;
      input.turn=1;check(!vrAdvanceRig(rig,world,head,input,.01),"A held stick cannot snap before a neutral sample after reset");
      input.turn=0;vrAdvanceRig(rig,world,head,input,.01);input.turn=1;vrAdvanceRig(rig,world,head,input,.01);
      check(close(rig.yaw,-pi/4)&&close(vrStageToWorld(rig,head).position,pivot),"Default right snap is 45 degrees around the actual tracked head");
      for(int i=0;i<90;++i)vrAdvanceRig(rig,world,head,input,.01);
      check(close(rig.yaw,-pi/4),"Holding a snap stick never repeats a turn");
      input.turn=.4;vrAdvanceRig(rig,world,head,input,.01);input.turn=-1;vrAdvanceRig(rig,world,head,input,.01);
      check(close(rig.yaw,-pi/4),"Snap hysteresis requires returning to the neutral release region");
      input.turn=0;vrAdvanceRig(rig,world,head,input,.01);input.turn=-1;vrAdvanceRig(rig,world,head,input,.01);
      check(close(rig.yaw,0)&&close(vrStageToWorld(rig,head).position,pivot),"Neutral rearms the opposite turn without orbiting the user");
      input.focused=false;check(!vrAdvanceRig(rig,world,head,input,.01),"Focus loss stops motion immediately");
      input.focused=true;check(!vrAdvanceRig(rig,world,head,input,.01),"Regaining focus with a held stick cannot unexpectedly snap");
      input.turn=0;vrAdvanceRig(rig,world,head,input,.01);input.turn=1;input.headTracked=false;
      check(!vrAdvanceRig(rig,world,head,input,.01)&&!rig.turnArmed,"Tracking loss neutralizes input and disarms turn latches");
      input.headTracked=true;check(!vrAdvanceRig(rig,world,head,input,.01),"Restored tracking still requires a neutral turn sample");
      input.turnActive=false;check(!vrAdvanceRig(rig,world,head,input,.01),"An inactive turn action cannot retain a latched turn");
      input.turnActive=true;input.turn=std::numeric_limits<double>::quiet_NaN();check(!vrAdvanceRig(rig,world,head,input,.01),"A nonfinite turn is safely neutral");
      VrLocomotionOptions smooth;smooth.turnMode=VrTurnMode::Smooth;smooth.smoothDegreesPerSecond=60;input.turn=1;
      vrAdvanceRig(rig,world,head,input,.05,smooth);
      check(close(rig.yaw,-pi/60)&&close(vrStageToWorld(rig,head).position,pivot),"Opt-in smooth turning is time based and preserves the same head pivot");
    }
    {
      auto world=walkingWorld();world.walk->min={-1,0,-1};world.walk->max={1,4,1};auto input=activeInput();input.move={1,0};VrPose head{{.9,1.7,0},{}};VrRig rig;
      vrAdvanceRig(rig,world,head,input,.05);check(close(vrStageToWorld(rig,head).position[0],1),"Artificial movement clamps the actual head, including its room-scale offset");
      head.position[0]=1.1;const auto physicalOutside=vrStageToWorld(rig,head).position;input.move={0,0};
      check(!vrAdvanceRig(rig,world,head,input,.05)&&close(vrStageToWorld(rig,head).position,physicalOutside),"A physical step beyond a virtual boundary never snaps the world beneath the head");
      input.move={1,0};check(!vrAdvanceRig(rig,world,head,input,.05),"Artificial outward movement is blocked when physically beyond the edge");
      input.move={-1,0};const auto before=vrStageToWorld(rig,head).position;vrAdvanceRig(rig,world,head,input,.05);
      check(close(before[0]-vrStageToWorld(rig,head).position[0],.17),"Returning from outside the boundary uses the ordinary limited movement step");
      VrPose invalidHead=head;invalidHead.orientation={0,0,0,0};check(!vrAdvanceRig(rig,world,invalidHead,input,.05),"Unusable head orientation cannot move the rig");
    }
    {
      const auto world=loadWorld("link://local/vr-room",std::filesystem::path(u8"" LINK_SOURCE_DIR)/"worlds");
      check(world->walk.has_value()&&!world->portals.empty(),"VR reference room has bounded movement and a real portal");
      Runtime runtime(world);runtime.start();
      check(!runtime.find("beacon_control").state.at("active").boolean(),"VR interaction starts in declared state");
      check(runtime.interact("beacon_sphere")=="beacon_control","Controller hit uses the existing nearest Script binding");
      check(runtime.find("beacon_control").state.at("active").boolean(),"VR reference interaction changes Script state");
      const auto& nodes=runtime.nodes();const auto index=static_cast<std::size_t>(&runtime.find("beacon_sphere")-nodes.data());
      check(runtime.material(index).color=="#FFD18B","VR reference interaction changes the displayed material");
      runtime.interact("beacon_sphere");check(!runtime.find("beacon_control").state.at("active").boolean(),"Repeated controller interaction restores the beacon");
    }
    std::cout<<"PASS VR rigid poses, asymmetric stereo, physical height/IPD, controller safety, head-relative locomotion and comfort turns\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL VR: "<<error.what()<<'\n';return 1;}
}