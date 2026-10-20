#include "link/loader.hpp"
#include "link/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error("guide example test failed: "+message);}
bool near(double left,double right){return std::abs(left-right)<1e-9;}
}

void testGuides(){
  using namespace linkworld;
  const auto root=std::filesystem::path(u8"" LINK_SOURCE_DIR).parent_path()/"docs"/"guides"/"examples";
  const auto world=loadWorld("link://local/first-world",root);
  check(world->id=="first-world"&&world->programs.size()==1&&world->images.empty(),"the documented command resolves the complete example without outside assets");
  check(!world->walk&&world->portals.empty(),"the first-world fixture uses the documented orbit camera");
  const auto initial=counters();std::vector<std::string> errors,logs;
  const LogFn logger=[&](bool error,const std::string& message){(error?errors:logs).push_back(message);};
  {
    Runtime runtime(world,logger);runtime.start();runtime.start();
    check(runtime.activeBindings()==1&&runtime.storage("spinner").at("angle").number()==0,"onStart initializes the binding once");
    const auto visual=std::find_if(runtime.nodes().begin(),runtime.nodes().end(),[](const auto& node){return node.definition.id=="sample_cube";});
    check(visual!=runtime.nodes().end(),"the guide's visual ID exists");
    const auto index=static_cast<std::size_t>(visual-runtime.nodes().begin());
    const auto base=runtime.material(index);
    check(base.color=="#6CCFC1"&&base.roughness==.35&&base.metalness==.15,"the real example demonstrates material, class, and id precedence together");
    for(int i=0;i<20;++i)runtime.update(.05);
    check(near(runtime.find("spinner").transform.rotation[1],45)&&near(runtime.find("spinner").state.at("angle").number(),45),"one second of callbacks produces 45 degrees in both transform and observable state");
    check(runtime.find("spinner").transform.position==Vec3{0,1.55,0},"a rotation patch preserves the declared component position");
    runtime.update(1);
    check(near(runtime.storage("spinner").at("angle").number(),47.25),"long frames use the documented 0.05-second Script delta cap");
    check(runtime.interact("sample_cube")=="spinner","a cube click reaches its nearest scripted component");
    check(runtime.find("spinner").state.at("selected").boolean()&&runtime.find("spinner").state.at("clicks").number()==1&&runtime.material(index).color=="#F1AE66","the first click updates state and applies the configured color override");
    runtime.interact("sample_cube");
    check(!runtime.find("spinner").state.at("selected").boolean()&&runtime.storage("spinner").at("clicks").number()==2&&runtime.find("sample_cube").overrideStyle.empty(),"the second click clears the complete override");
    const auto restored=runtime.material(index);
    check(restored.color==base.color&&restored.roughness==base.roughness&&restored.metalness==base.metalness,"clearing the override restores the file's computed style");
    check(runtime.interact("floor")=="floor"&&runtime.storage("spinner").at("clicks").number()==2,"the static floor has no scripted interaction");
  }
  check(errors.empty(),"all four lifecycle hooks complete without a Script error");
  check(std::count_if(logs.begin(),logs.end(),[](const auto& message){return message.find("first-world ready")!=std::string::npos;})==1,"the example's start log appears once");
  check(std::count_if(logs.begin(),logs.end(),[](const auto& message){return message.find("first-world binding destroyed")!=std::string::npos;})==1,"onDestroy runs once on teardown");
  {
    Runtime reloaded(world,logger);reloaded.start();
    check(reloaded.find("spinner").state.at("clicks").number()==0&&reloaded.find("spinner").transform.rotation==Vec3{0,0,0}&&reloaded.find("sample_cube").overrideStyle.empty(),"creating a fresh instance resets state, store, transforms, and overrides");
  }
  const auto final=counters();
  check(errors.empty()&&final.activeWorlds==initial.activeWorlds&&final.activeBindings==initial.activeBindings&&final.createdWorlds==initial.createdWorlds+2&&final.destroyedWorlds==initial.destroyedWorlds+2,"both documented example instances release their runtime ownership");
}
