#include "link/loader.hpp"
#include "link/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace linkworld;
void check(bool condition,const std::string& message){
  if(!condition)throw std::runtime_error("afterlight: "+message);
}
bool near(double value,double expected,double tolerance=.000001){return std::abs(value-expected)<=tolerance;}
Material material(const Runtime& runtime,const std::string& id){
  const auto& node=runtime.find(id);
  return runtime.material(static_cast<std::size_t>(&node-runtime.nodes().data()));
}
void checkInitial(const Runtime& runtime){
  check(!runtime.find("book_hinge").state.at("isOpen").boolean(),"a new book starts closed");
  check(near(runtime.find("book_hinge").transform.rotation[2],0),"a new cover starts flat");
  check(!runtime.find("lamp_control").state.at("isOn").boolean(),"a new lamp starts off");
  check(near(material(runtime,"lamp_bulb").emission,0),"an unlit bulb has no emission");
  check(!runtime.find("lamp_pool").transform.visible,"the switched-off lamp hides its baked light pool");
  check(near(runtime.storage("curtain_breeze").at("phase").number(),0),"a fresh breeze starts with a fresh phase");
}
}

void testAfterlight(){
  using namespace linkworld;
  const auto root=std::filesystem::path(u8"" LINK_SOURCE_DIR)/"worlds";
  const auto world=loadWorld("link://local/afterlight",root);
  check(world->id=="afterlight","the authored world loads through its LINK address");
  check(world->walk.has_value(),"the room enables first-person walking");
  check(near(world->eye[1],world->walk->eyeHeight),"the entry view starts at walking eye height");
  check(!world->grid,"the room hides the construction grid");
  check(!world->nodes.empty()&&world->nodes.size()<=Limits::nodes,"the room stays inside the native node budget");
  check(!world->images.empty()&&world->images.size()<=Limits::images,"the baked room stays inside the image-reference budget");
  check(world->programs.size()==3,"book, lamp, and breeze scripts all load");
  std::set<std::string> referencedImages;
  std::size_t bindingCount=0;
  for(const auto& node:world->nodes){
    if(!node.script.empty())++bindingCount;
    if(node.image.empty())continue;
    check(node.kind==Kind::Element&&node.geometry.type==Geometry::Type::Plane,"baked images belong to plane Elements");
    check(world->images.contains(node.image),"every textured surface resolves to a loaded image");
    referencedImages.insert(node.image);
  }
  check(bindingCount==3,"the three interactions each have one binding");
  check(referencedImages.size()==world->images.size(),"the image inventory contains exactly the referenced room assets");
  for(const auto& [reference,path]:world->images){
    check(std::filesystem::is_regular_file(path),"baked image exists: "+reference);
    const auto bytes=std::filesystem::file_size(path);
    check(bytes>0&&bytes<=Limits::imageBytes,"baked image fits its file budget: "+reference);
  }

  const auto initialCounters=counters();
  std::vector<std::string> errors;
  const auto log=[&](bool error,const std::string& message){if(error)errors.push_back(message);};
  {
    Runtime runtime(world,log);runtime.start();
    check(runtime.activeBindings()==3,"all bindings survive onStart");
    check(runtime.find("book_hinge").definition.kind==Kind::Component&&runtime.find("book_hinge").definition.script=="scripts/book.script","the book uses its own ordinary component Script");
    check(runtime.find("lamp_control").definition.kind==Kind::Component&&runtime.find("lamp_control").definition.script=="scripts/lamp.script","the lamp uses its own ordinary component Script");
    check(runtime.find("curtain_breeze").definition.kind==Kind::Component&&runtime.find("curtain_breeze").definition.script=="scripts/breeze.script","the curtain uses its own ordinary component Script");
    checkInitial(runtime);
    const auto curtainStart=runtime.find("curtain_breeze").transform;
    const auto firstPhase=runtime.storage("curtain_breeze").at("phase").number();
    double maximumCurtainDisplacement=0;
    const auto advance=[&](){
      runtime.update(.05);
      const auto& curtain=runtime.find("curtain_breeze").transform;
      double distanceSquared=0;
      for(std::size_t axis=0;axis<3;++axis){
        const auto displacement=curtain.position[axis]-curtainStart.position[axis];
        distanceSquared+=displacement*displacement;
      }
      const auto distance=std::sqrt(distanceSquared);
      maximumCurtainDisplacement=std::max(maximumCurtainDisplacement,distance);
      check(distance<=.009,"the curtain stays within nine millimetres of its authored position");
      check(curtain.rotation==curtainStart.rotation&&curtain.scale==curtainStart.scale,"the breeze moves only the curtain position");
    };

    check(runtime.interact("book_cover_top")=="book_hinge","clicking the textured cover reaches the book binding");
    check(runtime.find("book_hinge").state.at("isOpen").boolean(),"the first click opens book state");
    check(near(runtime.find("book_hinge").transform.rotation[2],0),"the click starts an animation without teleporting the cover");
    advance();
    check(runtime.storage("curtain_breeze").at("phase").number()>firstPhase,"the breeze store advances with elapsed time");
    auto previousAngle=runtime.find("book_hinge").transform.rotation[2];
    check(previousAngle>0&&previousAngle<135,"the cover interpolates through an intermediate opening angle");
    for(int frame=0;frame<119;++frame){
      advance();
      const auto angle=runtime.find("book_hinge").transform.rotation[2];
      check(angle>=previousAngle-.000001&&angle<=135.000001,"the cover opens monotonically without overshooting");
      previousAngle=angle;
    }
    check(near(previousAngle,135,.01),"the open cover reaches 135 degrees");
    check(runtime.interact("book_cover_top")=="book_hinge","a second cover click uses the same binding");
    check(!runtime.find("book_hinge").state.at("isOpen").boolean(),"the second click closes book state");
    advance();
    previousAngle=runtime.find("book_hinge").transform.rotation[2];
    check(previousAngle>0&&previousAngle<135,"closing also passes through an intermediate angle");
    for(int frame=0;frame<119;++frame){
      advance();
      const auto angle=runtime.find("book_hinge").transform.rotation[2];
      check(angle<=previousAngle+.000001&&angle>=-.000001,"the cover closes monotonically without overshooting");
      previousAngle=angle;
    }
    check(near(previousAngle,0,.01),"the second click returns the cover to its original angle");
    check(maximumCurtainDisplacement>=.004,"the curtain has a visible but restrained breeze");

    check(runtime.interact("lamp_toggle")=="lamp_control","clicking the switch reaches the lamp binding");
    check(runtime.find("lamp_control").state.at("isOn").boolean(),"the first switch click turns the lamp on");
    advance();
    check(near(material(runtime,"lamp_bulb").emission,1.6,.05),"the lit bulb uses the intended warm emission");
    check(runtime.find("lamp_pool").transform.visible,"turning the lamp on reveals its local baked light pool");
    runtime.interact("lamp_toggle");advance();
    check(!runtime.find("lamp_control").state.at("isOn").boolean(),"the second switch click turns the lamp off");
    check(near(material(runtime,"lamp_bulb").emission,0),"switching off clears bulb emission");
    check(!runtime.find("lamp_pool").transform.visible,"switching off hides the light pool");
    runtime.interact("curtain_breeze");
    check(runtime.activeBindings()==3,"all scripts survive update and interaction hooks");
    check(errors.empty(),errors.empty()?"script hooks are healthy":errors.front());
  }
  check(errors.empty(),errors.empty()?"destroy hooks are healthy":errors.front());
  check(counters().activeWorlds==initialCounters.activeWorlds&&counters().activeBindings==initialCounters.activeBindings,"the first room releases runtime and binding ownership");

  for(int reload=0;reload<4;++reload){
    Runtime runtime(loadWorld("link://local/afterlight",root),log);runtime.start();
    checkInitial(runtime);
    check(runtime.activeBindings()==3,"reloading recreates all three healthy bindings");
    runtime.interact("book_cover_top");runtime.interact("lamp_toggle");
    for(int frame=0;frame<20;++frame)runtime.update(.05);
    check(runtime.find("book_hinge").state.at("isOpen").boolean()&&runtime.find("lamp_control").state.at("isOn").boolean(),"each reload executes fresh interactive state");
    check(runtime.find("book_hinge").transform.rotation[2]>0,"each reload animates the newly opened book");
    check(runtime.find("lamp_pool").transform.visible,"each reload independently enables the lamp pool");
  }
  const auto finalCounters=counters();
  check(errors.empty(),errors.empty()?"all repeated lifecycle hooks are healthy":errors.front());
  check(finalCounters.activeWorlds==initialCounters.activeWorlds&&finalCounters.activeBindings==initialCounters.activeBindings,"repeated room reloads release every runtime and binding");
  check(finalCounters.createdWorlds-initialCounters.createdWorlds==5&&finalCounters.destroyedWorlds-initialCounters.destroyedWorlds==5,"all five room runtimes are destroyed exactly once");
}