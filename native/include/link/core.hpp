#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace linkworld {
struct Location { std::string file; std::size_t line=1, column=1; };
class Error : public std::runtime_error {
public:
  Location location;
  Error(Location where, const std::string& message);
};
struct Value {
  using Array=std::vector<Value>;
  using Map=std::map<std::string,Value>;
  std::variant<std::nullptr_t,double,bool,std::string,Array,Map> data=nullptr;
  Value()=default; Value(std::nullptr_t):data(nullptr){} Value(double n):data(n){} Value(int n):data(double(n)){}
  Value(bool b):data(b){} Value(std::string s):data(std::move(s)){} Value(const char* s):data(std::string(s)){}
  Value(Array a):data(std::move(a)){} Value(Map m):data(std::move(m)){}
  double number() const; bool boolean() const; const std::string& string() const;
  const Array& array() const; const Map& map() const; Map& map();
  std::string display() const;
  std::size_t bytes(std::size_t depth=0) const;
  bool operator==(const Value&) const=default;
};
inline constexpr std::size_t noParent=static_cast<std::size_t>(-1);
using Vec3=std::array<double,3>;
struct Transform { Vec3 position{0,0,0},rotation{0,0,0},scale{1,1,1}; bool visible=true; };
struct Material { std::string color="#c4d9d0"; double roughness=.65,metalness=0,opacity=1,emission=0; bool wireframe=false,textBackground=true; };
using StylePatch=Value::Map;
struct StyleRule { enum class Kind { Class,Id }; Kind kind; std::string selector; StylePatch properties; };
struct Geometry {
  enum class Type { Box,Sphere,Plane,Cylinder,Frame,Particles };
  Type type=Type::Box; Vec3 size{1,1,1}; double radius=.5;
  double radiusTop=.5,height=1,border=.06,depth=.08;
  std::size_t capacity=256; double cellSize=.6;
};
enum class Kind { Object,Component,Element };
struct NodeDefinition {
  std::string id,name; Kind kind=Kind::Object; std::size_t parent=noParent;
  Transform transform; Geometry geometry; std::string style; std::vector<std::string> classes;
  Value::Map state,config; std::string script; Location location;
  std::string image,text;
};
struct ScriptProgram;
struct Viewpoint { std::string name; Vec3 position,target; };
struct WalkSettings { Vec3 min{-3,0,-30},max{3,4,3}; double eyeHeight=1.65,speed=3.4; };
struct Portal { std::string id,address; Vec3 position,size; };
struct FogSettings { std::string color="#111c21"; double start=80,end=250; };
struct World {
  // Owns remote snapshot files for every renderer and viewer using this World.
  std::shared_ptr<void> resourceOwner;
  std::string address,id,name; std::filesystem::path directory;
  Vec3 eye{7,6,9},target{0,1,0}; std::vector<NodeDefinition> nodes;
  StylePatch defaults; std::map<std::string,StylePatch> materials; std::vector<StyleRule> rules;
  std::map<std::string,std::shared_ptr<const ScriptProgram>> programs;
  std::map<std::string,std::filesystem::path> images;
  std::vector<Viewpoint> views;
  std::optional<WalkSettings> walk;
  std::vector<Portal> portals;
  std::vector<std::string> extensions;
  std::string background="#111c21";
  std::optional<FogSettings> fog;
  bool grid=true;
};
struct Limits {
  static constexpr std::size_t remoteWorldBytes=64*1024*1024;
  static constexpr std::size_t manifestBytes=65536, documentBytes=1048576, scriptBytes=131072;
  static constexpr std::size_t tokens=40000, astNodes=20000, parseDepth=64, nodes=500,nodeDepth=16,scripts=16;
  static constexpr std::size_t stringBytes=8192,stateBytes=65536,evalSteps=4096,callDepth=16,storageBytes=65536;
  static constexpr std::size_t worldParticles=8192,particleCapacity=2048,particleStateBytes=1024;
  static constexpr std::size_t particleStateValues=64,particleStateDepth=16;
  static constexpr std::size_t particleCandidateChecks=100000,particleMutations=4096,particleSpawns=256,particleLifetimeIds=1000000;
  static constexpr std::size_t imageBytes=16*1024*1024,images=128,textBytes=1024,views=128,portals=16;
};
using LogFn=std::function<void(bool error,const std::string& message)>;
void validateStyle(const StylePatch&,Location={});
Material resolveStyle(const NodeDefinition&,const World&,const StylePatch& overrideStyle={});
Vec3 valueVec3(const Value&,Location={});
Value vecValue(const Vec3&);
} // namespace linkworld
