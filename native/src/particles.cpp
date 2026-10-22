#include "link/particles.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace linkworld {
namespace {
using Store=std::map<std::uint32_t,Particle>;
using Cell=std::array<std::int64_t,3>;
using Grid=std::map<Cell,std::vector<std::uint32_t>>;
void keys(const Value::Map& value){
  for(const auto& [key,_]:value)
    if(key!="position"&&key!="velocity"&&key!="radius"&&key!="color"&&key!="state")
      throw Error({},"unsupported particle field '"+key+"'");
}
void validateRadius(double number){
  if(!std::isfinite(number)||number<=0||number>1000)throw Error({},"particle radius must be finite, positive and at most 1000");
}
void validateStateShape(const Value& value,std::size_t depth,std::size_t& count){
  if(depth>Limits::particleStateDepth)throw Error({},"particle state depth budget exceeded (maximum 16, root depth 0)");
  if(++count>Limits::particleStateValues)throw Error({},"particle state value count budget exceeded (maximum 64 including root)");
  if(const auto* array=std::get_if<Value::Array>(&value.data))for(const auto& child:*array)validateStateShape(child,depth+1,count);
  if(const auto* map=std::get_if<Value::Map>(&value.data))for(const auto& [_,child]:*map)validateStateShape(child,depth+1,count);
}
void patchParticle(Particle& particle,const Value::Map& values){
  for(const auto& [name,value]:values){
    if(name=="position")particle.position=valueVec3(value);
    else if(name=="velocity")particle.velocity=valueVec3(value);
    else if(name=="radius"){const auto number=value.number();validateRadius(number);particle.radius=number;}
    else if(name=="color"){validateStyle({{"color",value}});particle.color=value.string();}
    else if(name=="state"){
      const auto& state=value.map();
      std::size_t valueCount=0;validateStateShape(value,0,valueCount);
      if(value.bytes()>Limits::particleStateBytes)throw Error({},"particle state byte budget exceeded (maximum 1024)");
      particle.state=state;
    }
  }
}
Cell cellOf(const Vec3& position,double width){
  // Saturation preserves correctness for tiny author-specified cell sizes:
  // extreme cells share a bucket and still undergo an exact distance test.
  constexpr std::int64_t bound=std::int64_t{1}<<60;
  Cell result{};
  for(std::size_t axis=0;axis<3;++axis){
    const auto cell=std::floor(position[axis]/width);
    result[axis]=cell>=static_cast<double>(bound)?bound:cell<=-static_cast<double>(bound)?-bound:static_cast<std::int64_t>(cell);
  }
  return result;
}
Grid makeGrid(const Store& particles,double width){
  Grid result;for(const auto& [id,particle]:particles)result[cellOf(particle.position,width)].push_back(id);return result;
}
}
struct ParticleEngine::Impl {
  struct System {
    std::string id,color;std::size_t node=0,capacity=0;
    double radius=.04,cellSize=.6;
    Store live,snapshot,staged;Grid grid;
  };
  std::vector<System> systems;
  std::map<std::string,std::size_t> byId;
  std::map<std::size_t,std::size_t> byNode;
  bool active=false;
  std::size_t mutations=0,spawns=0,candidateChecks=0;
  std::uint32_t nextId=1;
  System& system(const std::string& id){
    const auto found=byId.find(id);if(found==byId.end())throw Error({},"unknown particle system '"+id+"'");return systems[found->second];
  }
  const System& system(const std::string& id) const {
    const auto found=byId.find(id);if(found==byId.end())throw Error({},"unknown particle system '"+id+"'");return systems[found->second];
  }
  void phase() const {if(!active)throw Error({},"particle mutations require an active simulation phase");}
  void mutation() const {if(mutations>=Limits::particleMutations)throw Error({},"particle mutation budget exceeded (maximum 4096 per phase)");}
};

Value particleValue(const Particle& particle){
  return Value::Map{{"id",static_cast<double>(particle.id)},{"position",vecValue(particle.position)},
    {"velocity",vecValue(particle.velocity)},{"radius",particle.radius},{"color",particle.color},
    {"age",particle.age},{"state",particle.state}};
}
ParticleEngine::ParticleEngine(const World& world):impl_(std::make_unique<Impl>()){
  std::size_t capacity=0;
  for(std::size_t index=0;index<world.nodes.size();++index){
    const auto& node=world.nodes[index];
    if(node.kind!=Kind::Element||node.geometry.type!=Geometry::Type::Particles)continue;
    if(std::find(world.extensions.begin(),world.extensions.end(),"particles-v1")==world.extensions.end())throw Error(node.location,"particles geometry requires the particles-v1 world extension");
    const auto& geometry=node.geometry;
    if(geometry.capacity<1||geometry.capacity>Limits::particleCapacity)throw Error(node.location,"invalid particle capacity");
    if(geometry.capacity>Limits::worldParticles-capacity)throw Error(node.location,"world particle capacity budget exceeded (maximum 8192)");
    validateRadius(geometry.radius);validateRadius(geometry.cellSize);capacity+=geometry.capacity;
    Impl::System system;system.id=node.id;system.node=index;system.capacity=geometry.capacity;
    system.radius=geometry.radius;system.cellSize=geometry.cellSize;system.color=resolveStyle(node,world).color;
    const auto systemIndex=impl_->systems.size();
    if(!impl_->byId.emplace(node.id,systemIndex).second)throw Error(node.location,"duplicate particle system ID");
    impl_->byNode.emplace(index,systemIndex);impl_->systems.push_back(std::move(system));
  }
}
ParticleEngine::~ParticleEngine()=default;
void ParticleEngine::beginStep(){
  if(impl_->active)throw Error({},"particle simulation phase is already active");
  // All allocations finish before the phase becomes observable.
  for(auto& system:impl_->systems){system.snapshot=system.live;system.staged=system.live;system.grid=makeGrid(system.snapshot,system.cellSize);}
  impl_->mutations=impl_->spawns=impl_->candidateChecks=0;impl_->active=true;
}
void ParticleEngine::endStep(double dt){
  impl_->phase();
  if(!std::isfinite(dt)||dt<0||dt>1)throw Error({},"particle time step must be finite and between 0 and 1");
  // Preflight the entire world before changing any staged position or age.
  for(const auto& system:impl_->systems)for(const auto& [_,particle]:system.staged){
    for(std::size_t axis=0;axis<3;++axis){
      const auto next=particle.position[axis]+particle.velocity[axis]*dt;
      if(!std::isfinite(next)||std::abs(next)>1e6)throw Error({},"integrated particle coordinate exceeds 1000000");
    }
    if(!std::isfinite(particle.age+dt))throw Error({},"particle age is nonfinite");
  }
  for(auto& system:impl_->systems){
    for(auto& [_,particle]:system.staged){for(std::size_t axis=0;axis<3;++axis)particle.position[axis]+=particle.velocity[axis]*dt;particle.age+=dt;}
    system.live.swap(system.staged);system.snapshot.clear();system.staged.clear();system.grid.clear();
  }
  impl_->active=false;
}
void ParticleEngine::abortStep() noexcept {
  for(auto& system:impl_->systems){system.snapshot.clear();system.staged.clear();system.grid.clear();}impl_->active=false;
}
std::optional<std::uint32_t> ParticleEngine::spawn(const std::string& systemId,const Value::Map& specification){
  impl_->phase();auto& system=impl_->system(systemId);keys(specification);
  if(!specification.contains("position"))throw Error({},"particle spawn requires position");
  Particle particle;particle.radius=system.radius;particle.color=system.color;patchParticle(particle,specification);
  if(system.staged.size()>=system.capacity)return {};
  impl_->mutation();
  if(impl_->spawns>=Limits::particleSpawns)throw Error({},"particle spawn budget exceeded (maximum 256 per phase)");
  if(impl_->nextId>Limits::particleLifetimeIds)throw Error({},"particle lifetime ID budget exceeded (maximum 1000000)");
  particle.id=impl_->nextId;
  system.staged.emplace(particle.id,std::move(particle));
  ++impl_->nextId;++impl_->mutations;++impl_->spawns;return impl_->nextId-1;
}
void ParticleEngine::remove(const std::string& systemId,std::uint32_t id){
  impl_->phase();auto& system=impl_->system(systemId);const auto found=system.staged.find(id);
  if(found==system.staged.end())throw Error({},"unknown live particle ID");
  impl_->mutation();system.staged.erase(found);++impl_->mutations;
}
void ParticleEngine::set(const std::string& systemId,std::uint32_t id,const Value::Map& patch){
  impl_->phase();auto& system=impl_->system(systemId);const auto found=system.staged.find(id);
  if(found==system.staged.end())throw Error({},"unknown live particle ID");
  keys(patch);impl_->mutation();auto next=found->second;patchParticle(next,patch);
  found->second=std::move(next);++impl_->mutations;
}
std::optional<Particle> ParticleEngine::get(const std::string& systemId,std::uint32_t id) const {
  const auto& system=impl_->system(systemId);const auto& particles=impl_->active?system.snapshot:system.live;
  const auto found=particles.find(id);return found==particles.end()?std::optional<Particle>{}:found->second;
}
std::vector<std::uint32_t> ParticleEngine::ids(const std::string& systemId) const {
  const auto& system=impl_->system(systemId);const auto& particles=impl_->active?system.snapshot:system.live;
  std::vector<std::uint32_t> result;result.reserve(particles.size());for(const auto& [id,_]:particles)result.push_back(id);return result;
}
std::vector<std::uint32_t> ParticleEngine::neighbors(const std::string& systemId,std::uint32_t id,double radius,std::size_t limit){
  impl_->phase();const auto& system=impl_->system(systemId);
  if(!std::isfinite(radius)||radius<=0||radius>system.cellSize)throw Error({},"neighbor radius must be finite, positive and no greater than cellSize");
  if(limit<1||limit>64)throw Error({},"neighbor limit must be between 1 and 64");
  const auto origin=system.snapshot.find(id);if(origin==system.snapshot.end())throw Error({},"neighbor origin is not in the phase snapshot");
  const auto cell=cellOf(origin->second.position,system.cellSize);
  std::vector<const std::vector<std::uint32_t>*> buckets;std::size_t candidates=0;
  for(std::int64_t x=-1;x<=1;++x)for(std::int64_t y=-1;y<=1;++y)for(std::int64_t z=-1;z<=1;++z){
    const auto found=system.grid.find({cell[0]+x,cell[1]+y,cell[2]+z});
    if(found!=system.grid.end()){candidates+=found->second.size();buckets.push_back(&found->second);}
  }
  if(candidates>Limits::particleCandidateChecks-impl_->candidateChecks)throw Error({},"particle neighbor candidate budget exceeded (maximum 100000 per phase)");
  impl_->candidateChecks+=candidates;
  std::vector<std::pair<double,std::uint32_t>> closest;
  for(const auto* bucket:buckets)for(const auto candidate:*bucket){
    if(candidate==id)continue;const auto& position=system.snapshot.at(candidate).position;const auto& start=origin->second.position;
    const auto distance=std::hypot(position[0]-start[0],position[1]-start[1],position[2]-start[2]);
    if(distance<=radius)closest.emplace_back(distance,candidate);
  }
  std::sort(closest.begin(),closest.end());if(closest.size()>limit)closest.resize(limit);
  std::vector<std::uint32_t> result;result.reserve(closest.size());for(const auto& [_,candidate]:closest)result.push_back(candidate);return result;
}
std::size_t ParticleEngine::count(const std::string& systemId) const {
  const auto& system=impl_->system(systemId);return (impl_->active?system.staged:system.live).size();
}
const std::map<std::uint32_t,Particle>& ParticleEngine::live(std::size_t nodeIndex) const {
  static const Store empty;
  const auto found=impl_->byNode.find(nodeIndex);return found==impl_->byNode.end()?empty:impl_->systems[found->second].live;
}
} // namespace linkworld