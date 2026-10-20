#include "link/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace linkworld {
namespace {
RuntimeCounters count;
void countArgs(const std::vector<Value>& args,std::size_t minimum,std::size_t maximum) {
  if(args.size()<minimum||args.size()>maximum)throw std::runtime_error("Invalid API argument count");
}
void validateState(const Value::Map& state) {if(Value(state).bytes()>Limits::stateBytes)throw std::runtime_error("State byte budget exceeded");}
Transform applyTransform(Transform transform,const Value::Map& patch) {
  for(const auto& [key,value]:patch) {
    if(key=="position")transform.position=valueVec3(value);
    else if(key=="rotation")transform.rotation=valueVec3(value);
    else if(key=="scale") {transform.scale=valueVec3(value);for(double n:transform.scale)if(n<=0||n>1000)throw std::runtime_error("Scale must be > 0 and <= 1000");}
    else if(key=="visible")transform.visible=value.boolean();
    else throw std::runtime_error("Unknown transform field: "+key);
  }
  return transform;
}
}
RuntimeCounters counters(){return count;}
struct Runtime::Binding {
  std::size_t node=0; std::string source; std::unique_ptr<ScriptInstance> instance;
  Value context; bool active=true,destroyed=false;
};
Runtime::Runtime(std::shared_ptr<const World> world,LogFn log):world_(std::move(world)),log_(std::move(log)) {
  for(const auto& definition:world_->nodes) {
    if(index_.contains(definition.id))throw Error(definition.location,"Duplicate runtime ID: "+definition.id);
    index_.emplace(definition.id,nodes_.size());
    nodes_.push_back({definition,definition.transform,definition.state,{}});
  }
  for(std::size_t i=0;i<nodes_.size();++i) {
    const auto& definition=nodes_[i].definition;
    if(definition.script.empty())continue;
    const auto program=world_->programs.find(definition.script);
    if(program==world_->programs.end())throw Error(definition.location,"Missing script binding: "+definition.script);
    const auto bindingIndex=bindings_.size();
    auto binding=std::make_unique<Binding>();binding->node=i;binding->source=definition.script;
    binding->context=Value::Map{{"id",definition.id},{"config",definition.config}};
    binding->instance=std::make_unique<ScriptInstance>(program->second,[this,bindingIndex](const std::string& method,const std::vector<Value>& args){return api(bindingIndex,method,args);});
    bindings_.push_back(std::move(binding));
  }
  ++count.activeWorlds;++count.createdWorlds;count.activeBindings+=bindings_.size();
}
Runtime::~Runtime() {
  for(auto it=bindings_.rbegin();it!=bindings_.rend();++it){auto& binding=**it;
    if(!binding.destroyed){binding.destroyed=true;invoke(binding,"onDestroy",{binding.context});}
  }
  count.activeBindings-=bindings_.size();--count.activeWorlds;++count.destroyedWorlds;
}
void Runtime::invoke(Binding& binding,const std::string& hook,const std::vector<Value>& args) {
  try{binding.instance->invoke(hook,args);}
  catch(const std::exception& error){binding.active=false;
    if(log_)log_(true,"[node="+nodes_[binding.node].definition.id+" binding="+binding.source+" hook="+hook+"] "+error.what());
  }
}
void Runtime::start() {
  if(started_)return;started_=true;
  for(auto& binding:bindings_)if(binding->active)invoke(*binding,"onStart",{binding->context});
}
void Runtime::update(double seconds) {
  if(!started_)return;
  const double delta=std::isfinite(seconds)?std::clamp(seconds,0.0,.05):0.0;
  ++count.updates;
  for(auto& binding:bindings_)if(binding->active)invoke(*binding,"onUpdate",{binding->context,delta});
}
std::string Runtime::interact(const std::string& elementId) {
  auto found=index_.find(elementId);if(found==index_.end())throw std::runtime_error("No clicked node: "+elementId);
  auto i=found->second;
  while(i!=noParent) {
    auto binding=std::find_if(bindings_.begin(),bindings_.end(),[i](const auto& b){return b->node==i;});
    if(binding!=bindings_.end()) {
      auto& b=**binding;
      if(b.active)invoke(b,"onInteract",{b.context,Value::Map{{"type","click"},{"targetId",elementId},{"bindingId",nodes_[i].definition.id}}});
      return nodes_[i].definition.id;
    }
    i=nodes_[i].definition.parent;
  }
  return elementId;
}
const World& Runtime::world()const{return *world_;}
const std::vector<RuntimeNode>& Runtime::nodes()const{return nodes_;}
RuntimeNode& Runtime::find(const std::string& id) {auto found=index_.find(id);if(found==index_.end())throw std::runtime_error("Node not found in current world: "+id);return nodes_[found->second];}
const RuntimeNode& Runtime::find(const std::string& id)const {auto found=index_.find(id);if(found==index_.end())throw std::runtime_error("Node not found in current world: "+id);return nodes_[found->second];}
Material Runtime::material(std::size_t i)const {const auto& node=nodes_.at(i);return resolveStyle(node.definition,*world_,node.overrideStyle);}
Value::Map Runtime::storage(const std::string& id)const {for(const auto& binding:bindings_)if(nodes_[binding->node].definition.id==id)return binding->instance->storage();return {};}
std::size_t Runtime::activeBindings()const {return std::count_if(bindings_.begin(),bindings_.end(),[](const auto& b){return b->active;});}
Value Runtime::api(std::size_t bindingIndex,const std::string& method,const std::vector<Value>& args) {
  auto& binding=*bindings_.at(bindingIndex);const auto& self=nodes_[binding.node].definition.id;
  const auto target=[&](std::size_t argument){return args.size()>argument?args[argument].string():self;};
  if(method=="find"){countArgs(args,1,1);return find(args[0].string()).definition.id;}
  if(method=="getState"){countArgs(args,0,1);return find(target(0)).state;}
  if(method=="setState") {
    countArgs(args,1,2);auto& node=find(args.size()==2?args[0].string():self);
    if(node.definition.kind==Kind::Element)throw std::runtime_error("Element has no state");
    auto next=node.state;for(const auto& [key,value]:args.back().map())next.insert_or_assign(key,value);validateState(next);node.state=std::move(next);return {};
  }
  if(method=="getTransform") {
    countArgs(args,0,1);const auto& t=find(target(0)).transform;
    return Value::Map{{"position",vecValue(t.position)},{"rotation",vecValue(t.rotation)},{"scale",vecValue(t.scale)},{"visible",t.visible}};
  }
  if(method=="setTransform") {
    countArgs(args,1,2);auto& node=find(args.size()==2?args[0].string():self);node.transform=applyTransform(node.transform,args.back().map());return {};
  }
  if(method=="setStyle") {
    countArgs(args,2,2);auto& node=find(args[0].string());if(node.definition.kind!=Kind::Element)throw std::runtime_error("Style overrides require an Element");
    const auto& patch=args[1].map();validateStyle(patch);auto next=node.overrideStyle;for(const auto& [key,value]:patch)next.insert_or_assign(key,value);node.overrideStyle=std::move(next);return {};
  }
  if(method=="clearStyle"){countArgs(args,1,1);auto& node=find(args[0].string());if(node.definition.kind!=Kind::Element)throw std::runtime_error("Style overrides require an Element");node.overrideStyle.clear();return {};}
  if(method=="log"){countArgs(args,1,1);if(log_)log_(false,"["+self+"] "+args[0].display());return {};}
  throw std::runtime_error("Unsupported Script API: ctx."+method);
}
} // namespace linkworld
