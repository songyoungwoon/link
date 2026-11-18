#include "link/loader.hpp"
#include "link/runtime.hpp"
#include "link/shared.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
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
void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
std::filesystem::path utf8Path(const std::string& text){std::u8string value;for(unsigned char c:text)value+=static_cast<char8_t>(c);return std::filesystem::path(value);}
int run(const std::vector<std::string>& args){
  try{
    std::string address,worlds;int duration=600,stopAfter=-1;bool expectError=false,expectCycle=false;
    for(std::size_t i=1;i<args.size();++i){const auto& option=args[i];
      if(option=="--address"&&i+1<args.size())address=args[++i];
      else if(option=="--worlds"&&i+1<args.size())worlds=args[++i];
      else if(option=="--duration-ms"&&i+1<args.size())duration=std::stoi(args[++i]);
      else if(option=="--stop-after-ms"&&i+1<args.size())stopAfter=std::stoi(args[++i]);
      else if(option=="--expect-error")expectError=true;
      else if(option=="--expect-cycle")expectCycle=true;
      else throw std::runtime_error("Invalid NPC probe option.");
    }
    check(!address.empty()&&duration>=50&&duration<=60000&&stopAfter<=12000,"NPC probe requires an address and bounded duration.");
    bool credentialRejected=false;try{SharedSession invalid(address,"private-never-forwarded","Observer",SharedProfile::NpcTravel);}catch(const std::invalid_argument&){credentialRejected=true;}
    check(credentialRejected,"NPC observer must refuse player credentials before connecting.");
    std::unique_ptr<Runtime> runtime;std::vector<std::string> errors;
    if(!worlds.empty()){
      const auto world=loadWorld(address,utf8Path(worlds));
      check(std::find(world->extensions.begin(),world->extensions.end(),"npc-travel-v1")!=world->extensions.end(),"Authored observer world must declare npc-travel-v1.");
      runtime=std::make_unique<Runtime>(world,[&](bool error,const std::string& text){if(error)errors.push_back(text);});runtime->start();runtime->update(.02);
      check(errors.empty(),"Authored NPC Script failed before snapshots.");
      check(!runtime->find("npc_slot").transform.visible,"Authored NPC must stay hidden before an authority snapshot.");
    }
    SharedSession session(address,{},"Observer",SharedProfile::NpcTravel);
    check(!session.sendAction("authority-transfer",{{"target","forbidden"}}),"NPC observer must never queue authority actions.");
    session.updatePose({std::numeric_limits<double>::quiet_NaN(),0,0},std::numeric_limits<double>::quiet_NaN());
    if(stopAfter>=0){std::this_thread::sleep_for(std::chrono::milliseconds(stopAfter));const auto before=Clock::now();session.stop();const auto stopMs=std::chrono::duration<double,std::milli>(Clock::now()-before).count();check(stopMs<500,"NPC observer cancellation exceeded its bound.");std::cout<<Value(Value::Map{{"passed",true},{"stopMs",stopMs}}).display()<<'\n';return 0;}
    const auto started=Clock::now();int snapshots=0;bool sawNpc=false,sawEmpty=false,sawWalk=false,positionChanged=false;double maximumEpoch=0,maximumJourney=0;std::set<std::string> identities;std::map<std::string,Vec3> firstPositions;
    while(Clock::now()-started<std::chrono::milliseconds(duration)){
      if(auto state=session.poll()){
        ++snapshots;const auto& npcs=state->at("npcs").array();sawEmpty=sawEmpty||npcs.empty();sawNpc=sawNpc||!npcs.empty();
        for(const auto& item:npcs){const auto& npc=item.map();const auto id=npc.at("id").string();identities.insert(id);maximumEpoch=std::max(maximumEpoch,npc.at("epoch").number());maximumJourney=std::max(maximumJourney,npc.at("journey").number());sawWalk=sawWalk||npc.at("animation").string()=="Walk";const auto position=valueVec3(npc.at("position"));if(firstPositions.contains(id)&&firstPositions.at(id)!=position)positionChanged=true;firstPositions.try_emplace(id,position);}
        if(runtime){
          runtime->setSharedState(*state);for(int settle=0;settle<20;++settle)runtime->update(.05);
          check(errors.empty(),"Authored NPC Script failed while presenting server state.");
          const Value::Map* selected=nullptr;for(const auto& item:npcs)if(item.map().at("kind").string()=="liora"){selected=&item.map();break;}
          check(runtime->find("npc_slot").transform.visible==(selected!=nullptr),"Authored NPC visibility must match server presence.");
          if(selected){
            const auto& model=runtime->find("traveler_model");const auto target=valueVec3(selected->at("position"));
            for(std::size_t axis=0;axis<3;++axis)check(std::abs(model.transform.position[axis]-target[axis])<.001,"Authored NPC must converge to the authority position.");
            const auto animation=selected->at("animation").string()=="Walk"?"Walk":"Idle";
            check(model.animation.clip==animation&&model.animation.playing,"Authored model must play the authority movement animation.");
            check(runtime->find("traveler_status").definition.text.find(selected->at("id").string())!=std::string::npos,"Authored placard must retain the transferred NPC identity.");
          }
        }
      }
      if(!session.error().empty())break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(expectError){check(!session.error().empty(),"Malformed NPC state was not rejected.");session.stop();std::cout<<Value(Value::Map{{"passed",true},{"rejected",true},{"snapshots",snapshots}}).display()<<'\n';return 0;}
    check(session.error().empty()&&session.connected()&&snapshots>=2,"NPC observer failed to receive repeated state.");
    check(session.token().empty()&&session.playerId().empty(),"NPC observer must never acquire player credentials or identity.");
    if(expectCycle)check(sawNpc&&sawEmpty&&sawWalk&&positionChanged&&maximumEpoch>=2&&maximumJourney>=1,"NPC fixture arrival, movement, departure and return were not all observed.");
    const auto before=Clock::now();session.stop();const auto stopMs=std::chrono::duration<double,std::milli>(Clock::now()-before).count();check(stopMs<500,"NPC observer stop exceeded its bound.");
    if(runtime){runtime->setSharedState({{"tick",0},{"time",0},{"world",runtime->world().id},{"npcs",Value::Array{}}});runtime->update(.05);check(errors.empty()&&!runtime->find("npc_slot").transform.visible,"A departed NPC must disappear without retaining a ghost model.");}
    Value::Array seen;for(const auto& identity:identities)seen.emplace_back(identity);
    std::cout<<Value(Value::Map{{"passed",true},{"snapshots",snapshots},{"sawNpc",sawNpc},{"sawEmpty",sawEmpty},{"sawWalk",sawWalk},{"positionChanged",positionChanged},{"maxEpoch",maximumEpoch},{"maxJourney",maximumJourney},{"identities",seen},{"runtimeChecked",runtime!=nullptr},{"bindingErrors",static_cast<double>(errors.size())},{"credentialsAbsent",true},{"stopMs",stopMs}}).display()<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<"NPC observer probe failed: "<<error.what()<<'\n';return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::string> args;for(int i=0;i<argc;++i){const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(count<=0)return 2;std::string value(static_cast<std::size_t>(count),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,value.data(),count,nullptr,nullptr))return 2;value.pop_back();args.push_back(std::move(value));}return run(args);}
#else
int main(int argc,char** argv){std::vector<std::string> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);}
#endif
