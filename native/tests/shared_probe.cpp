#include "link/shared.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
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
void check(bool condition){if(!condition)throw std::runtime_error("Shared transport probe assertion failed.");}
Value::Map waitSnapshot(SharedSession& session,Clock::time_point deadline){
  while(Clock::now()<deadline){if(auto state=session.poll())return std::move(*state);if(!session.error().empty())throw std::runtime_error("Shared transport probe connection failed.");std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  throw std::runtime_error("Shared transport probe timed out.");
}
int run(const std::vector<std::string>& args){
  try{
    if(args.size()<2)throw std::runtime_error("Shared transport probe requires an address.");
    int duration=550,stopAfter=-1;bool expectError=false,resume=false,two=false,stress=false;std::string kind,target,name="Explorer";
    for(std::size_t i=2;i<args.size();++i){const auto& option=args[i];
      if(option=="--expect-error")expectError=true;else if(option=="--resume-test")resume=true;else if(option=="--two-players")two=true;else if(option=="--queue-stress")stress=true;
      else if(option=="--duration-ms"&&i+1<args.size())duration=std::stoi(args[++i]);
      else if(option=="--stop-after-ms"&&i+1<args.size())stopAfter=std::stoi(args[++i]);
      else if(option=="--name"&&i+1<args.size())name=args[++i];
      else if(option=="--action"&&i+2<args.size()){kind=args[++i];target=args[++i];}
      else throw std::runtime_error("Shared transport probe received invalid options.");
    }
    check(duration>=50&&duration<=12000&&stopAfter<=12000);const auto started=Clock::now();
    SharedSession session(args[1],{},name);int queued=0;
    if(stress)for(int i=0;i<64;++i)if(session.sendAction("rest",{{"target","rest_shrine"}}))++queued;
    if(stress)check(queued==32);
    if(stopAfter>=0){std::this_thread::sleep_for(std::chrono::milliseconds(stopAfter));const auto before=Clock::now();session.stop();const auto stopMs=std::chrono::duration<double,std::milli>(Clock::now()-before).count();check(stopMs<500);std::cout<<Value(Value::Map{{"passed",true},{"stopMs",stopMs},{"queued",queued}}).display()<<'\n';return 0;}
    std::unique_ptr<SharedSession> peer;if(two)peer=std::make_unique<SharedSession>(args[1],"","Second Explorer");
    std::optional<Value::Map> last;int snapshots=0,maximumPlayers=0;bool actionSent=false;std::string notice;
    while(Clock::now()-started<std::chrono::milliseconds(duration)){
      if(auto state=session.poll()){
        ++snapshots;last=std::move(*state);int active=0;for(const auto& player:last->at("players").array())if(player.map().at("active").boolean())++active;maximumPlayers=std::max(maximumPlayers,active);notice=last->at("notice").string();
        if(!kind.empty()&&!actionSent){check(session.sendAction(kind,{{"target",target}}));actionSent=true;}
      }
      if(!session.error().empty())break;session.updatePose({0,1.65,28},0);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(expectError){check(!session.error().empty());session.stop();std::cout<<Value(Value::Map{{"passed",true},{"rejected",true},{"error",session.error()},{"snapshots",snapshots}}).display()<<'\n';return 0;}
    check(session.error().empty()&&session.connected()&&snapshots>=2&&last.has_value());if(two)check(maximumPlayers>=2);if(!kind.empty())check(actionSent);
    const auto identity=session.playerId();const auto secret=session.token();check(!identity.empty()&&!secret.empty());
    const auto before=Clock::now();session.stop();const auto stopMs=std::chrono::duration<double,std::milli>(Clock::now()-before).count();check(stopMs<500);
    bool resumed=false;
    if(resume){std::this_thread::sleep_for(std::chrono::milliseconds(100));SharedSession resumedSession(args[1],secret,name);const auto state=waitSnapshot(resumedSession,Clock::now()+std::chrono::seconds(4));check(resumedSession.playerId()==identity&&resumedSession.token()==secret&&state.at("self").map().at("id").string()==identity);resumed=true;resumedSession.stop();}
    if(peer)peer->stop();
    std::cout<<Value(Value::Map{{"passed",true},{"snapshots",snapshots},{"playerId",identity},{"maximumPlayers",maximumPlayers},{"actionQueued",actionSent},{"resumed",resumed},{"queued",queued},{"stopMs",stopMs},{"unicodeNotice",notice=="\xED\x95\x9C\xEA\xB8\x80 notice \xF0\x9F\x8C\x8D"}}).display()<<'\n';return 0;
  }catch(...){std::cerr<<"Shared transport probe failed; credentials and payloads are not printed.\n";return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::string> args;for(int i=0;i<argc;++i){const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(size<=0)return 2;std::string text(static_cast<std::size_t>(size),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,text.data(),size,nullptr,nullptr))return 2;text.pop_back();args.push_back(std::move(text));}return run(args);}
#else
int main(int argc,char** argv){std::vector<std::string> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);}
#endif
