#pragma once
#include "core.hpp"
#include <string_view>
namespace linkworld {
std::shared_ptr<const ScriptProgram> parseScript(std::string_view source,const std::string& file);
class ScriptInstance {
public:
  using Api=std::function<Value(const std::string& method,const std::vector<Value>& arguments)>;
  ScriptInstance(std::shared_ptr<const ScriptProgram>,Api);
  ~ScriptInstance();
  ScriptInstance(const ScriptInstance&)=delete;
  ScriptInstance& operator=(const ScriptInstance&)=delete;
  void invoke(const std::string& hook,const std::vector<Value>& arguments,
              std::size_t* sharedRemaining=nullptr,std::size_t maxSteps=Limits::evalSteps);
  const Value::Map& storage() const;
private: struct Impl; std::unique_ptr<Impl> impl_;
};
} // namespace linkworld
