#include "link/runtime.hpp"
#include "link/model.hpp"
#include <cmath>
#include <iostream>
using namespace linkworld;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::shared_ptr<World> fixture(const std::string& script,bool extension=true){
  auto world=std::make_shared<World>();if(extension)world->extensions={"models-v1"};
  auto asset=std::make_shared<ModelAsset>();
  for(const auto& name:{"Idle","Walk","Attack"}){ModelAnimation clip;clip.name=name;clip.duration=1;asset->animations.push_back(clip);}
  world->models["hero.glb"]=asset;
  NodeDefinition controller;controller.id="controller";controller.kind=Kind::Component;controller.script="hero.script";controller.state={{"stop",false}};
  NodeDefinition hero;hero.id="hero";hero.kind=Kind::Element;hero.parent=0;hero.geometry.type=Geometry::Type::Model;hero.geometry.model="hero.glb";
  world->nodes={controller,hero};world->programs["hero.script"]=parseScript(script,"hero.script");return world;
}
void playback(){
  auto world=fixture(R"(on onStart(ctx){ctx.playAnimation("hero","Idle");}
on onInteract(ctx,event){ctx.playAnimation("hero","Attack",false,2);}
on onUpdate(ctx,deltaSeconds){let a=ctx.getState();if(a.stop==true){ctx.stopAnimation("hero");}})");
  Runtime runtime(world);runtime.start();
  require(runtime.find("hero").animation.clip=="Idle","onStart must choose a model clip");
  for(int i=0;i<30;++i)runtime.update(.05);
  require(std::abs(runtime.find("hero").animation.time-.5)<1e-8,"A looping clip wraps in seconds");
  runtime.interact("hero");
  require(runtime.find("hero").animation.clip=="Attack"&&runtime.find("hero").animation.time==0,"Ray interaction switches to attack");
  for(int i=0;i<12;++i)runtime.update(.05);
  require(!runtime.find("hero").animation.playing&&runtime.find("hero").animation.time==1,"Non-looping clip holds its final pose");
  runtime.interact("hero");require(runtime.find("hero").animation.playing&&runtime.find("hero").animation.time==0,"A completed attack may be replayed");
  runtime.find("controller").state["stop"]=true;runtime.update(.01);
  require(runtime.find("hero").animation.clip.empty()&&!runtime.find("hero").animation.playing,"stopAnimation resets to bind pose");
  Runtime repeated(fixture(R"(on onStart(ctx){ctx.playAnimation("hero","Walk",true,.5);} on onUpdate(ctx,deltaSeconds){ctx.playAnimation("hero","Walk",true,.5);})"));
  repeated.start();for(int i=0;i<10;++i)repeated.update(.04);
  require(std::abs(repeated.find("hero").animation.time-.2)<1e-8,"Repeated playback requests must not reset an active clip");
  repeated.update(-2);require(std::abs(repeated.find("hero").animation.time-.2)<1e-8,"Negative delta cannot rewind model playback");
}
void rejection(){
  for(const auto& script:{R"(on onStart(ctx){ctx.playAnimation("hero","Missing");})",R"(on onStart(ctx){ctx.playAnimation("controller","Idle");})",R"(on onStart(ctx){ctx.playAnimation("hero","Idle",true,0);})",R"(on onStart(ctx){ctx.playAnimation("hero","Idle",true,5);})",R"(on onStart(ctx){ctx.playAnimation("hero","Idle",1);})"}){
    int errors=0;Runtime runtime(fixture(script),[&](bool error,const std::string&){if(error)++errors;});runtime.start();require(errors==1&&runtime.activeBindings()==0,"Invalid animation request must disable only its binding");
  }
  int errors=0;Runtime disabled(fixture(R"(on onStart(ctx){ctx.playAnimation("hero","Idle");})",false),[&](bool error,const std::string&){if(error)++errors;});disabled.start();require(errors==1,"Animation APIs require explicit models-v1 opt-in");
}
void styles(){
  World world;NodeDefinition element;element.kind=Kind::Element;
  auto material=resolveStyle(element,world);require(!material.authoredRoughness&&!material.authoredMetalness,"Implicit primitive defaults must not erase glTF material factors");
  world.defaults={{"roughness",.4}};world.materials["hero"]={{"shading","toon"},{"outline",.012},{"metalness",.7}};element.style="hero";
  material=resolveStyle(element,world);require(material.shading=="toon"&&material.outline==.012&&material.authoredRoughness&&material.authoredMetalness,"Style cascade records explicit glTF overrides");
  for(const auto& patch:{StylePatch{{"shading","unknown"}},StylePatch{{"outline",-.1}},StylePatch{{"outline",.041}}}){bool rejected=false;try{validateStyle(patch);}catch(const Error&){rejected=true;}require(rejected,"Unsupported rendering styles must be rejected");}
}
}
int main(){try{playback();rejection();styles();std::cout<<"Model Script playback and material contract PASS\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
