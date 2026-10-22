#include "link/loader.hpp"
#include "link/particles.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <string>

namespace {
using namespace linkworld;
void check(bool result,const std::string& text){if(!result)throw std::runtime_error("particles: "+text);}
void rejects(const std::function<void()>& operation,const std::string& expected){
  try{operation();}catch(const Error& error){check(std::string(error.what()).find(expected)!=std::string::npos,"expected '"+expected+"', got: "+error.what());return;}
  throw std::runtime_error("particles: expected rejection containing '"+expected+"'");
}
void write(const std::filesystem::path& path,const std::string& source){
  std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<source;
  if(!stream)throw std::runtime_error("could not write particle fixture");
}
struct Fixture {
  std::filesystem::path directory,worlds,room;
  Fixture(){
    const auto native=std::filesystem::absolute(std::filesystem::path(u8"" LINK_SOURCE_DIR)).lexically_normal();
    directory=native/(".particle-test-data-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(directory.parent_path()==native,"fixture remains under native source");worlds=directory/"worlds";room=worlds/"particles";reset();
  }
  ~Fixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
  void manifest(const std::string& extension="extensions:[\"particles-v1\"];"){
    write(room/"world.link","world {version:\"0.1\";id:\"particles\";name:\"Particle test\";elements:\"scene.element\";style:\"appearance.style\";scripts:[];entry:{position:[0,2,6],target:[0,0,0]};"+extension+"}");
  }
  void scene(const std::string& geometry="{type:\"particles\",capacity:16,radius:.04,cellSize:.6}",const std::string& extra=""){
    write(room/"scene.element","scene {object experiment {component behavior {element cloud {geometry:"+geometry+";"+extra+"}}}}");
  }
  void reset(){manifest();scene();write(room/"appearance.style","defaults {color:\"#45acde\";emission:.3;}");}
  std::shared_ptr<const World> load(){return loadWorld("link://local/particles",worlds);}
};
World makeWorld(std::size_t capacity=256,std::size_t systems=1,double width=1){
  World world;world.extensions={"particles-v1"};world.defaults={{"color","#45acde"}};
  for(std::size_t index=0;index<systems;++index){
    NodeDefinition node;node.id="cloud"+std::to_string(index);node.kind=Kind::Element;node.geometry.type=Geometry::Type::Particles;
    node.geometry.capacity=capacity;node.geometry.radius=.04;node.geometry.cellSize=width;world.nodes.push_back(node);
  }
  return world;
}
Value::Map spawnAt(double x,double y=0,double z=0){return {{"position",vecValue({x,y,z})}};}
bool near(double a,double b){return std::abs(a-b)<1e-10;}
}
void testParticles(){
  using namespace linkworld;
  {
    Fixture fixture;const auto world=fixture.load();
    check(world->extensions==std::vector<std::string>{"particles-v1"},"extension survives loading");
    check(world->nodes.back().geometry.type==Geometry::Type::Particles&&world->nodes.back().geometry.capacity==16,"strict particle geometry loads");
    check(near(world->nodes.back().geometry.cellSize,.6),"cell size survives loading");
    fixture.manifest("");rejects([&]{fixture.load();},"requires the particles-v1");
    fixture.manifest("extensions:[\"particles-v1\",\"particles-v1\"];");rejects([&]{fixture.load();},"duplicate");
    fixture.manifest("extensions:[\"particles-v2\"];");rejects([&]{fixture.load();},"unsupported world extension");
    fixture.manifest("extensions:[0];");rejects([&]{fixture.load();},"nonempty strings");
    fixture.manifest();
    for(const auto& geometry:std::vector<std::string>{
      "{type:\"particles\",capacity:0,radius:.04,cellSize:.6}",
      "{type:\"particles\",capacity:2049,radius:.04,cellSize:.6}",
      "{type:\"particles\",capacity:1.5,radius:.04,cellSize:.6}",
      "{type:\"particles\",capacity:true,radius:.04,cellSize:.6}"}){
      fixture.scene(geometry);rejects([&]{fixture.load();},"capacity");
    }
    fixture.scene("{type:\"particles\",capacity:1,radius:0,cellSize:.6}");rejects([&]{fixture.load();},"radius");
    fixture.scene("{type:\"particles\",capacity:1,radius:.04,cellSize:0}");rejects([&]{fixture.load();},"cellSize");
    fixture.scene("{type:\"particles\",capacity:1,radius:.04}");rejects([&]{fixture.load();},"cellSize");
    fixture.scene("{type:\"particles\",capacity:1,radius:.04,cellSize:.6,unknown:1}");rejects([&]{fixture.load();},"unsupported field");
    fixture.scene("{type:\"particles\",capacity:1,radius:.04,cellSize:.6}","text:\"not a surface\";");rejects([&]{fixture.load();},"require an Element plane");
    std::string scene="scene {object experiment {component behavior {";
    for(int index=0;index<5;++index)scene+="element cloud"+std::to_string(index)+" {geometry:{type:\"particles\",capacity:2048,radius:.04,cellSize:.6};}";
    scene+="}}}";write(fixture.room/"scene.element",scene);rejects([&]{fixture.load();},"capacity budget");
  }
  {
    auto world=makeWorld(3);ParticleEngine engine(world);
    rejects([&]{engine.spawn("cloud0",spawnAt(0));},"active simulation phase");
    engine.beginStep();rejects([&]{engine.beginStep();},"already active");
    const auto first=engine.spawn("cloud0",{{"position",vecValue({0,0,0})},{"velocity",vecValue({1,0,0})},{"state",Value::Map{{"energy",1}}}}).value();
    const auto second=engine.spawn("cloud0",spawnAt(.5)).value();
    check(first==1&&second==2,"identifiers are deterministic and monotonically assigned");
    check(engine.count("cloud0")==2&&engine.ids("cloud0").empty()&&!engine.get("cloud0",first),"spawn reservations count but remain absent from frozen reads");
    engine.endStep(0);
    check(engine.live(0).size()==2&&engine.live(100).empty(),"only particle Elements expose renderer instances");
    check(engine.get("cloud0",first)->color=="#45acde"&&near(engine.get("cloud0",first)->radius,.04),"spawn defaults resolve from geometry and Style");
    const auto exported=particleValue(engine.get("cloud0",first).value()).map();
    check(exported.at("id").number()==first&&exported.at("state").map().at("energy").number()==1,"script values export the full copied particle state");
    engine.beginStep();
    engine.set("cloud0",first,{{"velocity",vecValue({2,0,0})},{"state",Value::Map{{"energy",.5}}}});
    check(engine.get("cloud0",first)->velocity[0]==1&&engine.get("cloud0",first)->state.at("energy").number()==1,"staged field and state writes cannot leak into reads");
    engine.remove("cloud0",second);
    check(engine.count("cloud0")==1&&engine.ids("cloud0").size()==2&&engine.get("cloud0",second).has_value(),"removed particles remain visible to this phase, but release capacity");
    const auto third=engine.spawn("cloud0",spawnAt(-.5)).value();
    check(engine.neighbors("cloud0",first,1,64)==std::vector<std::uint32_t>{second},"neighbor reads exclude new spawns and include pending removals");
    engine.endStep(.25);
    check(near(engine.get("cloud0",first)->position[0],.5)&&near(engine.get("cloud0",first)->age,.25),"velocity integration and age advance once at commit");
    check(!engine.get("cloud0",second)&&engine.get("cloud0",third).has_value(),"commit applies removals and spawns together");
    engine.beginStep();engine.spawn("cloud0",spawnAt(0));
    check(!engine.spawn("cloud0",spawnAt(0))&&engine.count("cloud0")==3,"full capacity returns null without changing reservations");
    engine.abortStep();check(engine.count("cloud0")==2,"abort restores committed particle contents");
    engine.beginStep();const auto fifth=engine.spawn("cloud0",spawnAt(0)).value();engine.endStep(0);
    check(fifth==5,"aborted IDs are not reused");
    engine.beginStep();
    rejects([&]{engine.set("cloud0",first,{{"position",vecValue({9,0,0})},{"radius",-1}});},"radius");
    rejects([&]{engine.set("cloud0",first,{{"state",Value::Map{{"big",std::string(1024,'x')}}}});},"state byte");
    rejects([&]{engine.set("cloud0",first,{{"position",Value::Array{std::numeric_limits<double>::infinity(),0,0}}});},"finite");
    rejects([&]{engine.set("cloud0",first,{{"age",2}});},"unsupported particle field");
    rejects([&]{engine.set("cloud0",first,{{"color","red"}});},"#RRGGBB");
    rejects([&]{engine.remove("cloud0",999);},"unknown live particle");
    rejects([&]{engine.get("missing",first);},"unknown particle system");
    rejects([&]{engine.neighbors("cloud0",first,1.01,64);},"cellSize");
    rejects([&]{engine.neighbors("cloud0",first,1,0);},"limit");
    rejects([&]{engine.endStep(std::numeric_limits<double>::quiet_NaN());},"time step");
    engine.endStep(0);
    check(near(engine.get("cloud0",first)->position[0],.5)&&engine.get("cloud0",first)->state.at("energy").number()==.5,"every invalid mutation leaves validated committed fields unchanged");
  }
  {
    auto world=makeWorld();ParticleEngine engine(world);engine.beginStep();
    const auto id=engine.spawn("cloud0",{{"position",vecValue({0,0,0})},{"state",Value::Map{{"energy",1}}}}).value();engine.endStep(0);
    engine.beginStep();
    const Value::Map wide{{"values",Value::Array(Limits::particleStateValues-1)}};
    rejects([&]{engine.set("cloud0",id,{{"state",wide}});},"value count budget");
    rejects([&]{engine.spawn("cloud0",{{"position",vecValue({1,0,0})},{"state",wide}});},"value count budget");
    check(engine.count("cloud0")==1,"invalid wide state does not reserve capacity");
    Value tooDeep=0;
    for(std::size_t depth=0;depth<=Limits::particleStateDepth;++depth)tooDeep=Value::Map{{"n",std::move(tooDeep)}};
    rejects([&]{engine.set("cloud0",id,{{"state",tooDeep}});},"depth budget");
    engine.endStep(0);
    check(engine.get("cloud0",id)->state==Value::Map{{"energy",1}},"rejected structural state changes leave the particle unchanged");
    engine.beginStep();
    // Root map + array + 62 nulls reaches exactly 64 Value objects.
    const Value::Map allowedWide{{"values",Value::Array(Limits::particleStateValues-2)}};
    engine.set("cloud0",id,{{"state",allowedWide}});engine.endStep(0);
    check(engine.get("cloud0",id)->state==allowedWide,"the exact state value-count boundary is accepted");
    Value allowedDeep=0;
    for(std::size_t depth=0;depth<Limits::particleStateDepth;++depth)allowedDeep=Value::Map{{"n",std::move(allowedDeep)}};
    engine.beginStep();engine.set("cloud0",id,{{"state",allowedDeep}});engine.endStep(0);
    check(Value(engine.get("cloud0",id)->state)==allowedDeep,"the exact state depth boundary is accepted");
  }
  {
    auto world=makeWorld(32);ParticleEngine engine(world);engine.beginStep();
    const auto center=engine.spawn("cloud0",spawnAt(-.02,-.02,-.02)).value();
    const auto left=engine.spawn("cloud0",spawnAt(-.52,-.02,-.02)).value();
    const auto right=engine.spawn("cloud0",spawnAt(.48,-.02,-.02)).value();
    engine.spawn("cloud0",spawnAt(1.1));
    const auto diagonal=engine.spawn("cloud0",spawnAt(-.5,-.5,-.5)).value();
    engine.endStep(0);engine.beginStep();
    check(engine.neighbors("cloud0",center,1,64)==std::vector<std::uint32_t>{left,right,diagonal},"grid covers negative coordinates and adjacent cells, sorted by distance then ID");
    check(engine.neighbors("cloud0",center,1,1)==std::vector<std::uint32_t>{left},"neighbor limits retain the closest deterministic tie winner");
    check(engine.neighbors("cloud0",center,.01,64).empty(),"exact radius tests remove distant bucket candidates");
    engine.endStep(0);
    auto tinyWorld=makeWorld(4,1,1e-300);ParticleEngine tiny(tinyWorld);tiny.beginStep();
    const auto origin=tiny.spawn("cloud0",spawnAt(1e6)).value();const auto neighbor=tiny.spawn("cloud0",spawnAt(1e6)).value();
    tiny.endStep(0);tiny.beginStep();check(tiny.neighbors("cloud0",origin,1e-300,64)==std::vector<std::uint32_t>{neighbor},"tiny cell sizes remain bounded without coordinate conversion overflow");tiny.endStep(0);
  }
  {
    auto world=makeWorld(300);ParticleEngine engine(world);engine.beginStep();
    for(std::size_t index=0;index<Limits::particleSpawns;++index)engine.spawn("cloud0",spawnAt(0));
    rejects([&]{engine.spawn("cloud0",spawnAt(0));},"spawn budget");check(engine.count("cloud0")==256,"spawn exhaustion adds no particle");engine.endStep(0);
    engine.beginStep();
    const auto id=engine.ids("cloud0").front();
    for(std::size_t index=0;index<Limits::particleMutations;++index)engine.set("cloud0",id,{{"radius",.03}});
    rejects([&]{engine.remove("cloud0",id);},"mutation budget");check(engine.count("cloud0")==256,"mutation exhaustion does not remove a particle");engine.endStep(0);
    engine.beginStep();
    const auto calls=Limits::particleCandidateChecks/256;
    for(std::size_t index=0;index<calls;++index)check(engine.neighbors("cloud0",id,1,64).size()==64,"dense neighbor query remains bounded");
    rejects([&]{engine.neighbors("cloud0",id,1,64);},"candidate budget");engine.endStep(0);
    engine.beginStep();check(engine.neighbors("cloud0",id,1,64).size()==64,"query work budget resets for the next phase");engine.endStep(0);
  }
  {
    auto world=makeWorld(2,2);ParticleEngine engine(world);engine.beginStep();
    const auto ordinary=engine.spawn("cloud0",{{"position",vecValue({0,0,0})},{"velocity",vecValue({1,0,0})}}).value();
    const auto edge=engine.spawn("cloud1",{{"position",vecValue({1e6,0,0})},{"velocity",vecValue({1,0,0})}}).value();
    engine.endStep(0);engine.beginStep();rejects([&]{engine.endStep(.1);},"integrated particle coordinate");engine.abortStep();
    check(engine.get("cloud0",ordinary)->position[0]==0&&engine.get("cloud0",ordinary)->age==0&&engine.get("cloud1",edge)->position[0]==1e6,"failed integration does not partially commit any system");
    world.extensions.clear();rejects([&]{ParticleEngine invalid(world);},"requires the particles-v1");
    auto tooLarge=makeWorld(2048,5);rejects([&]{ParticleEngine invalid(tooLarge);},"capacity budget");
    auto badCapacity=makeWorld(2049);rejects([&]{ParticleEngine invalid(badCapacity);},"capacity");
  }
  {
    auto world=makeWorld();ParticleEngine first(world),second(world);
    for(auto* engine:{&first,&second}){
      engine->beginStep();for(int index=0;index<16;++index)engine->spawn("cloud0",{{"position",vecValue({index*.03,0,0})},{"velocity",vecValue({.1,.2,0})}});engine->endStep(0);
      for(int step=0;step<20;++step){engine->beginStep();for(const auto id:engine->ids("cloud0")){
        const auto neighbors=engine->neighbors("cloud0",id,.3,8);engine->set("cloud0",id,{{"state",Value::Map{{"neighbors",static_cast<double>(neighbors.size())}}}});
      }engine->endStep(.02);}
    }
    for(const auto& [id,particle]:first.live(0))check(particleValue(particle)==particleValue(second.live(0).at(id)),"equal inputs produce equal state over repeated phases");
    ParticleEngine fresh(world);check(fresh.ids("cloud0").empty(),"a new world owns no stale particles or state");fresh.beginStep();check(fresh.spawn("cloud0",spawnAt(0)).value()==1,"new runtime resets its isolated ID namespace");fresh.endStep(0);
  }
}