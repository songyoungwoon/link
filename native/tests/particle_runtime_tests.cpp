#include "link/runtime.hpp"
#include "link/loader.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
using namespace linkworld;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<World> fixture(const std::string& source,bool extension=true) {
  auto world=std::make_shared<World>();world->id="particles";world->address="link://local/particles";
  if(extension)world->extensions={"particles-v1"};
  NodeDefinition root;root.id="root";
  NodeDefinition controller;controller.id="controller";controller.kind=Kind::Component;controller.parent=0;controller.script="control.script";
  NodeDefinition cloud;cloud.id="cloud";cloud.kind=Kind::Element;cloud.parent=1;
  cloud.geometry.type=Geometry::Type::Particles;cloud.geometry.capacity=16;cloud.geometry.radius=.05;cloud.geometry.cellSize=1;
  if(extension)world->nodes={root,controller,cloud};else world->nodes={root,controller};
  world->programs["control.script"]=parseScript(source,"control.script");return world;
}
}
void testParticleRuntime() {
  using namespace linkworld;
  const auto before=counters();
  {
    auto world=fixture(R"(
on onStart(ctx) {
  store first = null;
  for (let i of range(3)) {
    let created = ctx.spawnParticle("cloud",{position:[i * .1,0,0],velocity:[1,0,0],state:{energy:1}});
    if (i == 0) { first = created; }
  }
  ctx.setState({startVisible:len(ctx.particleIds("cloud")),count:ctx.particleCount("cloud")});
}
on onUpdate(ctx,deltaSeconds) {
  let old = ctx.getParticle("cloud",first);
  ctx.setParticle("cloud",first,{position:[old.position[0] + 1,0,0]});
  ctx.setState({snapshotX:ctx.getParticle("cloud",first).position[0],neighbors:len(ctx.neighbors("cloud",first,1,8))});
}
on onInteract(ctx,event) {
  ctx.removeParticle("cloud",first);
  let born = ctx.spawnParticle("cloud",{position:[0,0,0]});
  ctx.setState({born:born,eventTarget:event.targetId});
}
on onDestroy(ctx) { ctx.log(ctx.particleCount("cloud")); }
)");
    std::vector<std::string> errors;
    Runtime runtime(world,[&](bool error,const std::string& message){if(error)errors.push_back(message);});
    runtime.start();
    check(runtime.particleCount()==3&&runtime.nodes().size()==3,"particles are allocated separately from the static scene tree");
    check(runtime.find("controller").state.at("startVisible").number()==0&&runtime.find("controller").state.at("count").number()==3,"spawn is staged and projected count includes reservations");
    runtime.update(3);
    check(runtime.find("controller").state.at("snapshotX").number()==0,"getParticle reads pre-step snapshot after a queued setter");
    check(runtime.find("controller").state.at("neighbors").number()==2,"Script can query bounded nearest neighbors");
    const auto moved=runtime.particleEngine()->get("cloud",1).value();
    check(std::abs(moved.position[0]-1.05)<1e-12&&moved.age==.05,"particle integration uses the same clamped frame delta");
    check(runtime.interact("cloud")=="controller","particle cloud click is delivered to its nearest owning Script");
    check(runtime.particleCount()==3&&!runtime.particleEngine()->get("cloud",1)&&runtime.find("controller").state.at("born").number()==4,"Script can replace a particle without reusing its ID");
    check(errors.empty(),"valid particle lifecycle has no Script faults");
  }
  {
    auto world=fixture(R"(
on onStart(ctx) { ctx.spawnParticle("cloud",{position:[0,0,0]}); }
on onUpdate(ctx,deltaSeconds) {
  ctx.setParticle("cloud",1,{position:[4,0,0]});
}
)");
    auto observer=world->nodes[1];observer.id="observer";observer.script="observe.script";
    world->nodes.push_back(observer);
    world->programs["observe.script"]=parseScript(R"(
on onUpdate(ctx,deltaSeconds) {
  ctx.setState({observed:ctx.getParticle("cloud",1).position[0]});
}
)","observe.script");
    Runtime runtime(world);runtime.start();runtime.update(.01);
    check(runtime.find("observer").state.at("observed").number()==0&&runtime.particleEngine()->get("cloud",1)->position[0]==4,"all bindings observe one coherent pre-step snapshot");
  }
  {
    std::vector<std::string> errors;
    Runtime runtime(fixture("on onStart(ctx) { ctx.particleIds(\"cloud\"); }",false),[&](bool error,const std::string& text){if(error)errors.push_back(text);});
    runtime.start();
    check(!runtime.particleEngine()&&runtime.activeBindings()==0&&errors.size()==1&&errors[0].find("requires extensions")!=std::string::npos,"particle API requires an explicit feature declaration");
  }
  {
    std::vector<std::string> errors;
    auto world=fixture(R"(
on onStart(ctx) { ctx.spawnParticle("cloud",{position:[1000000,0,0],velocity:[1,0,0]}); }
on onUpdate(ctx,deltaSeconds) { ctx.particleCount("cloud"); }
)");
    Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});
    runtime.start();runtime.update(.05);
    check(runtime.particleEngine()->get("cloud",1)->position[0]==1e6,"invalid integration rolls back the phase");
    runtime.update(.01);runtime.update(.01);
    check(errors.size()==2&&errors[0].find("reload to restart")!=std::string::npos&&runtime.activeBindings()==0,"invalid integration freezes the simulation and reports once rather than retrying each frame");
  }
  {
    auto world=fixture(R"(
on onStart(ctx) {
  ctx.spawnParticle("cloud",{position:[ctx.random(),ctx.random(),ctx.random()]});
}
)");
    Runtime first(world),second(world);first.start();second.start();
    check(particleValue(first.particleEngine()->get("cloud",1).value())==particleValue(second.particleEngine()->get("cloud",1).value()),"reload resets deterministic random state and particle IDs");
    first.update(.02);
    check(second.particleEngine()->get("cloud",1)->age==0,"separate visitors own independent particle state");
  }
  {
    auto world=fixture(R"(
on onStart(ctx) {
  ctx.setStyle("cloud",{color:"#FF00FF"});
  ctx.spawnParticle("cloud",{position:[0,0,0]});
}
on onInteract(ctx,event) {
  ctx.setStyle("cloud",{color:"#00FF00"});
  ctx.spawnParticle("cloud",{position:[0,0,0]});
}
)");
    Runtime runtime(world);runtime.start();runtime.interact("cloud");
    check(runtime.particleEngine()->get("cloud",1)->color=="#FF00FF"&&runtime.particleEngine()->get("cloud",2)->color=="#00FF00","omitted particle color captures current resolved Style at each spawn");
  }
  {
    // Four individually valid callbacks consume a shared phase allowance;
    // the fifth stops before an unbounded aggregate workload is possible.
    std::string source="on onStart(ctx) { store sum=0; for(let i of range(2048)) {";
    for(int i=0;i<6;++i)source+="sum=sum+1;";
    source+="} }";
    auto world=fixture(source);
    for(int index=1;index<5;++index){auto node=world->nodes[1];node.id="worker"+std::to_string(index);world->nodes.push_back(node);}
    std::vector<std::string> errors;
    Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});
    runtime.start();
    check(std::any_of(errors.begin(),errors.end(),[](const auto& text){return text.find("world evaluation step budget")!=std::string::npos;}),"extended Script budgets are aggregated across world bindings");
  }
  {
    const auto root=std::filesystem::path(u8"" LINK_SOURCE_DIR)/"worlds";
    auto world=loadWorld("link://local/particle-life",root);
    std::vector<std::string> errors;
    Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});
    runtime.start();
    check(runtime.particleCount()>40&&runtime.activeBindings()>0,"authored particle laboratory creates its initial population");
    const auto initial=runtime.particleEngine()->get("organisms",1);
    for(int frame=0;frame<120;++frame)runtime.update(1./60);
    check(errors.empty(),"particle lab Script stays within execution and query budgets: "+(errors.empty()?std::string():errors.front()));
    check(runtime.particleCount()>0,"laboratory remains populated after repeated simulation ticks");
    const auto after=runtime.particleEngine()->get("organisms",1);
    check(initial&&after&&initial->position!=after->position,"lab organisms move from their declared local interaction rules");
  }
  check(counters().activeWorlds==before.activeWorlds&&counters().activeBindings==before.activeBindings,"particle runtimes release bindings and world ownership");
}