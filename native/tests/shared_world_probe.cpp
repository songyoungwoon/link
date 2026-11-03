#include "link/loader.hpp"
#include "link/runtime.hpp"
#include "link/shared.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace {
using namespace linkworld;
using Clock=std::chrono::steady_clock;
void check(bool okay,const char* reason){if(!okay)throw std::runtime_error(reason);}
bool close(double a,double b){return std::abs(a-b)<1e-7;}
bool close(Vec3 a,Vec3 b){return close(a[0],b[0])&&close(a[1],b[1])&&close(a[2],b[2]);}
Value::Map viewer(Vec3 position){return {{"position",vecValue(position)},{"rotation",vecValue({0,0,0})},{"forward",vecValue({0,0,-1})},{"weaponPosition",vecValue({position[0]+.28,position[1]-.3,position[2]-.45})},{"weaponRotation",vecValue({4,12,-3})}};}
std::size_t nodeIndex(const Runtime& runtime,const std::string& id){const auto& nodes=runtime.nodes();const auto found=std::find_if(nodes.begin(),nodes.end(),[&](const auto& item){return item.definition.id==id;});check(found!=nodes.end(),"Required authored node is missing");return static_cast<std::size_t>(found-nodes.begin());}
std::string selectable(const Runtime& runtime,const std::string& owner){const auto parent=nodeIndex(runtime,owner);for(std::size_t index=0;index<runtime.nodes().size();++index){if(runtime.nodes()[index].definition.kind!=Kind::Element)continue;auto ancestor=runtime.nodes()[index].definition.parent;while(ancestor!=noParent){if(ancestor==parent)return runtime.nodes()[index].definition.id;ancestor=runtime.nodes().at(ancestor).definition.parent;}}throw std::runtime_error("Interactive authored binding has no Element descendant");}
std::filesystem::path utf8Path(const std::string& text){std::u8string encoded;for(unsigned char c:text)encoded+=static_cast<char8_t>(c);return std::filesystem::path(encoded);}
Value::Map waitSnapshot(SharedSession& session,Clock::time_point deadline){while(Clock::now()<deadline){if(auto state=session.poll())return std::move(*state);check(session.error().empty(),"Native shared session failed during join");std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Native shared session did not produce a snapshot");}
int run(const std::vector<std::string>& arguments){
  try{
    std::string root,address;
    for(std::size_t index=1;index<arguments.size();++index){if(arguments[index]=="--worlds"&&index+1<arguments.size())root=arguments[++index];else if(arguments[index]=="--address"&&index+1<arguments.size())address=arguments[++index];else throw std::runtime_error("Usage: shared_world_probe --worlds ROOT --address link://IPv4:port/sao");}
    check(!root.empty()&&!address.empty(),"Both world root and shared address are required");
    auto world=loadWorld("link://local/sao",utf8Path(root));check(world->nodes.size()<=Limits::nodes,"Authored world exceeds the existing node budget");
    check(std::find(world->extensions.begin(),world->extensions.end(),"shared-state-v1")!=world->extensions.end(),"Authored RPG must declare shared-state-v1");
    std::vector<std::string> errors;Runtime runtime(world,[&](bool error,const std::string& message){if(error)errors.push_back(message);});
    std::size_t expectedBindings=0;std::map<int,std::string> monsters,players;std::map<std::string,std::string> interactions;
    for(const auto& node:world->nodes){if(!node.script.empty())++expectedBindings;if(node.script=="scripts/monster.script")monsters.emplace(static_cast<int>(node.config.at("index").number()),node.id);if(node.script=="scripts/player.script")players.emplace(static_cast<int>(node.config.at("slot").number()),node.id);if(node.script=="scripts/interaction.script")interactions.emplace(node.config.at("target").string(),node.id);}
    check(monsters.size()==8&&players.size()==8,"Actual scene must provide eight monster and eight player bindings");
    runtime.setViewerState(viewer(world->eye));runtime.start();runtime.update(.02);
    check(runtime.activeBindings()==expectedBindings&&errors.empty(),"Authored bindings failed without a shared snapshot");
    for(const auto& [_,id]:monsters)check(!runtime.find(id).transform.visible,"Disconnected monster must be hidden");
    for(const auto& [_,id]:players)check(!runtime.find(id).transform.visible,"Disconnected avatar must be hidden");
    check(!runtime.find("weapon_control").transform.visible,"Disconnected held weapon must be hidden");
    const auto connecting=runtime.find("journey_status").definition.text;runtime.takeTextChanges();
    SharedSession first(address,"","World Probe One"),second(address,"","World Probe Two");
    auto state=waitSnapshot(first,Clock::now()+std::chrono::seconds(5));waitSnapshot(second,Clock::now()+std::chrono::seconds(5));
    Vec3 viewPosition=valueVec3(state.at("self").map().at("position"));viewPosition[2]+=2;const auto view=viewer(viewPosition);runtime.setViewerState(view);
    int snapshots=0,maximumPlayers=0;std::map<std::string,std::string> changedText;
    const auto started=Clock::now();
    for(int frame=0;frame<100;++frame){
      if(auto fresh=first.poll()){state=std::move(*fresh);++snapshots;}
      int active=0;for(const auto& item:state.at("players").array())if(item.map().at("active").boolean())++active;maximumPlayers=std::max(maximumPlayers,active);
      runtime.setSharedState(state);runtime.update(.02);for(auto&& [id,text]:runtime.takeTextChanges())changedText[id]=std::move(text);
      check(errors.empty()&&runtime.activeBindings()==expectedBindings,"Authored Script failed while processing a real server snapshot");
      std::this_thread::sleep_until(started+std::chrono::milliseconds((frame+1)*20));
    }
    check(snapshots>=5&&maximumPlayers==2,"Two real shared connections must populate the authored world");
    check(first.error().empty()&&second.error().empty(),"Shared connection failed during authored-world updates");
    const auto& self=state.at("self").map();const auto selfId=self.at("id").string();int otherSlot=-1;
    for(const auto& item:state.at("players").array()){
      const auto& player=item.map();const auto slot=static_cast<int>(player.at("slot").number());const bool expected=player.at("active").boolean()&&player.at("id").string()!=selfId;
      check(runtime.find(players.at(slot)).transform.visible==expected,"Authored avatar visibility must hide self and inactive slots");
      if(expected)otherSlot=slot;
    }
    check(otherSlot>=0,"Another connected avatar must remain visible away from the viewer");
    for(const auto& item:state.at("monsters").array()){
      const auto& monster=item.map();const auto id=monster.at("id").string();const bool alive=monster.at("active").boolean()&&monster.at("hp").number()>0;
      check(runtime.find(id).transform.visible==alive,"Monster visibility must follow the authoritative snapshot");
      const auto hp=std::clamp(monster.at("hp").number()/monster.at("maxHp").number(),.001,1.);
      check(close(runtime.find(id+"_hp").transform.scale[0],hp),"Monster health bar must reflect server health");
    }
    check(runtime.find("weapon_control").transform.visible,"Healthy connected player must have a held weapon");
    check(close(runtime.find("weapon_control").transform.position,valueVec3(view.at("weaponPosition"))),"Held weapon must follow the complete viewer hand pose");
    check(changedText.contains("journey_status")&&changedText.contains("journey_notice")&&runtime.find("journey_status").definition.text!=connecting,"Shared HUD must replace the connecting text");
    check(runtime.find("journey_status").definition.text.find("HP ")!=std::string::npos,"Actual HUD must expose authoritative health");
    // Presentation-only mutations below are never sent back to the real server.
    auto display=state;const auto selfSlot=static_cast<std::size_t>(self.at("slot").number());
    auto setSelf=[&](const char* key,Value value){display.at("self").map()[key]=value;display.at("players").array();auto& list=std::get<Value::Array>(display.at("players").data);list.at(selfSlot).map()[key]=std::move(value);};
    for(const auto& [weapon,color]:std::array<std::pair<const char*,const char*>,3>{{{"starter","#C9D8DA"},{"iron","#91B4B7"},{"star","#A2EDDF"}}}){
      setSelf("weapon",weapon);runtime.setSharedState(display);runtime.update(.02);
      check(runtime.material(nodeIndex(runtime,"held_blade")).color==color,"Held blade material must follow all three weapon states");
      check(runtime.find("journey_status").definition.text.find(weapon)!=std::string::npos,"HUD must follow the current weapon state");
    }
    setSelf("gold",142);setSelf("quest",2);runtime.setSharedState(display);runtime.update(.02);
    check(runtime.find("journey_status").definition.text.find("G 142")!=std::string::npos&&runtime.find("journey_notice").definition.text.find("THE FIRST MEADOW")!=std::string::npos,"HUD must follow gold and completed quest state");
    auto& playerList=std::get<Value::Array>(display.at("players").data);auto& other=playerList.at(static_cast<std::size_t>(otherSlot)).map();
    runtime.setViewerState(viewer(valueVec3(other.at("position"))));runtime.update(.02);check(!runtime.find(players.at(otherSlot)).transform.visible,"Near-viewer avatar must hide instead of obscuring the camera");
    runtime.setViewerState(view);runtime.update(.02);check(runtime.find(players.at(otherSlot)).transform.visible,"Moving the viewer away must restore the other avatar");
    std::vector<std::pair<std::string,Value::Map>> actions;runtime.setActionSink([&](const std::string& kind,Value::Map payload){actions.emplace_back(kind,std::move(payload));return true;});
    for(const auto& [target,kind]:std::array<std::pair<const char*,const char*>,5>{{{"starter_altar","equip"},{"iron_altar","equip"},{"star_altar","equip"},{"rest_shrine","rest"},{"quest_stone","claim"}}}){
      const auto found=interactions.find(target);check(found!=interactions.end(),"Required authored interaction is missing");const auto before=actions.size();
      runtime.interact(selectable(runtime,found->second));check(actions.size()==before+1&&actions.back().first==kind&&actions.back().second.size()==1&&actions.back().second.at("target").string()==target,"Authored interaction must emit exactly the contracted kind and target");
    }
    runtime.interact(selectable(runtime,"slime_0"));check(actions.size()==6&&actions.back().first=="attack"&&actions.back().second.size()==1&&actions.back().second.at("target").string()=="slime_0","Monster interaction must emit one attack intent");
    const auto weaponBefore=runtime.find("weapon_control").transform.rotation;runtime.update(.02);runtime.update(.05);check(runtime.find("weapon_control").transform.rotation!=weaponBefore,"A monster click must animate the held weapon swing");
    // Attack counters and movement are presentation inputs; this checks joints,
    // while server-side combat tests own damage/cooldown/reward validation.
    other["attack"]=other.at("attack").number()+1;auto moved=valueVec3(other.at("position"));moved[0]+=.6;other["position"]=vecValue(moved);
    auto& monsterList=std::get<Value::Array>(display.at("monsters").data);monsterList.at(6).map()["attack"]=monsterList.at(6).map().at("attack").number()+1;
    runtime.setSharedState(display);runtime.update(.02);const auto arm=players.at(otherSlot)+"_arm_right";const auto avatarBefore=runtime.find(arm).transform.rotation;const auto jointBefore=runtime.find("golem_0_left").transform.rotation;const auto bodyBefore=runtime.find("slime_0_body").transform.scale;
    runtime.update(.05);
    check(runtime.find(arm).transform.rotation!=avatarBefore,"Other player attack counter must animate its arm joint over time");
    check(runtime.find("golem_0_left").transform.rotation!=jointBefore,"Monster attack counter must animate its articulated joint over time");
    check(runtime.find("slime_0_body").transform.scale!=bodyBefore,"Monster breathing must evolve over time");
    setSelf("hp",0);runtime.setSharedState(display);runtime.update(.02);check(!runtime.find("weapon_control").transform.visible,"Dead local player must not keep a visible held weapon");
    check(errors.empty()&&runtime.activeBindings()==expectedBindings,"All authored bindings must remain active after the complete integration flow");
    first.stop();second.stop();
    std::cout<<Value(Value::Map{{"passed",true},{"nodes",static_cast<double>(world->nodes.size())},{"bindings",static_cast<double>(expectedBindings)},{"bindingErrors",static_cast<double>(errors.size())},{"networkSnapshots",snapshots},{"activePlayers",maximumPlayers},{"snapshotNullStart",true},{"selfAndInactiveHidden",true},{"otherAndNearVisibility",true},{"monstersAndHealth",true},{"allWeaponStyles",true},{"viewerWeaponPose",true},{"hudText",true},{"actionContracts",static_cast<double>(actions.size())},{"jointAnimations",true}}).display()<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<"Shared world probe failed: "<<error.what()<<'\n';return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::string> args;for(int i=0;i<argc;++i){const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(count<=0)return 2;std::string text(static_cast<std::size_t>(count),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,text.data(),count,nullptr,nullptr))return 2;text.pop_back();args.push_back(std::move(text));}return run(args);}
#else
int main(int argc,char** argv){std::vector<std::string> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);}
#endif
