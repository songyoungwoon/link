#include "link/core.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace linkworld {
namespace {
std::string locationText(const Location& location) {
  return (location.file.empty() ? "<value>" : location.file)+":"+std::to_string(location.line)+":"+std::to_string(location.column);
}
template<class T> const T& as(const Value& value,const char* description) {
  if (auto found=std::get_if<T>(&value.data)) return *found;
  throw Error({},std::string("expected ")+description);
}
std::string quote(const std::string& value) {
  std::string result="\"";
  for (unsigned char character:value) {
    switch(character) { case '"':result+="\\\"";break;case '\\':result+="\\\\";break;case '\n':result+="\\n";break;case '\r':result+="\\r";break;case '\t':result+="\\t";break;
      default: if(character<32) result+='?'; else result+=static_cast<char>(character); }
  }
  return result+'"';
}
std::string displayValue(const Value& value,std::size_t depth) {
  if(depth>Limits::parseDepth) throw Error({},"value display depth exceeded");
  if(std::holds_alternative<std::nullptr_t>(value.data)) return "null";
  if(auto number=std::get_if<double>(&value.data)) { std::ostringstream out;out<<std::setprecision(15)<<*number;return out.str(); }
  if(auto boolean=std::get_if<bool>(&value.data)) return *boolean?"true":"false";
  if(auto string=std::get_if<std::string>(&value.data)) return quote(*string);
  std::string result;
  if(auto array=std::get_if<Value::Array>(&value.data)) {
    result="["; for(const auto& item:*array) { if(result.size()>1)result+=", ";result+=displayValue(item,depth+1); } return result+"]";
  }
  result="{";for(const auto& [key,item]:std::get<Value::Map>(value.data)) { if(result.size()>1)result+=", ";result+=quote(key)+": "+displayValue(item,depth+1); }return result+"}";
}
double scalar(const Value& value,const std::string& name,Location where) {
  if(!std::holds_alternative<double>(value.data)) throw Error(where,name+" must be a number");
  const auto result=value.number();if(!std::isfinite(result)||result<0||result>1)throw Error(where,name+" must be finite and between 0 and 1");return result;
}
void apply(Material& material,const StylePatch& properties) {
  for(const auto& [name,value]:properties) {
    if(name=="color")material.color=value.string();else if(name=="roughness")material.roughness=value.number();
    else if(name=="metalness")material.metalness=value.number();else if(name=="opacity")material.opacity=value.number();
    else if(name=="emission")material.emission=value.number();
    else if(name=="wireframe")material.wireframe=value.boolean();
    else if(name=="textBackground")material.textBackground=value.boolean();
  }
}
}
Error::Error(Location where,const std::string& message):std::runtime_error(locationText(where)+": "+message),location(std::move(where)){}
double Value::number() const {const auto result=as<double>(*this,"number");if(!std::isfinite(result))throw Error({},"nonfinite number");return result;}
bool Value::boolean() const {return as<bool>(*this,"bool");}
const std::string& Value::string() const {return as<std::string>(*this,"string");}
const Value::Array& Value::array() const {return as<Array>(*this,"array");}
const Value::Map& Value::map() const {return as<Map>(*this,"map");}
Value::Map& Value::map() {if(auto result=std::get_if<Map>(&data))return *result;throw Error({},"expected map");}
std::string Value::display() const { return displayValue(*this,0); }
std::size_t Value::bytes(std::size_t depth) const {
  if(depth>Limits::parseDepth)throw Error({},"value depth budget exceeded");
  std::size_t result=1;
  const auto add=[&](std::size_t amount) { if(amount>Limits::documentBytes || result>Limits::documentBytes-amount)throw Error({},"value size budget exceeded");result+=amount; };
  if(auto value=std::get_if<double>(&data)) {if(!std::isfinite(*value))throw Error({},"nonfinite number");return sizeof(double);}
  if(std::holds_alternative<bool>(data))return 1;
  if(auto value=std::get_if<std::string>(&data)){if(value->size()>Limits::stringBytes)throw Error({},"string byte budget exceeded");return value->size()+1;}
  if(auto value=std::get_if<Array>(&data))for(const auto& child:*value)add(child.bytes(depth+1));
  if(auto value=std::get_if<Map>(&data))for(const auto& [key,child]:*value){if(key.size()>Limits::stringBytes)throw Error({},"map key byte budget exceeded");add(key.size()+1);add(child.bytes(depth+1));}
  return result;
}
void validateStyle(const StylePatch& properties,Location where) {
  for(const auto& [name,value]:properties) {
    if(name=="color") {
      if(!std::holds_alternative<std::string>(value.data))throw Error(where,"color must be #RRGGBB");
      const auto& color=value.string();
      if(color.size()!=7 || color[0]!='#' || !std::all_of(color.begin()+1,color.end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))throw Error(where,"color must be #RRGGBB");
    } else if(name=="roughness"||name=="metalness"||name=="opacity") {scalar(value,name,where);}
    else if(name=="emission") {if(!std::holds_alternative<double>(value.data))throw Error(where,"emission must be a number");const auto intensity=value.number();if(intensity<0||intensity>4)throw Error(where,"emission must be finite and between 0 and 4");}
    else if(name=="wireframe"||name=="textBackground") {if(!std::holds_alternative<bool>(value.data))throw Error(where,name+" must be bool");}
    else throw Error(where,"unsupported Style property '"+name+"'");
  }
}
Material resolveStyle(const NodeDefinition& node,const World& world,const StylePatch& overrideStyle) {
  Material result;apply(result,world.defaults);
  if(!node.style.empty()){const auto named=world.materials.find(node.style);if(named==world.materials.end())throw Error(node.location,"unknown material '"+node.style+"'");apply(result,named->second);}
  for(const auto& rule:world.rules)if(rule.kind==StyleRule::Kind::Class&&std::find(node.classes.begin(),node.classes.end(),rule.selector)!=node.classes.end())apply(result,rule.properties);
  for(const auto& rule:world.rules)if(rule.kind==StyleRule::Kind::Id&&rule.selector==node.id)apply(result,rule.properties);
  validateStyle(overrideStyle,node.location);apply(result,overrideStyle);return result;
}
Vec3 valueVec3(const Value& value,Location where) {
  if(!std::holds_alternative<Value::Array>(value.data)||value.array().size()!=3)throw Error(where,"expected array of three finite numbers");
  Vec3 result{};for(std::size_t i=0;i<3;++i){if(!std::holds_alternative<double>(value.array()[i].data)||!std::isfinite(std::get<double>(value.array()[i].data)))throw Error(where,"expected array of three finite numbers");result[i]=value.array()[i].number();if(std::abs(result[i])>1e6)throw Error(where,"coordinate magnitude exceeds 1000000");}return result;
}
Value vecValue(const Vec3& vector) {return Value::Array{vector[0],vector[1],vector[2]};}
} // namespace linkworld
