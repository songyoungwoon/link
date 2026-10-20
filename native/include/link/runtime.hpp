#pragma once
#include "core.hpp"
#include "script.hpp"
namespace linkworld {
struct RuntimeNode { NodeDefinition definition; Transform transform; Value::Map state; StylePatch overrideStyle; };
struct RuntimeCounters { std::size_t activeWorlds=0,activeBindings=0,createdWorlds=0,destroyedWorlds=0,updates=0; };
RuntimeCounters counters();
class Runtime {
public:
  explicit Runtime(std::shared_ptr<const World>,LogFn log={});
  ~Runtime();
  Runtime(const Runtime&)=delete; Runtime& operator=(const Runtime&)=delete;
  void start(); void update(double seconds);
  std::string interact(const std::string& elementId);
  const World& world() const;
  const std::vector<RuntimeNode>& nodes() const;
  RuntimeNode& find(const std::string& id);
  const RuntimeNode& find(const std::string& id) const;
  Material material(std::size_t index) const;
  Value::Map storage(const std::string& bindingId) const;
  std::size_t activeBindings() const;
private:
  struct Binding; std::shared_ptr<const World> world_; std::vector<RuntimeNode> nodes_;
  std::map<std::string,std::size_t> index_; std::vector<std::unique_ptr<Binding>> bindings_; LogFn log_;
  bool started_=false;
  Value api(std::size_t binding,const std::string&,const std::vector<Value>&);
  void invoke(Binding&,const std::string&,const std::vector<Value>&);
};
} // namespace linkworld
