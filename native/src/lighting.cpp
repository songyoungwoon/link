#include "link/lighting.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace linkworld {
namespace {
using Map=Value::Map;
const Map& map(const Value& value,Location where,const char* field){
  const auto* result=std::get_if<Map>(&value.data);
  if(!result)throw Error(where,std::string(field)+" must be a map");
  return *result;
}
void keys(const Map& values,std::initializer_list<std::string_view> allowed,Location where,const char* field){
  for(const auto& [key,_]:values)
    if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())throw Error(where,std::string("unsupported ")+field+" field '"+key+"'");
}
double number(const Value& value,double minimum,double maximum,Location where,const std::string& field){
  const auto* result=std::get_if<double>(&value.data);
  if(!result||!std::isfinite(*result)||*result<minimum||*result>maximum)
    throw Error(where,field+" must be a finite number between "+std::to_string(minimum)+" and "+std::to_string(maximum));
  return *result;
}
std::string color(const Value& value,Location where){validateStyle({{"color",value}},where);return value.string();}
bool boolean(const Value& value,Location where,const char* field){
  const auto* result=std::get_if<bool>(&value.data);
  if(!result)throw Error(where,std::string(field)+" must be bool");
  return *result;
}
std::string environmentMap(const Value& value,Location where){
  const auto* result=std::get_if<std::string>(&value.data);
  if(!result||result->empty()||result->size()>2048)throw Error(where,"environmentMap must be a nonempty relative .hdr path of at most 2048 bytes");
  if(result->front()=='/'||std::any_of(result->begin(),result->end(),[](unsigned char c){return c<32||c==127||c=='\\'||c==':'||c=='?'||c=='#'||c=='@';}))
    throw Error(where,"environmentMap must be a relative .hdr path inside the world");
  for(std::size_t start=0;start<result->size();){const auto end=result->find('/',start);const auto count=end==std::string::npos?result->size()-start:end-start;
    if(result->compare(start,count,"..")==0)throw Error(where,"environmentMap parent traversal is forbidden");
    if(end==std::string::npos)break;start=end+1;
  }
  if(result->size()<5)throw Error(where,"environmentMap must name a .hdr resource");
  std::string suffix=result->substr(result->size()-4);std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c-'A'+'a':c);});
  if(suffix!=".hdr")throw Error(where,"environmentMap must name a .hdr resource");
  return *result;
}
}
LightingSettings parseLightingSettings(const Value& value,Location where){
  const auto& values=map(value,where,"lighting");
  keys(values,{"sunDirection","sunColor","skyColor","groundColor","sunIntensity","ambientIntensity","exposure","environmentMap","environmentIntensity","shadow","points"},where,"lighting");
  LightingSettings result;
  for(const auto& [key,item]:values){
    if(key=="sunDirection")result.sunDirection=valueVec3(item,where);
    else if(key=="sunColor")result.sunColor=color(item,where);
    else if(key=="skyColor")result.skyColor=color(item,where);
    else if(key=="groundColor")result.groundColor=color(item,where);
    else if(key=="sunIntensity")result.sunIntensity=number(item,0,20,where,key);
    else if(key=="ambientIntensity")result.ambientIntensity=number(item,0,4,where,key);
    else if(key=="exposure")result.exposure=number(item,.1,8,where,key);
    else if(key=="environmentMap")result.environmentMap=environmentMap(item,where);
    else if(key=="environmentIntensity")result.environmentIntensity=number(item,0,8,where,key);
    else if(key=="shadow"){
      const auto& shadow=map(item,where,"lighting shadow");
      keys(shadow,{"enabled","resolution","center","extent","bias","normalBias"},where,"lighting shadow");
      for(const auto& [field,setting]:shadow){
        if(field=="enabled")result.shadow.enabled=boolean(setting,where,"shadow.enabled");
        else if(field=="center")result.shadow.center=valueVec3(setting,where);
        else if(field=="extent")result.shadow.extent=number(setting,1,100,where,"shadow.extent");
        else if(field=="bias")result.shadow.bias=number(setting,0,.01,where,"shadow.bias");
        else if(field=="normalBias")result.shadow.normalBias=number(setting,0,.1,where,"shadow.normalBias");
        else if(field=="resolution"){
          const auto resolution=number(setting,512,2048,where,"shadow.resolution");
          if(resolution!=512&&resolution!=1024&&resolution!=2048)throw Error(where,"shadow.resolution must be 512, 1024 or 2048");
          result.shadow.resolution=static_cast<unsigned>(resolution);
        }
      }
    }else if(key=="points"){
      const auto* points=std::get_if<Value::Array>(&item.data);
      if(!points||points->size()>4)throw Error(where,"lighting points must be an array of at most four point lights");
      for(const auto& point:*points){
        const auto& fields=map(point,where,"point light");keys(fields,{"position","color","intensity","range"},where,"point light");PointLight light;
        for(const auto& [field,setting]:fields){
          if(field=="position")light.position=valueVec3(setting,where);
          else if(field=="color")light.color=color(setting,where);
          else if(field=="intensity")light.intensity=number(setting,0,100,where,"point light intensity");
          else if(field=="range")light.range=number(setting,.1,50,where,"point light range");
        }
        result.points.push_back(std::move(light));
      }
    }
  }
  const double length=std::hypot(result.sunDirection[0],result.sunDirection[1],result.sunDirection[2]);
  if(!(length>0)||!std::isfinite(length))throw Error(where,"sunDirection must be a nonzero vector");
  for(auto& component:result.sunDirection)component/=length;
  return result;
}
} // namespace linkworld
