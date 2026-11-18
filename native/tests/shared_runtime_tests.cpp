#include "link/runtime.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <functional>
using namespace linkworld;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void testSharedRuntime(){
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
}

namespace {
std::shared_ptr<World> inputFixture(const std::string& script,bool extension=true) {
  auto world=std::make_shared<World>();if(extension)world->extensions={"input-v1"};
  NodeDefinition controller;controller.id="status";controller.kind=Kind::Component;controller.script="status.script";
  world->nodes={controller};world->programs["status.script"]=parseScript(script,"status.script");return world;
}
const std::string statusScript=R"(
on onStart(ctx) { store opened=false;store presses=0;ctx.setTransform({visible:false}); }
on onInput(ctx,event) {
  if(event.action=="menu"&&event.phase=="pressed") {
    opened=!opened;presses=presses+1;
    ctx.setTransform({visible:opened});ctx.setState({presses:presses,event:event});
  }
}
)";
void testSemanticInput(){
  {
    Runtime runtime(inputFixture(statusScript));
    check(!runtime.input("menu"),"An unstarted runtime must not receive input");runtime.start();
    check(!runtime.find("status").transform.visible,"A status component starts hidden");
    std::size_t networkActions=0;runtime.setActionSink([&](const std::string&,Value::Map){++networkActions;return true;});
    check(!runtime.input("attack")&&!runtime.input("menu","hand")&&!runtime.input("menu","")&&!runtime.input(""),"Unsupported actions and sources must not enter Script");
    check(runtime.input("menu","keyboard")&&runtime.find("status").transform.visible,"A keyboard menu press opens the status component");
    const auto first=runtime.find("status").state.at("event").map();
    check(first.size()==3&&first.at("action").string()=="menu"&&first.at("phase").string()=="pressed"&&first.at("source").string()=="keyboard","Script receives only the semantic input envelope");
    for(int frame=0;frame<4;++frame)runtime.update(.016);
    check(runtime.find("status").transform.visible&&runtime.find("status").state.at("presses").number()==1,"Ordinary updates must not repeat a press");
    check(runtime.input("menu","controller")&&!runtime.find("status").transform.visible,"A second controller menu press closes the component");
    check(runtime.find("status").state.at("presses").number()==2&&runtime.find("status").state.at("event").map().at("source").string()=="controller","Input sources share one local toggle handler");
    check(networkActions==0,"Opening local UI must not generate automatic network actions");
  }
  {
    Runtime runtime(inputFixture(statusScript,false));runtime.start();
    check(!runtime.input("menu")&&!runtime.find("status").transform.visible,"The world must explicitly opt into input-v1");
  }
  {
    Runtime runtime(inputFixture("on onStart(ctx){ctx.setTransform({visible:false});}"));runtime.start();
    check(!runtime.input("menu"),"A Script without onInput must not count as a delivered event");
  }
  {
    auto world=inputFixture("on onStart(ctx){ctx.find(\"missing\");} on onInput(ctx,event){ctx.setState({received:true});}");
    std::vector<std::string> errors;Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});runtime.start();
    check(!runtime.input("menu")&&runtime.activeBindings()==0&&errors.size()==1,"A failed onStart binding must not receive input");
  }
  {
    auto world=inputFixture(statusScript);auto bad=world->nodes.front();bad.id="failed";bad.script="failed.script";world->nodes.push_back(bad);
    world->programs["failed.script"]=parseScript("on onInput(ctx,event){ctx.find(\"missing\");}","failed.script");
    std::vector<std::string> errors;Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});runtime.start();
    check(runtime.input("menu")&&runtime.find("status").transform.visible&&runtime.activeBindings()==1,"One invalid input handler must not stop another binding");
    check(errors.size()==1&&errors.front().find("hook=onInput")!=std::string::npos,"Input failures keep lifecycle diagnostics");
    check(runtime.input("menu")&&!runtime.find("status").transform.visible&&errors.size()==1,"A disabled input handler must not run or log repeatedly");
  }
  {
    auto world=inputFixture("on onInput(ctx,event){for(let i of range(2048)){for(let j of range(2048)){let sum=i+j;}}}");
    std::vector<std::string> errors;Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});runtime.start();
    check(runtime.input("menu")&&runtime.activeBindings()==0&&errors.size()==1&&errors.front().find("step budget")!=std::string::npos,"Input handlers retain bounded evaluation work");
    check(!runtime.input("menu")&&errors.size()==1,"A budget-disabled handler must stop receiving events");
  }
  {
    std::string script="on onStart(ctx){store total=0;} on onInput(ctx,event){for(let i of range(2048)){";
    for(int n=0;n<6;++n)script+="total=total+1;";script+="}}";
    auto world=inputFixture(script);
    for(int n=1;n<5;++n){auto worker=world->nodes.front();worker.id="worker"+std::to_string(n);world->nodes.push_back(worker);}
    std::vector<std::string> errors;Runtime runtime(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});runtime.start();runtime.input("menu");
    check(std::any_of(errors.begin(),errors.end(),[](const auto& text){return text.find("world evaluation step budget")!=std::string::npos;}),"Input fanout must share a world evaluation allowance");
  }
  {
    bool rejected=false;try{parseScript("on onInput(ctx){}","bad-input.script");}catch(const Error&){rejected=true;}
    check(rejected,"onInput requires exactly ctx,event parameters");
  }
}
}
int main(){
  try{testSharedRuntime();testSemanticInput();std::cout<<"PASS shared runtime, semantic input lifecycle, local UI toggles, failure isolation and budgets\n";return 0;}
  catch(const std::exception& error){std::cerr<<"FAIL shared runtime: "<<error.what()<<'\n';return 1;}
}
