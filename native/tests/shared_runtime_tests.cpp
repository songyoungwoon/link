#include "link/runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace linkworld;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
  try{
    auto world=std::make_shared<World>();world->extensions={"shared-state-v1"};
    NodeDefinition controller;controller.id="controller";controller.kind=Kind::Component;controller.script="game.script";
    NodeDefinition plaque;plaque.id="hud";plaque.kind=Kind::Element;plaque.parent=0;plaque.geometry.type=Geometry::Type::Plane;plaque.text="Connecting";
    world->nodes={controller,plaque};
    world->programs["game.script"]=parseScript(R"(
on onStart(ctx){store ready=false;}
on onUpdate(ctx,deltaSeconds){
  let snapshot=ctx.getSharedState();
  if(snapshot!=null){ctx.setText("hud","HP "+str(snapshot.self.hp));ctx.setState({ready:true,view:ctx.getViewer().position});}
}
on onInteract(ctx,event){ctx.setState({queued:ctx.sendAction("attack",{target:"slime_0"})});}
)","shared-runtime.script");
    Runtime runtime(world);runtime.start();runtime.update(.016);
    check(runtime.takeTextChanges().empty(),"No shared state must leave the loading text intact");
    runtime.setViewerState(Value::Map{{"position",vecValue({1,1.65,2})}});
    runtime.setSharedState(Value::Map{{"self",Value::Map{{"hp",73}}}});
    runtime.update(.016);
    auto changes=runtime.takeTextChanges();check(changes.at("hud")=="HP 73","Shared state must update live text through Script");
    check(runtime.find("controller").state.at("ready").boolean(),"Snapshot-dependent logic must run");
    check(runtime.find("controller").state.at("view")==vecValue({1,1.65,2}),"Script must receive the locally tracked viewer position");
    runtime.update(.016);check(runtime.takeTextChanges().empty(),"Unchanged text must not cause repeated GPU work");
    std::size_t actions=0;
    runtime.setActionSink([&](const std::string& kind,Value::Map payload){check(kind=="attack"&&payload.at("target").string()=="slime_0","Actions must preserve the declared intent");++actions;return true;});
    runtime.interact("hud");check(actions==1&&runtime.find("controller").state.at("queued").boolean(),"Nested selection must enqueue one server action");
    runtime.setActionSink({});runtime.interact("hud");check(actions==1&&!runtime.find("controller").state.at("queued").boolean(),"A disconnected world must not report queued actions");
    auto oversized=Value::Map{{"data",std::string(Limits::stateBytes+1,'x')}};bool rejected=false;
    try{runtime.setSharedState(std::move(oversized));}catch(const std::exception&){rejected=true;}
    check(rejected,"Oversized remote state must be rejected before installation");
    std::cout<<"PASS shared runtime actions, viewer pose, live text, disconnect behavior and state budget\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL shared runtime: "<<error.what()<<'\n';return 1;}
}
