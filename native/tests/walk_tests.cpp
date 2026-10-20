#include "link/walk.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error("walk test failed: "+message);}
bool close(double left,double right){return std::abs(left-right)<1e-10;}
}

void testWalk(){
  using namespace linkworld;
  WalkSettings walk;walk.min={-3,0,-30};walk.max={3,4,3};
  const Vec3 start{0,walk.eyeHeight,0},forward{0,.8,-.6};
  const auto straight=advanceWalk(walk,start,forward,1,0,.1);
  check(close(straight[2],-.34)&&straight[0]==0&&straight[1]==walk.eyeHeight,"camera pitch does not slow walking or change eye height");
  const auto diagonal=advanceWalk(walk,start,forward,1,1,.1);
  check(close(std::hypot(diagonal[0],diagonal[2]),.34)&&diagonal[0]>0&&diagonal[2]<0,"diagonal movement has the same speed and camera-relative right direction");
  const auto reversed=advanceWalk(walk,start,{0,0,1},1,1,.1);
  check(reversed[0]<0&&reversed[2]>0,"turning the camera reverses both horizontal movement axes");
  auto coarse=start,fine=start;
  for(int i=0;i<10;++i)coarse=advanceWalk(walk,coarse,forward,1,0,.1);
  for(int i=0;i<100;++i)fine=advanceWalk(walk,fine,forward,1,0,.01);
  check(close(coarse[2],fine[2])&&close(coarse[2],-walk.speed),"walking distance follows elapsed time across frame rates");
  check(advanceWalk(walk,start,forward,1,0,5)==straight,"a delayed frame cannot create a giant movement step");
  check(advanceWalk(walk,start,forward,1,0,-1)==start,"negative time causes no movement");
  check(advanceWalk(walk,start,forward,1,0,std::numeric_limits<double>::infinity())==start,"nonfinite time causes no movement");
  check(advanceWalk(walk,{2.99,8,-29.99},forward,1,1,.1)==Vec3{3,walk.eyeHeight,-30},"corridor walls and floor bounds constrain the camera eye");
  check(advanceWalk(walk,{100,0,-100},forward,0,0,.1)==Vec3{3,walk.eyeHeight,-30},"idle movement also restores walk bounds");
  check(advanceWalk(walk,start,{0,1,0},1,0,.1)==straight,"a vertical look direction falls back to a finite horizontal heading");
  check(advanceWalk(walk,start,forward,std::numeric_limits<double>::quiet_NaN(),0,.1)==start,"nonfinite movement input cannot corrupt the camera");

  World world;world.portals.push_back({"end","link://local/hello-world",{0,1.65,-5},{2,3.3,.1}});
  const auto* portal=&world.portals.front();
  check(enteredPortal(world,{0,1.65,-4},{0,1.65,-5})==portal,"walking into a portal returns its LINK definition");
  check(enteredPortal(world,{0,1.65,-4},{0,1.65,-6})==portal,"a segment crossing the complete thin portal still triggers");
  check(enteredPortal(world,{0,1.65,-5},{0,1.65,-5.01})==nullptr,"spawning or remaining inside a portal does not retrigger");
  check(enteredPortal(world,{0,1.65,-5},{0,1.65,-6})==nullptr,"leaving a portal does not retrigger");
  check(enteredPortal(world,{2,1.65,-4},{2,1.65,-6})==nullptr,"walking beside a portal does not trigger");
  check(enteredPortal(world,{0,5,-4},{0,5,-6})==nullptr,"a segment above a portal does not trigger");
  check(enteredPortal(world,{0,1.65,-4},{0,1.65,-4})==nullptr,"stationary eyes outside a portal do not trigger");
  check(enteredPortal(world,{0,1.65,-6},{0,1.65,-4})==portal,"volumes can be entered from either side");
  world.portals.push_back({"near","link://local/room-a",{0,1.65,-2},{2,3.3,.1}});
  check(enteredPortal(world,{0,1.65,0},{0,1.65,-6})==&world.portals.back(),"multiple crossings select the first volume along the movement rather than manifest order");
  check(enteredPortal(world,{0,1.65,0},{0,std::numeric_limits<double>::quiet_NaN(),-6})==nullptr,"nonfinite segments do not enter a portal");
  World room;check(!room.walk&&room.portals.empty()&&enteredPortal(room,start,straight)==nullptr,"existing worlds opt out of walking and portal behavior by default");
}
