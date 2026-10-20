#include "link/walk.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace linkworld {
namespace {
bool finite(const Vec3& value){return std::all_of(value.begin(),value.end(),[](double component){return std::isfinite(component);});}
double input(double value){return std::isfinite(value)?std::clamp(value,-1.,1.):0.;}
bool inside(const Portal& portal,const Vec3& point){
  for(std::size_t axis=0;axis<3;++axis)if(point[axis]<portal.position[axis]-portal.size[axis]/2||point[axis]>portal.position[axis]+portal.size[axis]/2)return false;
  return true;
}
std::optional<double> entryTime(const Portal& portal,const Vec3& from,const Vec3& to){
  if(inside(portal,from))return std::nullopt;
  double first=0,last=1;
  for(std::size_t axis=0;axis<3;++axis){
    const auto minimum=portal.position[axis]-portal.size[axis]/2,maximum=portal.position[axis]+portal.size[axis]/2;
    const auto distance=to[axis]-from[axis];
    if(std::abs(distance)<1e-12){if(from[axis]<minimum||from[axis]>maximum)return std::nullopt;continue;}
    auto enter=(minimum-from[axis])/distance,exit=(maximum-from[axis])/distance;
    if(enter>exit)std::swap(enter,exit);
    first=std::max(first,enter);last=std::min(last,exit);
    if(first>last)return std::nullopt;
  }
  return first;
}
}

Vec3 advanceWalk(const WalkSettings& settings,Vec3 eye,Vec3 forward,double forwardInput,double rightInput,double dt){
  if(!finite(eye))eye={settings.min[0],settings.eyeHeight,settings.max[2]};
  if(!finite(forward))forward={0,0,-1};
  const auto length=std::hypot(forward[0],forward[2]);
  if(length<1e-12)forward={0,0,-1};else forward={forward[0]/length,0,forward[2]/length};
  auto forwardAmount=input(forwardInput),rightAmount=input(rightInput);
  const auto inputLength=std::hypot(forwardAmount,rightAmount);
  if(inputLength>1){forwardAmount/=inputLength;rightAmount/=inputLength;}
  const auto step=settings.speed*(std::isfinite(dt)?std::clamp(dt,0.,.1):0.);
  eye[0]=std::clamp(eye[0]+step*(forward[0]*forwardAmount-forward[2]*rightAmount),settings.min[0],settings.max[0]);
  eye[2]=std::clamp(eye[2]+step*(forward[2]*forwardAmount+forward[0]*rightAmount),settings.min[2],settings.max[2]);
  eye[1]=settings.eyeHeight;
  return eye;
}

const Portal* enteredPortal(const World& world,Vec3 from,Vec3 to){
  if(!finite(from)||!finite(to))return nullptr;
  const Portal* result=nullptr;auto earliest=std::numeric_limits<double>::infinity();
  for(const auto& portal:world.portals)if(const auto time=entryTime(portal,from,to);time&&*time<earliest){earliest=*time;result=&portal;}
  return result;
}
} // namespace linkworld
