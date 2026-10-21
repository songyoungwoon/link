#include "link/loader.hpp"
#include "link/remote.hpp"
#include "link/script.hpp"
#include "link/text.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <random>
#include <set>
#include <string_view>

namespace linkworld {
namespace {
std::string pathText(const std::filesystem::path& path){const auto encoded=path.u8string();return std::string(encoded.begin(),encoded.end());}
std::filesystem::path utf8Path(std::string_view text,Location where){std::u8string encoded;encoded.reserve(text.size());for(unsigned char byte:text)encoded.push_back(static_cast<char8_t>(byte));try{return std::filesystem::path(encoded);}catch(const std::exception&){throw Error(std::move(where),"invalid UTF-8 resource path");}}
bool identifier(const std::string& text) {
  const auto initial=[](unsigned char c){return(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';};
  if(text.empty()||!initial(text[0]))return false;
  return std::all_of(text.begin()+1,text.end(),[&](unsigned char c){return initial(c)||(c>='0'&&c<='9');});
}
std::string decode(const std::string& input,Location where) {
  if(input.empty()||input.size()>2048)throw Error(where,"empty or oversized LINK path");
  const auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
  std::string result;
  for(std::size_t i=0;i<input.size();++i){unsigned char c=input[i];
    if(c=='%') {if(i+2>=input.size()||hex(input[i+1])<0||hex(input[i+2])<0)throw Error(where,"malformed percent encoding");c=static_cast<unsigned char>(hex(input[i+1])*16+hex(input[i+2]));i+=2;if(c=='/'||c=='\\')throw Error(where,"encoded path separator is forbidden");}
    if(c<32||c==127||c=='\\'||c==':'||c=='?'||c=='#'||c=='%'||c=='@')throw Error(where,"forbidden character in LINK path");
    result+=static_cast<char>(c);
  }
  return result;
}
bool sameComponent(const std::filesystem::path& a,const std::filesystem::path& b) {
#ifdef _WIN32
  auto left=a.wstring(),right=b.wstring();std::transform(left.begin(),left.end(),left.begin(),[](wchar_t c){return std::towlower(c);});std::transform(right.begin(),right.end(),right.begin(),[](wchar_t c){return std::towlower(c);});return left==right;
#else
  return a==b;
#endif
}
bool contained(const std::filesystem::path& candidate,const std::filesystem::path& root) {
  auto child=candidate.begin();for(auto parent=root.begin();parent!=root.end();++parent,++child)if(child==candidate.end()||!sameComponent(*child,*parent))return false;return true;
}
std::filesystem::path canonical(const std::filesystem::path& path,const std::string& label) {
  std::error_code error;const auto result=std::filesystem::canonical(path,error);if(error)throw Error({label,1,1},"missing or inaccessible path: "+error.message());return result;
}
void cancelled(std::stop_token stop,const std::string& file) {if(stop.stop_requested())throw Error({file,1,1},"loading cancelled");}
std::string read(const std::filesystem::path& file,std::size_t budget,std::stop_token stop,std::string label={}) {
  if(label.empty())label=pathText(file);cancelled(stop,label);std::ifstream input(file,std::ios::binary);if(!input)throw Error({label,1,1},"cannot open file");
  std::string result;char block[8192];
  while(input){cancelled(stop,label);input.read(block,sizeof(block));const auto bytes=static_cast<std::size_t>(input.gcount());if(bytes>budget-result.size())throw Error({label,1,1},"file byte budget exceeded (maximum "+std::to_string(budget)+")");result.append(block,bytes);}
  if(input.bad())throw Error({label,1,1},"failed while reading file");cancelled(stop,label);return result;
}
std::filesystem::path resourcePath(const std::string& relative) {
  const Location where{relative,1,1};const auto decoded=decode(relative,where);const auto path=utf8Path(decoded,where);
  if(path.is_absolute()||path.has_root_path()||decoded.front()=='/')throw Error(where,"resource must be a relative path inside the world");
  for(const auto& part:path)if(part=="..")throw Error(where,"resource parent traversal is forbidden");
  return path;
}
std::string localWorld(const std::string& address) {
  const Location where{address,1,1};if(!address.starts_with("link://"))throw Error(where,"expected link://local/<world>");const auto slash=address.find('/',7);
  if(slash==std::string::npos||address.substr(7,slash-7)!="local")throw Error(where,"LINK authority must be local without credentials or port");
  const auto segment=decode(address.substr(slash+1),where);
  if(segment.empty()||!std::all_of(segment.begin(),segment.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';}))throw Error(where,"LINK world must be one segment containing letters, digits or hyphens");
  return segment;
}
struct RemoteSnapshot {
  RemoteLink remote;
  std::string address;
  std::filesystem::path parent,directory;
  std::vector<std::pair<std::filesystem::path,std::size_t>> files;
  std::size_t transferred=0;
  RemoteSnapshot(RemoteLink source,std::string origin):remote(std::move(source)),address(std::move(origin)) {
    std::error_code error;const auto temporary=std::filesystem::temp_directory_path(error);
    if(error)throw Error({address,1,1},"cannot locate temporary snapshot directory: "+error.message());
    parent=canonical(temporary,address);std::random_device random;
    for(unsigned attempt=0;attempt<32;++attempt) {
      const auto name="link-world-"+std::to_string(random())+"-"+std::to_string(random());
      const auto candidate=parent/name;
      if(std::filesystem::create_directory(candidate,error)){directory=candidate;return;}
      if(error&&error!=std::errc::file_exists)throw Error({address,1,1},"cannot create temporary world snapshot: "+error.message());
    }
    throw Error({address,1,1},"cannot allocate a unique temporary world snapshot");
  }
  ~RemoteSnapshot() {
    // Delete only this owned, direct child of the canonical temporary directory.
    std::error_code error;const auto actual=std::filesystem::canonical(directory,error);
    if(error||directory.empty()||directory.parent_path()!=parent||!sameComponent(actual,directory)||!contained(actual,parent)||sameComponent(actual,parent))return;
    std::filesystem::remove_all(directory,error);
  }
  std::string label(const std::string& relative) const {return address+"/"+relative;}
  std::filesystem::path stage(const std::string& relative,std::size_t cap,std::stop_token stop) {
    const auto where=Location{label(relative),1,1};cancelled(stop,where.file);
    std::filesystem::path path;
    try{path=resourcePath(relative).lexically_normal();}catch(const Error& error){throw Error(where,error.what());}
    // Windows aliases can otherwise overwrite an earlier resource in the snapshot.
    for(const auto& component:path) {
      auto name=pathText(component);if(name==".")continue;
      if(name.empty()||name.back()=='.'||name.back()==' '||name.find_first_of("<>\"|*?")!=std::string::npos)throw Error(where,"unsafe remote resource filename");
      auto stem=name.substr(0,name.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
      if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9'))throw Error(where,"reserved remote resource filename");
    }
    const auto file=(directory/path).lexically_normal();
    if(!contained(file,directory)||sameComponent(file,directory))throw Error(where,"resource escapes its world snapshot");
    for(const auto& [prior,bytes]:files)if(sameComponent(file,prior)) {
      if(bytes>cap)throw Error(where,"file byte budget exceeded (maximum "+std::to_string(cap)+")");
      return resolveResource(directory,relative);
    }
    if(transferred>=Limits::remoteWorldBytes)throw Error(where,"world transfer byte budget exceeded (maximum "+std::to_string(Limits::remoteWorldBytes)+")");
    const auto encoded=path.generic_u8string();const std::string decodedRelative(encoded.begin(),encoded.end());
    const auto bytes=fetchRemoteResource(remote,decodedRelative,std::min(cap,Limits::remoteWorldBytes-transferred),stop);
    transferred+=bytes.size();cancelled(stop,where.file);
    std::error_code error;std::filesystem::create_directories(file.parent_path(),error);
    if(error)throw Error(where,"cannot create snapshot resource directory: "+error.message());
    const auto actualParent=canonical(file.parent_path(),where.file);
    if(!contained(actualParent,directory))throw Error(where,"snapshot resource directory escapes its world");
    std::ofstream output(file,std::ios::binary|std::ios::trunc);
    if(!output)throw Error(where,"cannot create snapshot resource");
    for(std::size_t offset=0;offset<bytes.size();) {
      cancelled(stop,where.file);const auto count=std::min<std::size_t>(8192,bytes.size()-offset);
      output.write(bytes.data()+offset,static_cast<std::streamsize>(count));offset+=count;
      if(!output)throw Error(where,"failed while writing snapshot resource");
    }
    output.close();if(!output)throw Error(where,"failed while closing snapshot resource");
    cancelled(stop,where.file);files.emplace_back(file,bytes.size());return resolveResource(directory,relative);
  }
};
struct Field {Value value;Location location;};
using Fields=std::map<std::string,Field>;
void addField(Cursor& cursor,Fields& fields) {
  const auto name=cursor.expectKind(TokenKind::Identifier,"field name");cursor.expect(":");auto value=cursor.value();cursor.expect(";");
  if(!fields.emplace(name.text,Field{std::move(value),name.location}).second)throw Error(name.location,"duplicate field '"+name.text+"'");
}
Fields fieldBlock(Cursor& cursor) {Fields result;cursor.expect("{");while(!cursor.match("}")){if(cursor.done())throw Error(cursor.peek().location,"unterminated declaration");addField(cursor,result);}return result;}
void allowed(const Fields& fields,std::initializer_list<const char*> names) {
  for(const auto& [key,field]:fields)if(std::none_of(names.begin(),names.end(),[&](const char* name){return key==name;}))throw Error(field.location,"unsupported field '"+key+"'");
}
const Field& required(const Fields& fields,const std::string& name,Location where) {const auto found=fields.find(name);if(found==fields.end())throw Error(where,"missing required field '"+name+"'");return found->second;}
std::string text(const Field& field,const std::string& name) {if(!std::holds_alternative<std::string>(field.value.data)||field.value.string().empty())throw Error(field.location,name+" must be a nonempty string");return field.value.string();}
const Value::Map& dataMap(const Field& field,const std::string& name) {
  if(!std::holds_alternative<Value::Map>(field.value.data))throw Error(field.location,name+" must be a map");
  try {if(field.value.bytes()>Limits::stateBytes)throw Error(field.location,name+" byte budget exceeded");}catch(const Error& error){throw Error(field.location,error.what());}return field.value.map();
}
const Value::Array& dataArray(const Field& field,const std::string& name) {if(!std::holds_alternative<Value::Array>(field.value.data))throw Error(field.location,name+" must be an array");return field.value.array();}
double positive(const Value& value,Location where,const std::string& name) {if(!std::holds_alternative<double>(value.data)||!std::isfinite(std::get<double>(value.data))||value.number()<=0||value.number()>1000)throw Error(where,name+" must be a positive finite number at most 1000");return value.number();}
double nonnegative(const Value& value,Location where,const std::string& name) {if(!std::holds_alternative<double>(value.data)||!std::isfinite(std::get<double>(value.data))||value.number()<0||value.number()>1000)throw Error(where,name+" must be a finite number between 0 and 1000");return value.number();}
std::vector<std::string> strings(const Field& field,const std::string& name,std::size_t limit,bool identifiers=false) {
  const auto& items=dataArray(field,name);if(items.size()>limit)throw Error(field.location,name+" count budget exceeded");std::vector<std::string> result;std::set<std::string> unique;
  for(const auto& item:items){if(!std::holds_alternative<std::string>(item.data)||item.string().empty())throw Error(field.location,name+" entries must be nonempty strings");if(identifiers&&!identifier(item.string()))throw Error(field.location,"invalid identifier in "+name);if(!unique.insert(item.string()).second)throw Error(field.location,"duplicate entry in "+name);result.push_back(item.string());}return result;
}
void mapKeys(const Value::Map& map,std::initializer_list<const char*> names,Location where) {for(const auto& [key,_]:map)if(std::none_of(names.begin(),names.end(),[&](const char* name){return key==name;}))throw Error(where,"unsupported field '"+key+"'");}
const Value& mapRequired(const Value::Map& map,const std::string& key,Location where) {const auto found=map.find(key);if(found==map.end())throw Error(where,"missing field '"+key+"'");return found->second;}
Geometry geometry(const Field& field) {
  const auto& values=dataMap(field,"geometry");const auto& typeValue=mapRequired(values,"type",field.location);if(!std::holds_alternative<std::string>(typeValue.data))throw Error(field.location,"geometry type must be a string");const auto& type=typeValue.string();Geometry result;
  if(type=="sphere"){mapKeys(values,{"type","radius"},field.location);result.type=Geometry::Type::Sphere;result.radius=positive(mapRequired(values,"radius",field.location),field.location,"radius");}
  else if(type=="box"){mapKeys(values,{"type","size"},field.location);result.type=Geometry::Type::Box;result.size=valueVec3(mapRequired(values,"size",field.location),field.location);for(const auto number:result.size)if(number<=0||number>1000)throw Error(field.location,"box dimensions must be positive and at most 1000");}
  else if(type=="plane"){mapKeys(values,{"type","size"},field.location);result.type=Geometry::Type::Plane;const auto& size=mapRequired(values,"size",field.location);if(!std::holds_alternative<Value::Array>(size.data)||size.array().size()!=2)throw Error(field.location,"plane size requires two numbers");result.size={positive(size.array()[0],field.location,"plane width"),positive(size.array()[1],field.location,"plane height"),1};}
  else if(type=="cylinder"){
    mapKeys(values,{"type","radius","radiusTop","height"},field.location);result.type=Geometry::Type::Cylinder;
    result.radius=positive(mapRequired(values,"radius",field.location),field.location,"cylinder radius");
    result.radiusTop=nonnegative(mapRequired(values,"radiusTop",field.location),field.location,"cylinder radiusTop");
    result.height=positive(mapRequired(values,"height",field.location),field.location,"cylinder height");
  }
  else if(type=="frame"){
    mapKeys(values,{"type","size","border","depth"},field.location);result.type=Geometry::Type::Frame;
    const auto& size=mapRequired(values,"size",field.location);if(!std::holds_alternative<Value::Array>(size.data)||size.array().size()!=2)throw Error(field.location,"frame size requires two numbers");
    result.size={positive(size.array()[0],field.location,"frame width"),positive(size.array()[1],field.location,"frame height"),1};
    result.border=positive(mapRequired(values,"border",field.location),field.location,"frame border");
    result.depth=positive(mapRequired(values,"depth",field.location),field.location,"frame depth");
    if(result.border>=std::min(result.size[0],result.size[1])/2)throw Error(field.location,"frame border must be less than half the smaller outer dimension");
  }
  else throw Error(field.location,"unsupported geometry type '"+type+"'");return result;
}
void applyNodeFields(NodeDefinition& node,const Fields& fields) {
  if(node.kind==Kind::Element)allowed(fields,{"name","position","rotation","scale","visible","geometry","style","classes","image","text"});
  else allowed(fields,{"name","position","rotation","scale","visible","state","script","config"});
  node.name=node.id;
  for(const auto& [name,field]:fields) {
    if(name=="name")node.name=text(field,name);else if(name=="position")node.transform.position=valueVec3(field.value,field.location);else if(name=="rotation")node.transform.rotation=valueVec3(field.value,field.location);else if(name=="scale")node.transform.scale=valueVec3(field.value,field.location);
    else if(name=="visible"){if(!std::holds_alternative<bool>(field.value.data))throw Error(field.location,"visible must be bool");node.transform.visible=field.value.boolean();}
    else if(name=="state")node.state=dataMap(field,name);else if(name=="config")node.config=dataMap(field,name);else if(name=="script")node.script=text(field,name);
    else if(name=="geometry")node.geometry=geometry(field);else if(name=="classes")node.classes=strings(field,name,64,true);else if(name=="style"){node.style=text(field,name);if(!identifier(node.style))throw Error(field.location,"style must name a material identifier");}
    else if(name=="image")node.image=text(field,name);
    else if(name=="text"){
      if(!std::holds_alternative<std::string>(field.value.data))throw Error(field.location,"text must be a string");
      node.text=field.value.string();if(node.text.size()>Limits::textBytes)throw Error(field.location,"text byte budget exceeded (maximum "+std::to_string(Limits::textBytes)+")");
    }
  }
  if(node.kind==Kind::Element)required(fields,"geometry",node.location);
  if(fields.contains("image")&&fields.contains("text"))throw Error(fields.at("text").location,"image and text are mutually exclusive");
  if((fields.contains("image")||fields.contains("text"))&&node.geometry.type!=Geometry::Type::Plane)throw Error(node.location,"image and text require an Element plane");
  if(fields.contains("scale"))for(const auto number:node.transform.scale)if(number<=0||number>1000)throw Error(fields.at("scale").location,"scale must be positive and at most 1000");
  if(fields.contains("config")&&node.script.empty())throw Error(fields.at("config").location,"config requires a script binding");
}
void parseScene(World& world,std::string_view source,const std::string& file,std::stop_token stop) {
  Cursor cursor(lex(source,file));cursor.expect("scene");cursor.expect("{");std::set<std::string> ids;
  std::function<void(std::size_t,std::size_t)> node=[&](std::size_t parent,std::size_t depth) {
    cancelled(stop,file);const auto keyword=cursor.expectKind(TokenKind::Identifier,"object, component or element declaration");if(keyword.text!="object"&&keyword.text!="component"&&keyword.text!="element")throw Error(keyword.location,"expected object, component or element declaration");
    if(depth>Limits::nodeDepth)throw Error(keyword.location,"node depth budget exceeded");if(world.nodes.size()>=Limits::nodes)throw Error(keyword.location,"node count budget exceeded");
    NodeDefinition definition;definition.kind=keyword.text=="object"?Kind::Object:keyword.text=="component"?Kind::Component:Kind::Element;definition.parent=parent;definition.location=keyword.location;
    const auto id=cursor.expectKind(TokenKind::Identifier,"node identifier");definition.id=id.text;if(!ids.insert(id.text).second)throw Error(id.location,"duplicate node ID '"+id.text+"'");
    if(parent==noParent&&definition.kind!=Kind::Object)throw Error(keyword.location,"top-level nodes must be Objects");
    if(parent!=noParent){const auto parentKind=world.nodes[parent].kind;if(parentKind==Kind::Element)throw Error(keyword.location,"Element must be a leaf");if(parentKind==Kind::Object&&definition.kind!=Kind::Component)throw Error(keyword.location,"Object may contain only Components");if(parentKind==Kind::Component&&definition.kind==Kind::Object)throw Error(keyword.location,"Component may contain only Components or Elements");}
    const auto index=world.nodes.size();world.nodes.push_back(std::move(definition));cursor.expect("{");Fields fields;
    while(!cursor.match("}")){if(cursor.done())throw Error(cursor.peek().location,"unterminated node declaration");const auto& next=cursor.peek();if(next.kind==TokenKind::Identifier&&(next.text=="object"||next.text=="component"||next.text=="element")&&cursor.peek(1).kind==TokenKind::Identifier)node(index,depth+1);else addField(cursor,fields);}
    applyNodeFields(world.nodes[index],fields);
  };
  while(!cursor.match("}")){if(cursor.done())throw Error(cursor.peek().location,"unterminated scene");node(noParent,1);}if(!cursor.done())throw Error(cursor.peek().location,"unexpected text after scene");
}
void parseStyles(World& world,std::string_view source,const std::string& file,std::stop_token stop) {
  Cursor cursor(lex(source,file));bool defaults=false;std::size_t declarations=0;
  while(!cursor.done()) {
    cancelled(stop,file);const auto keyword=cursor.expectKind(TokenKind::Identifier,"Style declaration");std::string name;
    if(keyword.text!="defaults"&&keyword.text!="material"&&keyword.text!="class"&&keyword.text!="id")throw Error(keyword.location,"unsupported Style declaration '"+keyword.text+"'");
    if(++declarations>Limits::astNodes)throw Error(keyword.location,"Style AST budget exceeded");
    if(keyword.text!="defaults")name=cursor.expectKind(TokenKind::Identifier,"material or selector identifier").text;
    const auto fields=fieldBlock(cursor);StylePatch properties;for(const auto& [property,field]:fields){StylePatch one{{property,field.value}};validateStyle(one,field.location);properties.emplace(property,field.value);}
    if(keyword.text=="defaults"){if(defaults)throw Error(keyword.location,"duplicate defaults declaration");defaults=true;world.defaults=std::move(properties);}
    else if(keyword.text=="material"){if(!world.materials.emplace(name,std::move(properties)).second)throw Error(keyword.location,"duplicate material '"+name+"'");}
    else world.rules.push_back({keyword.text=="class"?StyleRule::Kind::Class:StyleRule::Kind::Id,name,std::move(properties)});
  }
}
}

std::filesystem::path resolveLink(const std::string& address,const std::filesystem::path& worldsRoot) {
  const Location where{address,1,1};const auto segment=localWorld(address);
  const auto root=canonical(worldsRoot,address);const auto result=canonical(root/utf8Path(segment,where),address);
  if(!contained(result,root)||sameComponent(result,root))throw Error(where,"LINK directory escapes the registered worlds root");
  if(!std::filesystem::is_directory(result))throw Error(where,"LINK world is not a directory");return result;
}
std::filesystem::path resolveResource(const std::filesystem::path& directory,const std::string& relative) {
  const Location where{relative,1,1};const auto path=resourcePath(relative);
  const auto root=canonical(directory,relative);const auto result=canonical(root/path,relative);if(!contained(result,root)||sameComponent(result,root))throw Error(where,"resource escapes its world directory after symlink resolution");
  if(!std::filesystem::is_regular_file(result))throw Error(where,"resource is not a regular file");return result;
}
std::string resolvePortalAddress(const std::string& address,const std::string& currentAddress,const std::filesystem::path& worldsRoot) {
  std::string result=address;
  if(result.starts_with('/')) {
    std::string authority;
    if(currentAddress.starts_with("link://local/")){localWorld(currentAddress);authority="local";}
    else authority=parseRemoteLink(currentAddress).authority();
    result="link://"+authority+result;
  }
  if(result.starts_with("link://local/"))resolveLink(result,worldsRoot);
  else parseRemoteLink(result);
  return result;
}
std::shared_ptr<const World> loadWorld(const std::string& address,const std::filesystem::path& worldsRoot,std::stop_token stop) {
  cancelled(stop,address);auto result=std::make_shared<World>();result->address=address;
  std::shared_ptr<RemoteSnapshot> snapshot;
  if(address.starts_with("link://local/"))result->directory=resolveLink(address,worldsRoot);
  else{snapshot=std::make_shared<RemoteSnapshot>(parseRemoteLink(address),address);result->directory=snapshot->directory;result->resourceOwner=snapshot;}
  const auto resource=[&](const std::string& relative,std::size_t cap){return snapshot?snapshot->stage(relative,cap,stop):resolveResource(result->directory,relative);};
  const auto label=[&](const std::string& relative,const std::filesystem::path& file){return snapshot?snapshot->label(relative):pathText(file);};
  const auto manifest=resource("world.link",Limits::manifestBytes);const auto manifestSource=read(manifest,Limits::manifestBytes,stop,label("world.link",manifest));Cursor cursor(lex(manifestSource,label("world.link",manifest)));const auto keyword=cursor.expect("world");const auto fields=fieldBlock(cursor);if(!cursor.done())throw Error(cursor.peek().location,"unexpected text after world declaration");
  allowed(fields,{"version","id","name","elements","style","scripts","entry","views","environment","walk","portals"});const auto version=text(required(fields,"version",keyword.location),"version");if(version!="0.1")throw Error(fields.at("version").location,"unsupported version '"+version+"'");
  result->id=text(required(fields,"id",keyword.location),"id");result->name=text(required(fields,"name",keyword.location),"name");
  const auto elements=text(required(fields,"elements",keyword.location),"elements");const auto style=text(required(fields,"style",keyword.location),"style");const auto scripts=strings(required(fields,"scripts",keyword.location),"scripts",Limits::scripts);
  const auto& entryField=required(fields,"entry",keyword.location);const auto& entry=dataMap(entryField,"entry");mapKeys(entry,{"position","target"},entryField.location);result->eye=valueVec3(mapRequired(entry,"position",entryField.location),entryField.location);result->target=valueVec3(mapRequired(entry,"target",entryField.location),entryField.location);
  if(const auto walk=fields.find("walk");walk!=fields.end()){
    const auto& field=walk->second;const auto& settings=dataMap(field,"walk");mapKeys(settings,{"min","max","eyeHeight","speed"},field.location);WalkSettings value;
    value.min=valueVec3(mapRequired(settings,"min",field.location),field.location);value.max=valueVec3(mapRequired(settings,"max",field.location),field.location);
    for(std::size_t axis=0;axis<3;++axis)if(value.min[axis]>=value.max[axis])throw Error(field.location,"walk min must be less than max on every axis");
    const auto bounded=[&](const std::string& name,double minimum,double maximum,double fallback){
      const auto found=settings.find(name);if(found==settings.end())return fallback;
      if(!std::holds_alternative<double>(found->second.data)||!std::isfinite(found->second.number())||found->second.number()<minimum||found->second.number()>maximum)throw Error(field.location,"walk "+name+" must be a finite number between "+std::to_string(minimum)+" and "+std::to_string(maximum));
      return found->second.number();
    };
    value.eyeHeight=bounded("eyeHeight",.25,10.,value.eyeHeight);value.speed=bounded("speed",.1,20.,value.speed);
    if(value.eyeHeight<value.min[1]||value.eyeHeight>value.max[1])throw Error(field.location,"walk eyeHeight must be within vertical bounds");
    result->walk=value;
  }
  if(const auto portals=fields.find("portals");portals!=fields.end()){
    const auto& field=portals->second;const auto& items=dataArray(field,"portals");if(items.size()>Limits::portals)throw Error(field.location,"portal count budget exceeded (maximum "+std::to_string(Limits::portals)+")");
    std::set<std::string> ids;
    for(const auto& item:items){
      cancelled(stop,address);const Field portalField{item,field.location};const auto& portal=dataMap(portalField,"portal");mapKeys(portal,{"id","address","position","size"},field.location);Portal value;
      value.id=text(Field{mapRequired(portal,"id",field.location),field.location},"portal id");if(value.id.size()>128||!identifier(value.id))throw Error(field.location,"portal id must be an identifier of at most 128 bytes");
      if(!ids.insert(value.id).second)throw Error(field.location,"duplicate portal id '"+value.id+"'");
      value.address=text(Field{mapRequired(portal,"address",field.location),field.location},"portal address");
      try{value.address=resolvePortalAddress(value.address,address,worldsRoot);}catch(const Error& error){throw Error(field.location,"invalid portal address: "+std::string(error.what()));}
      value.position=valueVec3(mapRequired(portal,"position",field.location),field.location);value.size=valueVec3(mapRequired(portal,"size",field.location),field.location);
      for(const auto dimension:value.size)if(dimension<=0||dimension>1000)throw Error(field.location,"portal size must be positive and at most 1000");
      result->portals.push_back(std::move(value));
    }
  }
  if(const auto environment=fields.find("environment");environment!=fields.end()){
    const auto& field=environment->second;const auto& settings=dataMap(field,"environment");mapKeys(settings,{"background","grid","fog"},field.location);
    if(const auto background=settings.find("background");background!=settings.end()){validateStyle({{"color",background->second}},field.location);result->background=background->second.string();}
    if(const auto grid=settings.find("grid");grid!=settings.end()){if(!std::holds_alternative<bool>(grid->second.data))throw Error(field.location,"environment grid must be bool");result->grid=grid->second.boolean();}
    if(const auto fog=settings.find("fog");fog!=settings.end()){
      const Field fogField{fog->second,field.location};const auto& options=dataMap(fogField,"environment fog");mapKeys(options,{"color","start","end"},field.location);FogSettings value;value.color=result->background;
      if(const auto color=options.find("color");color!=options.end()){validateStyle({{"color",color->second}},field.location);value.color=color->second.string();}
      const auto distance=[&](const std::string& name){const auto& item=mapRequired(options,name,field.location);if(!std::holds_alternative<double>(item.data))throw Error(field.location,"fog "+name+" must be a finite number");const auto number=item.number();if(number<0||number>500)throw Error(field.location,"fog distances must be between 0 and 500");return number;};
      value.start=distance("start");value.end=distance("end");if(value.start>=value.end)throw Error(field.location,"fog start must be less than end");result->fog=std::move(value);
    }
  }
  if(const auto views=fields.find("views");views!=fields.end()){
    const auto& items=dataArray(views->second,"views");if(items.size()>Limits::views)throw Error(views->second.location,"view count budget exceeded (maximum "+std::to_string(Limits::views)+")");
    for(const auto& item:items){
      cancelled(stop,address);const Field field{item,views->second.location};const auto& view=dataMap(field,"view");mapKeys(view,{"name","position","target"},field.location);
      const auto& name=mapRequired(view,"name",field.location);const Field nameField{name,field.location};
      result->views.push_back({text(nameField,"view name"),valueVec3(mapRequired(view,"position",field.location),field.location),valueVec3(mapRequired(view,"target",field.location),field.location)});
    }
  }
  const auto elementsFile=resource(elements,Limits::documentBytes);const auto styleFile=resource(style,Limits::documentBytes);std::vector<std::filesystem::path> scriptFiles;
  for(const auto& script:scripts){cancelled(stop,address);const auto file=resource(script,Limits::scriptBytes);for(const auto& prior:scriptFiles)if(sameComponent(file,prior))throw Error(fields.at("scripts").location,"duplicate resolved Script resource");scriptFiles.push_back(file);}
  const auto elementSource=read(elementsFile,Limits::documentBytes,stop,label(elements,elementsFile));parseScene(*result,elementSource,label(elements,elementsFile),stop);
  const auto styleSource=read(styleFile,Limits::documentBytes,stop,label(style,styleFile));parseStyles(*result,styleSource,label(style,styleFile),stop);
  const std::set<std::string> declared(scripts.begin(),scripts.end());
  for(const auto& node:result->nodes){
    cancelled(stop,address);if(!node.script.empty()&&!declared.contains(node.script))throw Error(node.location,"Script binding '"+node.script+"' is not listed in world scripts");if(!node.style.empty()&&!result->materials.contains(node.style))throw Error(node.location,"unknown material '"+node.style+"'");
    if(!node.image.empty()&&!result->images.contains(node.image)){
      if(result->images.size()>=Limits::images)throw Error(node.location,"image count budget exceeded (maximum "+std::to_string(Limits::images)+")");
      const auto file=resource(node.image,Limits::imageBytes);auto extension=pathText(file.extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
      if(extension!=".jpg"&&extension!=".jpeg"&&extension!=".png"&&extension!=".bmp")throw Error(node.location,"unsupported image type; expected jpg, jpeg, png or bmp");
      std::error_code error;const auto bytes=std::filesystem::file_size(file,error);if(error)throw Error(node.location,"cannot inspect image '"+node.image+"': "+error.message());
      if(bytes>Limits::imageBytes)throw Error(node.location,"image file byte budget exceeded (maximum "+std::to_string(Limits::imageBytes)+")");
      result->images.emplace(node.image,file);
    }
  }
  for(std::size_t i=0;i<scripts.size();++i){const auto source=read(scriptFiles[i],Limits::scriptBytes,stop,label(scripts[i],scriptFiles[i]));cancelled(stop,address);result->programs.emplace(scripts[i],parseScript(source,label(scripts[i],scriptFiles[i])));cancelled(stop,address);}
  cancelled(stop,address);return result;
}
} // namespace linkworld
