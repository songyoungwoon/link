#include "link/loader.hpp"
#include "link/remote.hpp"
#include "link/runtime.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stop_token>
#include <thread>

namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
template<class Operation> void rejects(Operation operation,const std::string& description) {
  try{operation();}catch(const linkworld::Error&){return;}
  throw std::runtime_error("expected rejection: "+description);
}
void write(const std::filesystem::path& path,const std::string& contents) {
  std::filesystem::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<contents;
  check(bool(output),"cannot write local loader fixture");
}
struct Fixture {
  std::filesystem::path parent,directory,worlds;
  Fixture() {
    parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    directory=parent/("link-loader-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory),"cannot allocate loader test directory");worlds=directory/"worlds";
    for(const auto* id:{"room-a","room-b"}) {
      write(worlds/id/"world.link",manifest(id));
      write(worlds/id/"scene.element","scene {object room {component surface {element panel {geometry:{type:\"box\",size:[1,1,1]};}}}}");
      write(worlds/id/"appearance.style","");
    }
  }
  ~Fixture() {
    std::error_code error;const auto actual=std::filesystem::canonical(directory,error);
    if(!error&&actual==directory&&directory.parent_path()==parent)std::filesystem::remove_all(directory,error);
  }
  static std::string manifest(const std::string& id,const std::string& portals="") {
    return "world {version:\"0.1\";id:\""+id+"\";name:\"Loader test\";elements:\"scene.element\";style:\"appearance.style\";scripts:[];entry:{position:[0,1,3],target:[0,1,0]};"+portals+"}";
  }
};
std::set<std::filesystem::path> snapshots() {
  std::set<std::filesystem::path> result;std::error_code error;
  for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::temp_directory_path(),error)) {
    const auto name=entry.path().filename().string();if(name.starts_with("link-world-")&&entry.is_directory())result.insert(entry.path());
  }
  return result;
}
void unitTests() {
  using namespace linkworld;Fixture fixture;const auto current=std::string("link://192.0.2.10:9123/room-a");
  check(resolvePortalAddress("/room-b","link://local/room-a",fixture.worlds)=="link://local/room-b","relative local portal retains local authority");
  check(resolvePortalAddress("/room-b",current,fixture.worlds)=="link://192.0.2.10:9123/room-b","relative remote portal retains IP and port");
  const auto defaultTarget=parseRemoteLink(resolvePortalAddress("/room-b","link://192.0.2.10/room-a",fixture.worlds));
  check(defaultTarget.host=="192.0.2.10"&&defaultTarget.port==8000&&defaultTarget.world=="room-b","default remote portal port remains 8000");
  check(resolvePortalAddress("link://local/room-b",current,fixture.worlds)=="link://local/room-b","explicit local portal uses browser worlds");
  const auto absolute=std::string("link://192.0.2.20:9010/missing-target");
  check(resolvePortalAddress(absolute,current,fixture.worlds)==absolute,"absolute remote portal keeps its authority without prefetch");
  for(const auto& invalid:{"/../room-b","//192.0.2.20/room-b","/room-b/extra","/room-b?x=1","/%2froom-b","/%252e%252e","link://example.com/room-b","https://192.0.2.20/room-b"})
    rejects([&]{resolvePortalAddress(invalid,current,fixture.worlds);},invalid);
  rejects([&]{resolvePortalAddress("/missing-target","link://local/room-a",fixture.worlds);},"local portal retains existence checks");
  const std::string portals=R"(portals:[{id:"next",address:"/room-b",position:[0,1,-3],size:[2,2,1]},{id:"remote",address:"link://192.0.2.20:9010/missing-target",position:[3,1,0],size:[2,2,1]}];)";
  write(fixture.worlds/"room-a"/"world.link",Fixture::manifest("room-a",portals));
  const auto world=loadWorld("link://local/room-a",fixture.worlds);
  check(!world->resourceOwner&&world->address=="link://local/room-a","local worlds retain their original address and filesystem ownership");
  check(world->portals.size()==2&&world->portals[0].address=="link://local/room-b"&&world->portals[1].address==absolute,"local loader resolves relative portals and validates remote targets without loading them");
  const auto before=snapshots();std::stop_source cancelled;cancelled.request_stop();
  rejects([&]{loadWorld(current,fixture.worlds,cancelled.get_token());},"cancelled remote world");
  check(snapshots()==before,"pre-cancelled navigation creates no remote snapshot");
}
void rejectedIntegration(const std::string& address,const std::filesystem::path& worldsRoot,const std::string& expected) {
  linkworld::parseRemoteLink(address);const auto before=snapshots();bool rejected=false;
  try{linkworld::loadWorld(address,worldsRoot);}catch(const linkworld::Error& error) {
    rejected=true;check(std::string(error.what()).find(expected)!=std::string::npos,"unexpected remote failure: "+std::string(error.what()));
    check(error.location.file.starts_with("link://"),"remote failure identifies its LINK origin");
  }
  check(rejected,"invalid remote world unexpectedly loaded");check(snapshots()==before,"failed remote load removes its temporary snapshot");
}
void visitPortals(const std::string& address,const std::filesystem::path& worldsRoot) {
  using namespace linkworld;const auto origin=parseRemoteLink(address);const auto before=snapshots();const auto beforeRuntime=counters();
  std::vector<std::filesystem::path> directories;
  const auto activate=[](const std::shared_ptr<const World>& world) {
    std::vector<std::string> errors;
    {
      Runtime runtime(world,[&](bool error,const std::string& message){if(error)errors.push_back(message);});
      runtime.start();runtime.update(.1);
      const auto bindings=static_cast<std::size_t>(std::count_if(world->nodes.begin(),world->nodes.end(),[](const auto& node){return !node.script.empty();}));
      check(runtime.activeBindings()==bindings,"portal destination keeps downloaded Script bindings active");
    }
    check(errors.empty(),"portal destination runtime error: "+(errors.empty()?std::string():errors.front()));
  };
  {
    auto source=loadWorld(address,worldsRoot);directories.push_back(source->directory);
    const auto portal=std::find_if(source->portals.begin(),source->portals.end(),[](const auto& candidate){return !candidate.address.starts_with("link://local/");});
    check(portal!=source->portals.end(),"source world must declare a remote portal to visit");
    const auto targetAddress=portal->address;
    check(parseRemoteLink(targetAddress).authority()==origin.authority(),"outbound portal retains server IP and port");
    auto target=loadWorld(targetAddress,worldsRoot);directories.push_back(target->directory);
    check(target->address==targetAddress&&target->resourceOwner,"portal destination loads its own remote snapshot");
    source.reset();check(!std::filesystem::exists(directories.front()),"leaving source world removes its snapshot");
    activate(target);
    if(!target->portals.empty()) {
      const auto returnAddress=target->portals.front().address;
      const auto returnOrigin=parseRemoteLink(returnAddress);
      check(returnOrigin.authority()==origin.authority(),"return portal retains server IP and port");
      check(returnOrigin.world==origin.world,"destination first portal returns to the source world");
      auto returned=loadWorld(returnAddress,worldsRoot);directories.push_back(returned->directory);
      check(returned->address==returnAddress&&returned->resourceOwner,"return portal loads a fresh remote snapshot");
      target.reset();check(!std::filesystem::exists(directories[1]),"returning removes the destination snapshot");
      activate(returned);
    }
  }
  for(const auto& directory:directories)check(!std::filesystem::exists(directory),"visited remote world snapshots are released");
  check(snapshots()==before,"portal round trip leaves no temporary snapshots");
  check(counters().activeWorlds==beforeRuntime.activeWorlds&&counters().activeBindings==beforeRuntime.activeBindings,"portal round trip releases Runtime and Script bindings");
}
void cancelledIntegration(const std::string& address,const std::filesystem::path& worldsRoot,unsigned delayMs) {
  using namespace linkworld;const auto before=snapshots();std::stop_source stop;
  const auto started=std::chrono::steady_clock::now();bool rejected=false;
  std::jthread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));stop.request_stop();});
  try{loadWorld(address,worldsRoot,stop.get_token());}catch(const Error& error) {
    rejected=true;check(std::string(error.what()).find("cancelled")!=std::string::npos,"unexpected cancellation failure: "+std::string(error.what()));
    check(error.location.file.starts_with("link://"),"cancelled load identifies its LINK origin");
  }
  check(rejected,"in-flight remote world unexpectedly loaded");
  check(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(delayMs+1400),"in-flight load cancellation exceeded its bound");
  check(snapshots()==before,"cancelled remote load removes its partial snapshot");
}
void integration(const std::string& address,const std::filesystem::path& worldsRoot) {
  using namespace linkworld;const auto origin=parseRemoteLink(address);
  auto first=loadWorld(address,worldsRoot);const auto firstDirectory=first->directory;
  check(first->address==address&&first->resourceOwner,"remote snapshot preserves original LINK address and owns its files");
  check(!first->images.empty(),"integration world must declare at least one image");
  check(std::any_of(first->portals.begin(),first->portals.end(),[&](const auto& portal){
    if(portal.address.starts_with("link://local/"))return false;
    return parseRemoteLink(portal.address).authority()==origin.authority();
  }),"integration world must contain a relative portal on the same authority");
  for(const auto& [name,path]:first->images){(void)name;check(std::filesystem::is_regular_file(path),"remote image exists for renderer and full-size viewer");}
  const auto beforeRuntime=counters();std::vector<std::string> scriptErrors;
  {
    Runtime runtime(first,[&](bool error,const std::string& message){if(error)scriptErrors.push_back(message);});
    runtime.start();runtime.update(.1);
    const auto expectedBindings=static_cast<std::size_t>(std::count_if(first->nodes.begin(),first->nodes.end(),[](const auto& node){return !node.script.empty();}));
    check(runtime.activeBindings()==expectedBindings,"downloaded Script bindings remain active after onStart and onUpdate");
    if(!first->programs.empty()) {
      check(expectedBindings>0&&counters().activeBindings>beforeRuntime.activeBindings,"integration world executes a bound downloaded Script");
      check(counters().updates>beforeRuntime.updates,"remote Runtime processes an update");
    }
  }
  check(scriptErrors.empty(),"downloaded Script runtime error: "+(scriptErrors.empty()?std::string():scriptErrors.front()));
  check(counters().activeWorlds==beforeRuntime.activeWorlds&&counters().activeBindings==beforeRuntime.activeBindings,"remote Runtime releases worlds and Script bindings");
  auto second=loadWorld(address,worldsRoot);const auto secondDirectory=second->directory;
  check(firstDirectory!=secondDirectory,"fresh reload has an isolated snapshot");
  auto retained=first;first.reset();check(std::filesystem::is_directory(firstDirectory),"retained World keeps its image snapshot alive");
  retained.reset();check(!std::filesystem::exists(firstDirectory),"released World removes its snapshot");
  check(std::filesystem::is_directory(secondDirectory),"releasing old World preserves replacement snapshot");
  second.reset();check(!std::filesystem::exists(secondDirectory),"replacement snapshot is removed after release");
  const auto before=snapshots();std::stop_source cancelled;cancelled.request_stop();
  rejects([&]{loadWorld(address,worldsRoot,cancelled.get_token());},"pre-cancelled integration navigation");
  check(snapshots()==before,"cancelled integration navigation leaves no snapshot");
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
  try {
    unitTests();
    if(argc>1) {
      const std::string usage="usage: link_remote_loader_tests [--integration LINK-address local-worlds-root | --visit-portals LINK-address local-worlds-root | --reject LINK-address local-worlds-root error-substring | --cancel LINK-address local-worlds-root delay-ms]";
#ifdef _WIN32
      const auto argument=[&](int index){std::string value;for(const auto character:std::wstring(argv[index])){check(character>=0&&character<=127,"remote test option must be ASCII");value+=static_cast<char>(character);}return value;};
#else
      const auto argument=[&](int index){return std::string(argv[index]);};
#endif
      const auto option=argument(1);check((argc==4&&(option=="--integration"||option=="--visit-portals"))||(argc==5&&(option=="--reject"||option=="--cancel")),usage);
      if(option=="--integration") {
        integration(argument(2),std::filesystem::path(argv[3]));
        std::cout<<"PASS remote Script execution, snapshot loading, reload isolation, image lifetime and cleanup\n";
      }else if(option=="--visit-portals") {
        visitPortals(argument(2),std::filesystem::path(argv[3]));
        std::cout<<"PASS remote portal traversal, return, Script execution and snapshot cleanup\n";
      }else if(option=="--cancel") {
        cancelledIntegration(argument(2),std::filesystem::path(argv[3]),static_cast<unsigned>(std::stoul(argument(4))));
        std::cout<<"PASS in-flight native LINK cancellation and partial snapshot cleanup\n";
      }else {
        rejectedIntegration(argument(2),std::filesystem::path(argv[3]),argument(4));
        std::cout<<"PASS remote failure diagnostics and snapshot cleanup\n";
      }
    }
    std::cout<<"PASS relative and absolute portal authority, local compatibility, remote lazy validation and cancellation\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL remote loader: "<<error.what()<<'\n';return 1;}
}
