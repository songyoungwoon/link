#include "link/loader.hpp"
#include "link/navigation.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

namespace {
using namespace linkworld;
void check(bool result,const std::string& description){if(!result)throw std::runtime_error(description);}
void wait(Navigator& nav){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(nav.loading()&&std::chrono::steady_clock::now()<end){nav.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(!nav.loading(),"Navigation completion timeout");}
std::shared_ptr<World> empty(const std::string& id){auto w=std::make_shared<World>();w->id=id;w->name=id;w->address="link://local/"+id;return w;}
}
void testRuntime(){
  using namespace linkworld;
  const auto root=std::filesystem::path(u8"" LINK_SOURCE_DIR)/"worlds";
  const auto roomA=loadWorld("link://local/room-a",root);
  const auto roomB=loadWorld("link://local/room-b",root);
  const auto initial=counters();
  {
    Runtime runtime(roomA);runtime.start();
    check(runtime.interact("door_panel")=="door_hinge","Child click must select closest binding");
    check(runtime.find("door_hinge").state.at("isOpen").boolean(),"Door state opens");
    for(int i=0;i<60;++i)runtime.update(.016);
    check(std::abs(runtime.find("door_hinge").transform.rotation[1])>20,"Door animation rotates parent component");
    check(std::abs(runtime.find("sphere_spin").transform.rotation[1])>1,"Sphere uses delta-time rotation");
    runtime.interact("door_panel");
    check(!runtime.find("door_hinge").state.at("isOpen").boolean(),"Repeated door click closes");
  }
  {
    Runtime runtime(roomB);runtime.start();runtime.interact("stone_soft_core");
    check(runtime.find("stone_soft").state.at("strength").number()==0,"Soft stone strength decreases from 10");
    check(runtime.find("stone_soft").state.at("broken").boolean(),"Soft stone breaks");
    check(runtime.find("stone_hard").state.at("strength").number()==100,"Hard stone remains independent");
    runtime.interact("stone_hard_core");
    check(runtime.find("stone_hard").state.at("strength").number()==90,"Hard stone loses only its strength");
    check(runtime.storage("stone_soft_body").at("hits").number()==1,"Soft instance storage");
    check(runtime.storage("stone_hard_body").at("hits").number()==1,"Hard instance storage");
    runtime.interact("stone_hard_core");
    check(runtime.storage("stone_soft_body").at("hits").number()==1&&runtime.storage("stone_hard_body").at("hits").number()==2,"Stores stay separated for shared script");
  }
  {
    const auto parent=std::filesystem::canonical(std::filesystem::current_path());
    const auto fixturePath=parent/("link-reload-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixturePath);
    struct Cleanup {std::filesystem::path path,parent;~Cleanup(){std::error_code error;if(path.parent_path()==parent&&path.filename().string().starts_with("link-reload-fixture-"))std::filesystem::remove_all(path,error);}} cleanup{fixturePath,parent};
    std::filesystem::copy(root/"room-a",fixturePath/"room-a",std::filesystem::copy_options::recursive);
    Runtime before(loadWorld("link://local/room-a",fixturePath));before.start();before.interact("door_panel");
    const auto original=before.material(0).color;
    {std::ofstream style(fixturePath/"room-a"/"appearance.style",std::ios::app);style<<"\nid door_panel { color: \"#ff00ff\"; }\n";}
    const auto sourcePath=fixturePath/"room-a"/"scripts"/"door.script";
    std::ifstream input(sourcePath);std::string source((std::istreambuf_iterator<char>(input)),{});input.close();
    const auto marker=source.find("store clicks = 0;");check(marker!=std::string::npos,"Expected script fixture marker");
    source.replace(marker,std::string("store clicks = 0;").size(),"store clicks = 40; ctx.setState({revision: 2});");
    {std::ofstream output(sourcePath);output<<source;}
    Runtime after(loadWorld("link://local/room-a",fixturePath));after.start();
    const auto& definitions=after.nodes();auto panel=std::find_if(definitions.begin(),definitions.end(),[](const auto& n){return n.definition.id=="door_panel";});
    check(panel!=definitions.end()&&after.material(static_cast<std::size_t>(panel-definitions.begin())).color=="#ff00ff","Style-only edits reload without recompilation");
    check(!after.find("door_hinge").state.at("isOpen").boolean()&&after.find("door_hinge").state.at("revision").number()==2,"Reload refreshes script source and initial state");
    after.interact("door_panel");check(after.storage("door_hinge").at("clicks").number()==41,"Changed script really executes");
    check(before.find("door_hinge").state.at("isOpen").boolean()&&before.material(0).color==original,"Staging reload does not mutate old instance");
  }
  std::vector<std::string> logs;
  auto fixture=empty("fixture");
  NodeDefinition object;object.id="root";
  NodeDefinition first;first.id="first";first.kind=Kind::Component;first.parent=0;first.script="bad.script";
  NodeDefinition second=first;second.id="second";second.script="good.script";
  fixture->nodes={object,first,second};
  fixture->programs["bad.script"]=parseScript("on onStart(ctx) { ctx.log(1 / 0); } on onDestroy(ctx) { ctx.log(\"destroy-bad\"); }","bad.script");
  fixture->programs["good.script"]=parseScript("on onStart(ctx) { store n = 0; } on onUpdate(ctx, deltaSeconds) { n = n + 1; ctx.setState({dt: deltaSeconds}); } on onDestroy(ctx) { ctx.log(\"destroy-good\"); }","good.script");
  {
    Runtime runtime(fixture,[&](bool,const std::string& message){logs.push_back(message);});runtime.start();
    check(runtime.activeBindings()==1,"Only failing script instance is disabled");
    runtime.update(12);check(runtime.find("second").state.at("dt").number()==.05,"Long delta is clamped to 50ms");
    check(runtime.storage("second").at("n").number()==1,"Healthy scripts continue");
  }
  check(logs.size()==3,"Failure log and onDestroy exactly once for both instances");
  check(logs[0].find("bad.script:")!=std::string::npos&&logs[0].find("node=first")!=std::string::npos,"Runtime errors include source position and binding node");
  for(int i=0;i<20;++i){Runtime runtime(i%2?roomA:roomB);runtime.start();runtime.update(.02);}
  check(counters().activeWorlds==initial.activeWorlds&&counters().activeBindings==initial.activeBindings,"20 transitions release runtime and binding ownership");
  {
    std::atomic<bool> oldStarted=false;
    Navigator nav(root,{},[&](const std::string& address,std::stop_token){
      if(address=="slow"){oldStarted=true;std::this_thread::sleep_for(std::chrono::milliseconds(60));return std::shared_ptr<const World>(empty("slow"));}
      if(address=="invalid")throw Error({"broken.element",3,8},"unknown field");
      if(address=="activation-failure"){auto world=empty(address);world->nodes={object,object};return std::shared_ptr<const World>(world);}
      return std::shared_ptr<const World>(empty(address));
    });
    nav.request("slow");
    const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(!oldStarted&&std::chrono::steady_clock::now()<timeout)std::this_thread::yield();
    check(oldStarted,"Delayed request actually started");
    nav.request("latest");wait(nav);check(nav.current()&&nav.current()->world().id=="latest","Late A cannot replace newer B");
    const auto* previous=nav.current();nav.request("invalid");wait(nav);
    check(nav.current()==previous&&!nav.error().empty(),"Preflight failure retains active world");
    nav.request("activation-failure");wait(nav);check(!nav.current()&&!nav.error().empty(),"Activation failure clears old world and recovers");
    nav.request("recovery");wait(nav);check(nav.current()&&nav.error().empty(),"Recover after failed activation");
    nav.request("slow");nav.cancel();std::this_thread::sleep_for(std::chrono::milliseconds(80));nav.poll();check(nav.current()->world().id=="recovery","Canceled loads cannot commit");
    nav.stop();check(!nav.current()&&!nav.loading(),"Stop drops all active state");
  }
  check(counters().activeWorlds==initial.activeWorlds&&counters().activeBindings==initial.activeBindings,"Navigator destructor releases live state");
}
