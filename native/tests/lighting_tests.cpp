#include "link/lighting.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace linkworld;
namespace {
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
void close(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-10,message);}
void rejects(const Value& value,const char* message){bool rejected=false;try{parseLightingSettings(value,{"world.link",12,4});}catch(const Error& error){rejected=true;require(error.location.file=="world.link"&&error.location.line==12&&error.location.column==4,"Lighting validation retains source location");}require(rejected,message);}
void defaults(){
 const auto actual=parseLightingSettings(Value::Map{});const LightingSettings expected;
 close(std::hypot(actual.sunDirection[0],actual.sunDirection[1],actual.sunDirection[2]),1,"Default sun direction normalized");
 require(actual.sunColor==expected.sunColor&&actual.skyColor==expected.skyColor&&actual.groundColor==expected.groundColor,"Default colors retained");
 require(actual.sunIntensity==expected.sunIntensity&&actual.ambientIntensity==expected.ambientIntensity&&actual.exposure==expected.exposure&&actual.environmentIntensity==expected.environmentIntensity,"Default intensities retained");
 require(actual.shadow.enabled&&actual.shadow.resolution==2048&&actual.shadow.center==expected.shadow.center&&actual.shadow.extent==16&&actual.shadow.bias==expected.shadow.bias&&actual.shadow.normalBias==expected.shadow.normalBias,"Default shadow retained");
 require(actual.environmentMap.empty()&&actual.environmentFile.empty()&&!actual.environment&&actual.points.empty(),"Parser does not touch filesystem or decode resources");
 const auto empty=parseLightingSettings(Value::Map{{"shadow",Value::Map{}},{"points",Value::Array{}}});require(empty.points.empty()&&empty.shadow.enabled,"Empty optional settings preserve defaults");
}
void authored(){
 const auto actual=parseLightingSettings(Value::Map{
  {"sunDirection",vecValue({0,3,4})},{"sunColor","#AaBbCc"},{"skyColor","#123456"},{"groundColor","#654321"},
  {"sunIntensity",5},{"ambientIntensity",.25},{"exposure",1.7},{"environmentMap","lighting/stone-courtyard.HDR"},{"environmentIntensity",.8},
  {"shadow",Value::Map{{"enabled",false},{"resolution",1024},{"center",vecValue({1,2,-3})},{"extent",22},{"bias",.001},{"normalBias",.02}}},
  {"points",Value::Array{Value::Map{{"position",vecValue({1,2,3})},{"color","#FF8040"},{"intensity",45},{"range",8}},Value::Map{}}}
 });
 close(actual.sunDirection[1],.6,"Sun direction normalized y");close(actual.sunDirection[2],.8,"Sun direction normalized z");
 require(actual.sunColor=="#AaBbCc"&&actual.skyColor=="#123456"&&actual.groundColor=="#654321","Color spelling preserved");
 close(actual.sunIntensity,5,"Sun intensity retained");close(actual.ambientIntensity,.25,"Ambient intensity retained");close(actual.exposure,1.7,"Exposure retained");close(actual.environmentIntensity,.8,"Environment intensity retained");require(actual.environmentMap=="lighting/stone-courtyard.HDR","Relative HDR resource name retained");
 require(!actual.shadow.enabled&&actual.shadow.resolution==1024&&actual.shadow.center==Vec3{1,2,-3},"Authored shadow values retained");close(actual.shadow.extent,22,"Extent retained");close(actual.shadow.bias,.001,"Bias retained");close(actual.shadow.normalBias,.02,"Normal bias retained");
 require(actual.points.size()==2&&actual.points[0].position==Vec3{1,2,3}&&actual.points[0].color=="#FF8040"&&actual.points[0].intensity==45&&actual.points[0].range==8,"Point light settings retained");
 const PointLight point;require(actual.points[1].position==point.position&&actual.points[1].color==point.color&&actual.points[1].intensity==point.intensity&&actual.points[1].range==point.range,"Point light empty map uses struct defaults");
 const auto small=parseLightingSettings(Value::Map{{"sunDirection",vecValue({0,1e-250,0})}});close(small.sunDirection[1],1,"Finite nonzero tiny sun direction is normalized without overflow");
}
void limits(){
 struct Numeric{const char* key;double minimum,maximum;};
 for(const auto& setting:{Numeric{"sunIntensity",0,20},Numeric{"ambientIntensity",0,4},Numeric{"exposure",.1,8},Numeric{"environmentIntensity",0,8}}){
  parseLightingSettings(Value::Map{{setting.key,setting.minimum}});parseLightingSettings(Value::Map{{setting.key,setting.maximum}});
  for(double bad:{setting.minimum-.01,setting.maximum+.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})rejects(Value::Map{{setting.key,bad}},"Lighting number range and finite checks");
  rejects(Value::Map{{setting.key,"1"}},"Numeric lighting fields reject strings");rejects(Value::Map{{setting.key,true}},"Numeric lighting fields reject bool");
 }
 for(const auto& setting:{Numeric{"extent",1,100},Numeric{"bias",0,.01},Numeric{"normalBias",0,.1}}){
  parseLightingSettings(Value::Map{{"shadow",Value::Map{{setting.key,setting.minimum}}}});parseLightingSettings(Value::Map{{"shadow",Value::Map{{setting.key,setting.maximum}}}});
  for(double bad:{setting.minimum-.001,setting.maximum+.001,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})rejects(Value::Map{{"shadow",Value::Map{{setting.key,bad}}}},"Shadow numeric bounds and finite checks");
 }
 for(int resolution:{512,1024,2048})require(parseLightingSettings(Value::Map{{"shadow",Value::Map{{"resolution",resolution}}}}).shadow.resolution==static_cast<unsigned>(resolution),"Supported shadow resolutions accepted");
 for(double resolution:{0.,511.,512.5,768.,4096.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})rejects(Value::Map{{"shadow",Value::Map{{"resolution",resolution}}}},"Unsupported shadow resolutions rejected");
 for(const auto& setting:{Numeric{"intensity",0,100},Numeric{"range",.1,50}}){
  parseLightingSettings(Value::Map{{"points",Value::Array{Value::Map{{setting.key,setting.minimum}}}}});parseLightingSettings(Value::Map{{"points",Value::Array{Value::Map{{setting.key,setting.maximum}}}}});
  for(double bad:{setting.minimum-.01,setting.maximum+.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})rejects(Value::Map{{"points",Value::Array{Value::Map{{setting.key,bad}}}}},"Point light numeric bounds and finite checks");
 }
 Value::Array four(4,Value::Map{});require(parseLightingSettings(Value::Map{{"points",four}}).points.size()==4,"Four lights accepted");four.emplace_back(Value::Map{});rejects(Value::Map{{"points",four}},"Fifth point light rejected");
}
void malformed(){
 rejects(Value(),"Null lighting rejected");rejects(Value::Array{},"Array lighting rejected");rejects(Value::Map{{"sunIntensitty",3}},"Unknown top-level field rejected");
 for(const auto* key:{"sunColor","skyColor","groundColor"})for(const Value& invalid:{Value("white"),Value("#FFF"),Value("#00000000"),Value("#GG0000"),Value(123),Value()})rejects(Value::Map{{key,invalid}},"Lighting colors require six hexadecimal digits");
 rejects(Value::Map{{"sunDirection",vecValue({0,0,0})}},"Zero sun direction rejected");
 for(const Value& invalid:std::vector<Value>{Value::Array{1,2},Value::Array{0,1,std::numeric_limits<double>::quiet_NaN()},Value::Array{0,std::numeric_limits<double>::infinity(),0},Value::Array{0,1000001,0},Value("up")})rejects(Value::Map{{"sunDirection",invalid}},"Sun direction shape and finite components enforced");
 rejects(Value::Map{{"shadow",true}},"Shadow must be a map");rejects(Value::Map{{"shadow",Value::Map{{"quality","high"}}}},"Unknown shadow field rejected");rejects(Value::Map{{"shadow",Value::Map{{"enabled",1}}}},"Shadow enabled requires bool");rejects(Value::Map{{"shadow",Value::Map{{"center",Value::Array{0,1}}}}},"Shadow center requires Vec3");
 rejects(Value::Map{{"points",Value::Map{}}},"Points must be array");rejects(Value::Map{{"points",Value::Array{Value(1)}}},"Each point must be map");rejects(Value::Map{{"points",Value::Array{Value::Map{{"shadow",true}}}}},"Unknown point field rejected");rejects(Value::Map{{"points",Value::Array{Value::Map{{"position",Value::Array{0,1}}}}}},"Point position requires Vec3");rejects(Value::Map{{"points",Value::Array{Value::Map{{"color","orange"}}}}},"Point color validated");
 for(const auto* path:{"","map.png",".hdr","/outside.hdr","C:/outside.hdr","../outside.hdr","env/../outside.hdr","env\\outside.hdr","https://host/sky.hdr","sky.hdr?token=1","sky.hdr#part"})rejects(Value::Map{{"environmentMap",path}},"Environment map must be a relative HDR resource");
 rejects(Value::Map{{"environmentMap",1}},"Environment path must be string");rejects(Value::Map{{"environmentMap",std::string("map\0.hdr",8)}},"Environment path rejects embedded NUL");rejects(Value::Map{{"environmentMap",std::string(2049,'a')+".hdr"}},"Environment path byte limit enforced");
 require(parseLightingSettings(Value::Map{{"environmentMap","환경/하늘.hdr"}}).environmentMap=="환경/하늘.hdr","UTF-8 relative resource names retained");
}
}
int main(){try{defaults();authored();limits();malformed();std::cout<<"lighting-v1 settings, bounds and malformed input PASS\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
