#include "link/loader.hpp"
#include "link/text.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <iostream>
#include <stop_token>

namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error("parser test failed: "+message);}
void rejects(const std::function<void()>& operation,const std::string& message) {
  try{operation();}catch(const linkworld::Error& error){check(std::string(error.what()).find(message)!=std::string::npos,"expected error containing '"+message+"', got: "+error.what());check(error.location.line>=1&&error.location.column>=1,"positive error location");return;}
  throw std::runtime_error("parser test failed: expected rejection containing '"+message+"'");
}
void write(const std::filesystem::path& file,const std::string& source){std::filesystem::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output<<source;if(!output)throw std::runtime_error("could not write parser fixture");}
const std::string validManifest=R"(world {
  version:"0.1"; id:"room-a"; name:"Parser test";
  elements:"scene.element"; style:"appearance.style"; scripts:[];
  entry:{position:[7,6,9],target:[0,1,0]};
})";
const std::string validScene=R"(scene {
  object door { state:{isOpen:false,nested:{values:[null,true,"safe",1e-2]}};
    component hinge { rotation:[0,90,0];
      element panel { geometry:{type:"box",size:[1,2,.15]};style:"oak";classes:["accent","other"]; }
    }
  }
})";
const std::string validStyles=R"(defaults {color:"#111111";metalness:.2;}
material oak {color:"#222222";roughness:.4;}
id panel {color:"#555555";opacity:.5;}
class accent {color:"#333333";roughness:.3;}
class other {color:"#444444";roughness:.2;}
id panel {opacity:.25;}
)";
std::filesystem::path utf8Path(const std::string& text){std::u8string encoded;for(unsigned char byte:text)encoded.push_back(static_cast<char8_t>(byte));return std::filesystem::path(encoded);}
struct Fixture {
  std::filesystem::path directory,worlds,room;
  Fixture(){
    const auto native=std::filesystem::absolute(utf8Path(LINK_SOURCE_DIR)).lexically_normal();
    directory=native/(".parser-test-data-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(directory.parent_path()==native,"fixture must stay under native source directory");
    worlds=directory/"worlds";room=worlds/"room-a";reset();
  }
  ~Fixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
  void reset(){write(room/"world.link",validManifest);write(room/"scene.element",validScene);write(room/"appearance.style",validStyles);}
  std::shared_ptr<const linkworld::World> load(std::stop_token stop={}){return linkworld::loadWorld("link://local/room-a",worlds,stop);}
};
}

void testParser() {
  using namespace linkworld;
  {
    const auto tokens=lex("// comment\nfoo -.15 +2 1e-3 \"line\\nquote\\\"\\\\\" == != <= >= && ||", "tokens.element");
    check(tokens[0].text=="foo"&&tokens[0].location.line==2&&tokens[0].location.column==1,"lexer tracks comments/lines");
    Cursor cursor(lex("{empty:[], nested:{value:-.15}, flags:[true,false,null], text:\"a\\tb\"}","data.element"));
    const auto value=cursor.value();check(cursor.done(),"data parser consumes source");check(std::abs(value.map().at("nested").map().at("value").number()+.15)<1e-12,"signed fractional number");
    check(value.map().at("flags").array().size()==3,"JSON-like array values");check(value.map().at("text").string()=="a\tb","string escape decoding");
    check(value.bytes()>0&&value.display().find("nested")!=std::string::npos,"value inspection");
    rejects([]{lex("\n\"unterminated","bad.element");},"unterminated string");
    rejects([]{lex("\"bad\\u0041\"","bad.element");},"unsupported string escape");
    rejects([]{lex("\"line\nbreak\"","bad.element");},"control character");
    rejects([]{lex("1e999","bad.element");},"nonfinite");
    rejects([]{lex("1e+","bad.element");},"exponent");
    rejects([]{lex("12name","bad.element");},"numeric token");
    rejects([]{lex("`host code`","bad.element");},"unsupported character");
    rejects([]{lex(std::string("\"")+std::bit_cast<char>(std::uint8_t{0xff})+"\"","bad.element");},"UTF-8");
    {Cursor c(lex("\"[\"","string.element"));check(c.value().string()=="[","string contents do not become punctuation");}
    rejects([]{Cursor c(lex("\"world\" {}","bad.link"));c.expect("world");},"expected 'world'");
    rejects([]{Cursor c(lex("{key:1,key:2}","bad.element"));c.value();},"duplicate map key");
    rejects([]{Cursor c(lex("[1,]","bad.element"));c.value();},"data value");
    rejects([]{Cursor c(lex("unknown","bad.element"));c.value();},"data value");
    rejects([]{std::string value(Limits::parseDepth+2,'[');value+='0';value+=std::string(Limits::parseDepth+2,']');Cursor c(lex(value,"deep.element"));c.value();},"depth budget");
    rejects([]{lex("\""+std::string(Limits::stringBytes+1,'a')+"\"","big.element");},"string byte budget");
    rejects([]{std::string source;for(std::size_t i=0;i<=Limits::tokens;++i)source+="a ";lex(source,"tokens.element");},"token budget");
    rejects([]{Value(std::numeric_limits<double>::infinity()).bytes();},"nonfinite");
    rejects([]{valueVec3(Value::Array{1,2});},"three finite");
  }
  Fixture fixture;
  {
    const auto world=fixture.load();check(world->nodes.size()==3,"flat Object/Component/Element model");check(world->nodes[0].parent==noParent&&world->nodes[1].parent==0&&world->nodes[2].parent==1,"parent indices");
    check(world->nodes[1].transform.rotation[1]==90,"rotation remains degrees at model boundary");
    const auto material=resolveStyle(world->nodes[2],*world);check(material.color=="#555555"&&material.opacity==.25&&material.roughness==.2&&material.metalness==.2,"Style cascade specificity and source order");
    check(resolveStyle(world->nodes[2],*world,{{"color","#ffffff"}}).color=="#ffffff","runtime override wins");check(resolveStyle(world->nodes[2],*world).color==material.color,"override does not mutate source");
    auto reversed=world->nodes[2];std::reverse(reversed.classes.begin(),reversed.classes.end());check(resolveStyle(reversed,*world).roughness==.2,"class order comes from Style source");
  }
  for(const auto& bad:std::vector<std::pair<std::string,std::string>>{
    {"http://local/room-a","link://"},{"link://remote/room-a","authority"},{"link://user@local/room-a","authority"},{"link://local:123/room-a","authority"},
    {"link://local/room-a?x=1","forbidden"},{"link://local/room-a#fragment","forbidden"},{"link://local/../room-a","one segment"},{"link://local/%2e%2e","one segment"},
    {"link://local/room-a/extra","one segment"},{"link://local/%2froom-a","encoded path separator"},{"link://local/%5croom-a","encoded path separator"},
    {"link://local/%252e%252e","forbidden"},{"link://local/%GG","malformed percent"},{"link://local/room-a%","malformed percent"},{"link://local/room-a%00","forbidden"},
    {"link://local//room-a","one segment"},{"link://local/C:world","forbidden"},{"link://local/","empty"}}) rejects([&]{resolveLink(bad.first,fixture.worlds);},bad.second);
  check(resolveLink("link://local/room%2da",fixture.worlds)==std::filesystem::canonical(fixture.room),"single canonical percent decode");
  for(const auto& bad:std::vector<std::pair<std::string,std::string>>{{"../outside.element","parent traversal"},{"./../outside.element","parent traversal"},{"%2e%2e/outside.element","parent traversal"},{"/absolute.element","relative path"},{"C:/absolute.element","forbidden"},{"scene.element?x=1","forbidden"},{"scene.element#x","forbidden"},{"%2fscene.element","encoded path separator"},{"%255cscene.element","forbidden"},{"scene%GG.element","malformed percent"},{"missing.element","missing or inaccessible"}})rejects([&]{resolveResource(fixture.room,bad.first);},bad.second);
  check(resolveResource(fixture.room,"./scene.element")==std::filesystem::canonical(fixture.room/"scene.element"),"dot relative resources resolve");
  {
    const auto outside=fixture.directory/"outside";write(outside/"secret.element","secret");std::error_code error;
    std::filesystem::create_directory_symlink(outside,fixture.worlds/"escape",error);
    if(!error)rejects([&]{resolveLink("link://local/escape",fixture.worlds);},"escapes");
    else std::cout<<"SKIP directory symlink confinement test: "<<error.message()<<'\n';
    error.clear();std::filesystem::create_symlink(outside/"secret.element",fixture.room/"escape.element",error);
    if(!error)rejects([&]{resolveResource(fixture.room,"escape.element");},"escapes");
    else std::cout<<"SKIP file symlink confinement test: "<<error.message()<<'\n';
  }
  const auto manifestCase=[&](const std::string& source,const std::string& error){fixture.reset();write(fixture.room/"world.link",source);rejects([&]{fixture.load();},error);};
  manifestCase("world {version:\"9\";}","unsupported version");
  manifestCase("world {version:\"0.1\"; version:\"0.1\";}","duplicate field");
  manifestCase("world {version:\"0.1\"; extra:true;}","unsupported field");
  manifestCase("world {version:\"0.1\";}","missing required field");
  manifestCase("world {version:true;}","nonempty string");
  manifestCase(validManifest+" unexpected","unexpected text");
  auto source=validManifest;source.replace(source.find("[7,6,9]"),7,"[0,0]");manifestCase(source,"three finite");
  const auto sceneCase=[&](const std::string& value,const std::string& error){fixture.reset();write(fixture.room/"scene.element",value);rejects([&]{fixture.load();},error);};
  sceneCase("scene {object same {} object same {}}","duplicate node ID");
  sceneCase("scene {component root {}}","top-level");
  sceneCase("scene {object root {element mesh {geometry:{type:\"sphere\",radius:1};}}}","only Components");
  sceneCase("scene {object root {component part {object nested {}}}}","only Components or Elements");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"sphere\",radius:1};component nested {}}}}}","leaf");
  sceneCase("scene {object root {geometry:{type:\"sphere\",radius:1};}}","unsupported field");
  sceneCase("scene {object root {component part {element mesh {state:{};geometry:{type:\"sphere\",radius:1};}}}}","unsupported field");
  sceneCase("scene {object root {state:[];}}","must be a map");
  sceneCase("scene {object root {visible:1;}}","visible must be bool");
  sceneCase("scene {object root {position:[0,0];}}","three finite");
  sceneCase("scene {object root {position:[0,1000001,0];}}","magnitude");
  sceneCase("scene {object root {scale:[1,0,1];}}","scale must be positive");
  sceneCase("scene {object root {scale:[1,1001,1];}}","scale must be positive");
  sceneCase("scene {object root {config:{};}}","requires a script");
  sceneCase("scene {object root {script:\"scripts/missing.script\";}}","not listed");
  sceneCase("scene {object root {component part {element mesh {}}}}","missing required field");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"sphere\",radius:0};}}}}","positive finite");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"sphere\",radius:1001};}}}}","at most 1000");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"box\",size:[1,0,1]};}}}}","positive");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"sphere\",radius:1,extra:0};}}}}","unsupported field");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"cone\"};}}}}","unsupported geometry");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"plane\",size:[1,2,3]};}}}}","requires two numbers");
  sceneCase("scene {object root {component part {element mesh {geometry:{type:\"sphere\",radius:1};style:\"missing\";}}}}","unknown material");
  {
    fixture.reset();write(fixture.room/"scene.element","scene {object root {component part {element mesh {geometry:{type:\"plane\",size:[2,3]};}}}}");
    const auto plane=fixture.load()->nodes.back().geometry;check(plane.type==Geometry::Type::Plane&&plane.size[0]==2&&plane.size[1]==3,"plane's two dimensions remain renderer independent");
  }
  {
    std::string deep="scene {object root {";for(std::size_t i=1;i<=Limits::nodeDepth;++i)deep+="component node"+std::to_string(i)+" {";deep+=std::string(Limits::nodeDepth+2,'}');sceneCase(deep,"depth budget");
    std::string many="scene {";for(std::size_t i=0;i<=Limits::nodes;++i)many+="object node"+std::to_string(i)+" {}";many+='}';sceneCase(many,"count budget");
    sceneCase(std::string(Limits::documentBytes+1,' '),"file byte budget");
    std::string largeState="scene {object root {state:{values:[";for(int i=0;i<9;++i){if(i)largeState+=',';largeState+='"'+std::string(8000,'a')+'"';}largeState+="]};}}";sceneCase(largeState,"byte budget");
  }
  const auto styleCase=[&](const std::string& value,const std::string& error){fixture.reset();write(fixture.room/"appearance.style",value);rejects([&]{fixture.load();},error);};
  styleCase("defaults {color:\"red\";}","RRGGBB");styleCase("defaults {opacity:2;}","between 0 and 1");styleCase("defaults {wireframe:1;}","must be bool");styleCase("defaults {emissive:true;}","unsupported Style property");
  styleCase("defaults {emission:true;}","must be a number");styleCase("defaults {emission:-.1;}","between 0 and 4");styleCase("defaults {emission:4.1;}","between 0 and 4");
  styleCase("defaults {textBackground:1;}","textBackground must be bool");
  styleCase("material oak {textBackground:\"false\";}","textBackground must be bool");
  styleCase("class accent {textBackground:null;}","textBackground must be bool");
  styleCase("id panel {textBackground:[];}","textBackground must be bool");
  {
    fixture.reset();const auto legacy=fixture.load();check(resolveStyle(legacy->nodes[2],*legacy).emission==0,"legacy materials do not emit light by default");
    write(fixture.room/"appearance.style","defaults {emission:.4;} material oak {emission:.9;} class accent {emission:1.4;} id panel {emission:2.2;}");const auto world=fixture.load();
    check(resolveStyle(world->nodes[2],*world).emission==2.2&&resolveStyle(world->nodes[2],*world,{{"emission",4.}}).emission==4,"emission follows the material cascade and bounded runtime overrides");
  }
  {
    fixture.reset();const auto legacy=fixture.load();check(resolveStyle(legacy->nodes[2],*legacy).textBackground,"Legacy materials keep opaque text backgrounds by default");
    write(fixture.room/"appearance.style","defaults {textBackground:false;} material oak {textBackground:true;} class accent {textBackground:false;} id panel {textBackground:true;}");
    const auto world=fixture.load();const auto& panel=world->nodes[2];
    check(resolveStyle(panel,*world).textBackground&&!resolveStyle(panel,*world,{{"textBackground",false}}).textBackground,"Text background follows id priority and validated runtime material overrides");
    auto materialOnly=panel;materialOnly.id="unmatched";materialOnly.classes.clear();
    check(resolveStyle(materialOnly,*world).textBackground,"Named materials override the default text background");
    auto classOnly=materialOnly;classOnly.classes={"accent"};
    check(!resolveStyle(classOnly,*world).textBackground,"Class rules override named material text backgrounds");
    auto defaultsOnly=materialOnly;defaultsOnly.style.clear();
    check(!resolveStyle(defaultsOnly,*world).textBackground,"Text background defaults apply without a named material");
    rejects([&]{resolveStyle(panel,*world,{{"textBackground",1.}});},"textBackground must be bool");
  }
  styleCase("defaults {} defaults {}","duplicate defaults");styleCase("material oak {} material oak {}","duplicate material");styleCase("class accent {opacity:1;opacity:.5;}","duplicate field");styleCase("selector accent {}","unsupported Style declaration");styleCase("class accent.other {}","expected '{'");
  const auto elementScene=[](const std::string& geometry,const std::string& extra=""){
    return "scene {object root {component part {element mesh {geometry:"+geometry+";"+extra+"}}}}";
  };
  {
    fixture.reset();write(fixture.room/"scene.element",elementScene("{type:\"cylinder\",radius:2,radiusTop:0,height:4}"));
    const auto cylinder=fixture.load()->nodes.back().geometry;
    check(cylinder.type==Geometry::Type::Cylinder&&cylinder.radius==2&&cylinder.radiusTop==0&&cylinder.height==4,"cylinder carries lower/upper radius and height, including a cone tip");
    fixture.reset();write(fixture.room/"scene.element",elementScene("{type:\"frame\",size:[3,2],border:.06,depth:.08}"));
    const auto frame=fixture.load()->nodes.back().geometry;
    check(frame.type==Geometry::Type::Frame&&frame.size==Vec3{3,2,1}&&frame.border==.06&&frame.depth==.08,"frame carries outer size, border and depth");
  }
  for(const auto& [shape,error]:std::vector<std::pair<std::string,std::string>>{
    {"{type:\"cylinder\",radius:0,radiusTop:0,height:1}","positive finite"},
    {"{type:\"cylinder\",radius:1,radiusTop:-1,height:1}","between 0 and 1000"},
    {"{type:\"cylinder\",radius:1,radiusTop:1001,height:1}","between 0 and 1000"},
    {"{type:\"cylinder\",radius:1,radiusTop:1,height:0}","positive finite"},
    {"{type:\"cylinder\",radius:1,radiusTop:true,height:1}","between 0 and 1000"},
    {"{type:\"cylinder\",radius:1,height:1}","missing field 'radiusTop'"},
    {"{type:\"cylinder\",radius:1,radiusTop:1,height:1,segments:10}","unsupported field"},
    {"{type:\"frame\",size:[3,2,1],border:.06,depth:.08}","requires two numbers"},
    {"{type:\"frame\",size:[3,2],border:1,depth:.08}","less than half"},
    {"{type:\"frame\",size:[3,2],border:0,depth:.08}","positive finite"},
    {"{type:\"frame\",size:[3,2],border:.06,depth:0}","positive finite"},
    {"{type:\"frame\",size:[3,2],border:.06}","missing field 'depth'"},
    {"{type:\"frame\",size:[3,2],border:.06,depth:.08,radius:1}","unsupported field"},
  })sceneCase(elementScene(shape),error);
  {
    fixture.reset();write(fixture.room/"scene.element",elementScene("{type:\"plane\",size:[3,2]}","text:\"고양이 사진 · 제목\";"));
    check(fixture.load()->nodes.back().text=="고양이 사진 · 제목","UTF-8 plaque text survives parsing");
    write(fixture.room/"scene.element",elementScene("{type:\"plane\",size:[3,2]}","text:\""+std::string(Limits::textBytes,'a')+"\";"));
    check(fixture.load()->nodes.back().text.size()==Limits::textBytes,"text byte limit is inclusive");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","text:\""+std::string(Limits::textBytes+1,'a')+"\";"),"text byte budget");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","text:true;"),"text must be a string");
    sceneCase(elementScene("{type:\"box\",size:[3,2,1]}","text:\"label\";"),"require an Element plane");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","text:\"\";image:\"assets/photo.jpg\";"),"mutually exclusive");
    sceneCase("scene {object root {text:\"label\";}}","unsupported field");
  }
  {
    fixture.reset();write(fixture.room/"assets/photo.JPG","encoded image decoded by the renderer, not by the core loader");
    write(fixture.room/"scene.element",elementScene("{type:\"plane\",size:[3,2]}","image:\"assets/photo.JPG\";"));
    const auto world=fixture.load();check(world->nodes.back().image=="assets/photo.JPG","image binding keeps original reference");
    check(world->images.at("assets/photo.JPG")==std::filesystem::canonical(fixture.room/"assets/photo.JPG"),"loader prepares canonical image path without decoding");
    sceneCase(elementScene("{type:\"box\",size:[3,2,1]}","image:\"assets/photo.JPG\";"),"require an Element plane");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","image:true;"),"nonempty string");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","image:\"missing.png\";"),"missing or inaccessible");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","image:\"../outside.png\";"),"parent traversal");
    write(fixture.room/"assets/vector.svg","not a supported bitmap");
    sceneCase(elementScene("{type:\"plane\",size:[3,2]}","image:\"assets/vector.svg\";"),"unsupported image type");
    for(const auto& extension:{"jpg","jpeg","png","bmp"}){
      fixture.reset();const auto relative="assets/valid."+std::string(extension);write(fixture.room/relative,"placeholder: core preflight does not decode");write(fixture.room/"scene.element",elementScene("{type:\"plane\",size:[1,1]}","image:\""+relative+"\";"));check(fixture.load()->images.size()==1,"permitted bitmap extension");
    }
    fixture.reset();const auto large=fixture.room/"assets/large.png";write(large,"x");std::filesystem::resize_file(large,Limits::imageBytes);
    write(fixture.room/"scene.element",elementScene("{type:\"plane\",size:[1,1]}","image:\"assets/large.png\";"));check(fixture.load()->images.size()==1,"image file byte limit is inclusive");
    std::filesystem::resize_file(large,Limits::imageBytes+1);rejects([&]{fixture.load();},"image file byte budget");
  }
  {
    fixture.reset();std::string scene="scene {object root {component wall {";
    for(std::size_t i=0;i<Limits::images;++i){const auto relative="assets/image"+std::to_string(i)+".png";write(fixture.room/relative,"image placeholder");scene+="element image"+std::to_string(i)+" {geometry:{type:\"plane\",size:[1,1]};image:\""+relative+"\";}";}
    write(fixture.room/"scene.element",scene+"}}}");check(fixture.load()->images.size()==Limits::images,"128 distinct images fit within the unchanged 500-node budget");
    write(fixture.room/"scene.element",scene+"element reused {geometry:{type:\"plane\",size:[1,1]};image:\"assets/image0.png\";}}}}");check(fixture.load()->images.size()==Limits::images,"multiple planes share the same image reference without consuming another image slot");
    write(fixture.room/"assets/extra.png","image placeholder");write(fixture.room/"scene.element",scene+"element extra {geometry:{type:\"plane\",size:[1,1]};image:\"assets/extra.png\";}}}}");rejects([&]{fixture.load();},"image count budget");
  }
  const auto withViews=[](const std::string& views){auto manifest=validManifest;manifest.insert(manifest.rfind('}'),"views:"+views+";");return manifest;};
  {
    fixture.reset();write(fixture.room/"world.link",withViews("[{name:\"사진 01\",position:[1,2,3],target:[0,1,0]}]"));
    const auto world=fixture.load();check(world->views.size()==1&&world->views[0].name=="사진 01"&&world->views[0].position==Vec3{1,2,3}&&world->views[0].target==Vec3{0,1,0},"named camera views carry independent finite positions and targets");
    check(world->eye==Vec3{7,6,9},"named viewpoints preserve the entry camera");
    std::string views="[";for(std::size_t i=0;i<Limits::views;++i){if(i)views+=',';views+="{name:\"view"+std::to_string(i)+"\",position:[0,1,2],target:[0,0,0]}";}
    write(fixture.room/"world.link",withViews(views+"]"));check(fixture.load()->views.size()==Limits::views,"128 camera views accepted");
    manifestCase(withViews(views+",{name:\"extra\",position:[0,1,2],target:[0,0,0]}]"),"view count budget");
  }
  for(const auto& [views,error]:std::vector<std::pair<std::string,std::string>>{
    {"true","must be an array"},{"[true]","must be a map"},
    {"[{name:\"view\",position:[1,2,3]}]","missing field 'target'"},
    {"[{name:1,position:[1,2,3],target:[0,0,0]}]","nonempty string"},
    {"[{name:\"view\",position:[1,2],target:[0,0,0]}]","three finite"},
    {"[{name:\"view\",position:[1000001,2,3],target:[0,0,0]}]","magnitude"},
    {"[{name:\"view\",position:[1,2,3],target:[0,0,0],fov:60}]","unsupported field"},
    {"[{name:\"view\",name:\"again\",position:[1,2,3],target:[0,0,0]}]","duplicate map key"},
  })manifestCase(withViews(views),error);
  const auto withEnvironment=[](const std::string& settings){auto manifest=validManifest;manifest.insert(manifest.rfind('}'),"environment:"+settings+";");return manifest;};
  {
    fixture.reset();const auto defaults=fixture.load();check(defaults->background=="#111c21"&&defaults->grid&&!defaults->fog,"absent environment preserves the standard clear color, debug grid, and no fog");
    write(fixture.room/"world.link",withEnvironment("{background:\"#A8C9DE\",grid:false}"));const auto configured=fixture.load();check(configured->background=="#A8C9DE"&&!configured->grid,"world environment configures backdrop and grid without world-specific behavior");
    write(fixture.room/"world.link",withEnvironment("{grid:false}"));const auto partial=fixture.load();check(partial->background==defaults->background&&!partial->grid,"omitted environment fields retain defaults");
    write(fixture.room/"world.link",withEnvironment("{}"));const auto empty=fixture.load();check(empty->background==defaults->background&&empty->grid,"empty environment map retains defaults");
    write(fixture.room/"world.link",withEnvironment("{background:\"#06151D\",fog:{start:80,end:250}}"));const auto fog=fixture.load();check(fog->fog&&fog->fog->start==80&&fog->fog->end==250&&fog->fog->color=="#06151D","distance fog uses explicit bounds and defaults to the environment background color");
    write(fixture.room/"world.link",withEnvironment("{fog:{color:\"#203040\",start:0,end:500}}"));check(fixture.load()->fog->color=="#203040","fog accepts its own valid color and inclusive distance boundaries");
  }
  for(const auto& [settings,error]:std::vector<std::pair<std::string,std::string>>{
    {"true","environment must be a map"},{"{background:\"red\"}","RRGGBB"},{"{background:12}","RRGGBB"},
    {"{grid:0}","grid must be bool"},{"{background:\"#ffffff\",fog:true}","fog must be a map"},
    {"{fog:{start:5,end:5}}","start must be less than end"},{"{fog:{start:10,end:5}}","start must be less than end"},
    {"{fog:{start:-1,end:5}}","between 0 and 500"},{"{fog:{start:0,end:501}}","between 0 and 500"},
    {"{fog:{start:true,end:50}}","must be a finite number"},{"{fog:{color:\"blue\",start:0,end:50}}","RRGGBB"},
    {"{fog:{start:0,end:50,density:.1}}","unsupported field 'density'"},
    {"{grid:true,grid:false}","duplicate map key"},
  })manifestCase(withEnvironment(settings),error);
  const auto withNavigation=[](const std::string& settings){auto manifest=validManifest;manifest.insert(manifest.rfind('}'),settings);return manifest;};
  {
    fixture.reset();const auto defaults=fixture.load();check(!defaults->walk&&defaults->portals.empty(),"worlds without walk/portal fields retain their orbit-navigation model");
    write(fixture.room/"world.link",withNavigation("walk:{min:[-3,0,-30],max:[3,4,3]};"));const auto walking=fixture.load();
    check(walking->walk&&walking->walk->min==Vec3{-3,0,-30}&&walking->walk->max==Vec3{3,4,3}&&walking->walk->eyeHeight==1.65&&walking->walk->speed==3.4,"walk bounds enable horizontal navigation with default eye height and speed");
    write(fixture.room/"world.link",withNavigation("walk:{min:[-3,0,-30],max:[3,4,3],eyeHeight:1.8,speed:4};portals:[{id:\"end_portal\",address:\"link://local/room-a\",position:[0,1.8,-28],size:[2,3.6,1]}];"));const auto configured=fixture.load();
    check(configured->walk->eyeHeight==1.8&&configured->walk->speed==4,"walk configuration keeps explicit eye height and speed");
    check(configured->portals.size()==1&&configured->portals[0].id=="end_portal"&&configured->portals[0].address=="link://local/room-a"&&configured->portals[0].position==Vec3{0,1.8,-28}&&configured->portals[0].size==Vec3{2,3.6,1},"portal declaration carries a canonical-local LINK and its complete eye-trigger volume");
    write(fixture.room/"world.link",withNavigation("portals:[];"));check(fixture.load()->portals.empty(),"an empty portal array is accepted without enabling walking");
    std::string portals="portals:[";
    for(std::size_t i=0;i<Limits::portals;++i){if(i)portals+=',';portals+="{id:\"portal"+std::to_string(i)+"\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]}";}
    write(fixture.room/"world.link",withNavigation(portals+"];"));check(fixture.load()->portals.size()==Limits::portals,"portal count limit is inclusive");
    manifestCase(withNavigation(portals+",{id:\"extra\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]}];"),"portal count budget");
  }
  for(const auto& [settings,error]:std::vector<std::pair<std::string,std::string>>{
    {"walk:true;","walk must be a map"},
    {"walk:{max:[3,4,3]};","missing field 'min'"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],gravity:1};","unsupported field 'gravity'"},
    {"walk:{min:[-3,0],max:[3,4,3]};","three finite"},
    {"walk:{min:[3,0,-30],max:[3,4,3]};","min must be less"},
    {"walk:{min:[-3,4,-30],max:[3,0,3]};","min must be less"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],speed:0};","walk speed"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],speed:21};","walk speed"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],speed:true};","walk speed"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],eyeHeight:.1};","walk eyeHeight"},
    {"walk:{min:[-3,0,-30],max:[3,4,3],eyeHeight:11};","walk eyeHeight"},
    {"walk:{min:[-3,2,-30],max:[3,4,3]};","within vertical bounds"},
    {"portals:true;","portals must be an array"},
    {"portals:[true];","portal must be a map"},
    {"portals:[{id:\"end\",position:[0,1,0],size:[1,2,1]}];","missing field 'address'"},
    {"portals:[{id:\"1end\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]}];","portal id must be an identifier"},
    {"portals:[{id:\"\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]}];","portal id must be a nonempty string"},
    {"portals:[{id:\"end\",address:\"link://remote/room-a\",position:[0,1,0],size:[1,2,1]}];","invalid portal address"},
    {"portals:[{id:\"end\",address:\"https://example.com\",position:[0,1,0],size:[1,2,1]}];","invalid portal address"},
    {"portals:[{id:\"end\",address:\"link://local/missing\",position:[0,1,0],size:[1,2,1]}];","invalid portal address"},
    {"portals:[{id:\"end\",address:\"link://local/../room-a\",position:[0,1,0],size:[1,2,1]}];","invalid portal address"},
    {"portals:[{id:\"end\",address:\"link://local/room-a\",position:[0,1],size:[1,2,1]}];","three finite"},
    {"portals:[{id:\"end\",address:\"link://local/room-a\",position:[0,1,0],size:[1,0,1]}];","portal size must be positive"},
    {"portals:[{id:\"end\",address:\"link://local/room-a\",position:[0,1,0],size:[1001,2,1]}];","portal size must be positive"},
    {"portals:[{id:\"end\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1],onEnter:\"script\"}];","unsupported field 'onEnter'"},
    {"portals:[{id:\"end\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]},{id:\"end\",address:\"link://local/room-a\",position:[0,1,0],size:[1,2,1]}];","duplicate portal id"},
  })manifestCase(withNavigation(settings),error);
  fixture.reset();std::stop_source stop;stop.request_stop();rejects([&]{fixture.load(stop.get_token());},"cancelled");
  {
    fixture.reset();auto manifest=validManifest;manifest.replace(manifest.find("scripts:[]"),10,"scripts:[\"scripts/test.script\"]");write(fixture.room/"world.link",manifest);write(fixture.room/"scripts/test.script","on onStart(ctx) {}");check(fixture.load()->programs.size()==1,"loader parses listed Script source");
    write(fixture.room/"scripts/test.script",std::string(Limits::scriptBytes+1,' '));rejects([&]{fixture.load();},"file byte budget");
    write(fixture.room/"scripts/test.script","on onStart(ctx) { invalid syntax }");rejects([&]{fixture.load();},"expected");
    std::filesystem::remove(fixture.room/"scripts/test.script");rejects([&]{fixture.load();},"missing or inaccessible");
  }
  manifestCase(std::string(Limits::manifestBytes+1,' '),"file byte budget");
  fixture.reset();std::filesystem::remove(fixture.room/"appearance.style");rejects([&]{fixture.load();},"missing or inaccessible");
}
