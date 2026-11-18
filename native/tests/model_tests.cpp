#include "link/model.hpp"
#include "link/loader.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <functional>
using namespace linkworld;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void close(double a,double b,const char* text){check(std::abs(a-b)<1e-6,text);}
void fails(const std::function<void()>& action,const char* text){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,text);}
void word(std::vector<std::uint8_t>& out,std::uint32_t value){for(int i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(i*8)));}
struct Fixture {
 std::vector<std::uint8_t> bin;std::vector<std::string> views,accessors;std::string json;
 template<class T>int add(const std::vector<T>& values,int component,const char* type,std::size_t count){while(bin.size()%4)bin.push_back(0);std::size_t offset=bin.size(),bytes=values.size()*sizeof(T);auto ptr=reinterpret_cast<const std::uint8_t*>(values.data());bin.insert(bin.end(),ptr,ptr+bytes);views.push_back("{\"buffer\":0,\"byteOffset\":"+std::to_string(offset)+",\"byteLength\":"+std::to_string(bytes)+"}");accessors.push_back("{\"bufferView\":"+std::to_string(views.size()-1)+",\"componentType\":"+std::to_string(component)+",\"count\":"+std::to_string(count)+",\"type\":\""+type+"\"}");return static_cast<int>(accessors.size()-1);}
 Fixture(){
  add<float>({0,0,0,1,0,0,0,1,0},5126,"VEC3",3); // 0: triangle
  add<std::uint16_t>({0,1,2},5123,"SCALAR",3); // 1: indices
  add<std::uint16_t>({0,0,0,0,0,0,0,0,1,0,0,0},5123,"VEC4",3); // 2: joints
  add<float>({1,0,0,0,1,0,0,0,1,0,0,0},5126,"VEC4",3); // 3: weights
  add<float>({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,1},5126,"MAT4",2); // 4: inverse bind
  add<float>({0,1},5126,"SCALAR",2); // 5: time
  add<float>({0,1,0,0,2,0},5126,"VEC3",2); // 6: walk translation
  add<float>({0,0,0,1,0,0,.7071067812f,.7071067812f},5126,"VEC4",2); // 7: slash rotation
  std::ostringstream o;o<<"{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":"<<bin.size()<<"}],\"bufferViews\":[";for(std::size_t i=0;i<views.size();++i){if(i)o<<',';o<<views[i];}o<<"],\"accessors\":[";for(std::size_t i=0;i<accessors.size();++i){if(i)o<<',';o<<accessors[i];}o<<R"(],"materials":[{"name":"toon","extensions":{"KHR_materials_unlit":{}},"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,1,1],"roughnessFactor":0.8,"metallicFactor":0}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1,"material":0}]}],"nodes":[{"name":"mesh","mesh":0,"skin":0,"translation":[7,0,0]},{"name":"hip","children":[2]},{"name":"arm","translation":[0,1,0]},{"mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,100,0,0,1]}],"skins":[{"joints":[1,2],"inverseBindMatrices":4}],"scenes":[{"nodes":[0,1]}],"scene":0,"animations":[{"name":"Walk","samplers":[{"input":5,"output":6,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]},{"name":"Slash","samplers":[{"input":5,"output":7,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]}]})";json=o.str();
 }
 void replace(const std::string& from,const std::string& to){auto p=json.find(from);check(p!=std::string::npos,"fixture replacement missing");json.replace(p,from.size(),to);}
 std::vector<std::uint8_t> bytes(){auto text=json;while(text.size()%4)text+=' ';auto binary=bin;while(binary.size()%4)binary.push_back(0);std::vector<std::uint8_t> out;word(out,0x46546c67);word(out,2);word(out,static_cast<std::uint32_t>(12+8+text.size()+8+binary.size()));word(out,static_cast<std::uint32_t>(text.size()));word(out,0x4e4f534a);out.insert(out.end(),text.begin(),text.end());word(out,static_cast<std::uint32_t>(binary.size()));word(out,0x004e4942);out.insert(out.end(),binary.begin(),binary.end());return out;}
};
struct Temp {std::filesystem::path dir;Temp(){dir=std::filesystem::temp_directory_path()/ ("link-model-tests-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);}~Temp(){std::error_code ignored;std::filesystem::remove_all(dir,ignored);}std::filesystem::path save(std::vector<std::uint8_t> data,const char* filename="fixture.glb"){auto p=dir/filename;std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));return p;}};
}
namespace {
Fixture pbrFixture(const std::string& material=R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":2},"metallicFactor":0.6,"roughnessFactor":0.7},"normalTexture":{"index":1,"scale":0.75},"occlusionTexture":{"index":3,"strength":0.4},"emissiveTexture":{"index":4},"emissiveFactor":[0.2,0.3,0.4]})"){
 Fixture f;f.bin.resize(44);const float uv[6]={0,0,1,0,0,1};const auto* data=reinterpret_cast<const std::uint8_t*>(uv);f.bin.insert(f.bin.end(),data,data+sizeof(uv));
 const std::uint8_t png[8]={137,80,78,71,13,10,26,10};for(int i=0;i<5;++i)f.bin.insert(f.bin.end(),std::begin(png),std::end(png));
 std::ostringstream json;json<<"{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":"<<f.bin.size()<<"}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":44,\"byteLength\":24}";
 for(int i=0;i<5;++i)json<<",{\"buffer\":0,\"byteOffset\":"<<68+i*8<<",\"byteLength\":8}";
 json<<R"(],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],"images":[)";
 for(int i=0;i<5;++i){if(i)json<<',';json<<"{\"bufferView\":"<<i+3<<",\"mimeType\":\"image/png\"}";}
 json<<R"(],"textures":[{"source":4},{"source":2},{"source":0},{"source":3},{"source":1}],"materials":[)"<<material<<R"(],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":2},"indices":1,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
 f.json=json.str();return f;
}
void pbrMaterialTests(Temp& temp){
 auto fixture=pbrFixture();auto asset=loadModelGlb(temp.save(fixture.bytes()));check(asset->materials.size()==1&&asset->images.size()==5,"PBR material fixture loaded");const auto& material=asset->materials[0];
 check(material.baseColorImage==4&&material.normalImage==2&&material.metallicRoughnessImage==0&&material.occlusionImage==3&&material.emissiveImage==1,"all PBR maps resolve through texture table to the right embedded image");
 close(material.normalScale,.75,"normal scale imported");close(material.occlusionStrength,.4,"occlusion strength imported");close(material.roughness,.7,"textured material retains roughness factor");close(material.metallic,.6,"textured material retains metallic factor");close(material.emissive[2],.4,"textured material retains emissive factor");
 auto reject=[&](Fixture f,const char* text){fails([&]{loadModelGlb(temp.save(f.bytes()));},text);};
 for(const auto* slot:{"baseColorTexture","metallicRoughnessTexture","normalTexture","occlusionTexture","emissiveTexture"}){
  auto f=pbrFixture();const std::string prefix=std::string("\"")+slot+"\":{\"index\":";
  auto begin=f.json.find(prefix);check(begin!=std::string::npos,"PBR texture slot fixture exists");begin+=prefix.size();f.json.insert(begin+1,",\"texCoord\":1");reject(f,"each PBR map rejects unsupported UV sets");
  f=pbrFixture();begin=f.json.find(prefix)+prefix.size();f.json.replace(begin,1,"99");reject(f,"each PBR map rejects invalid texture references");
  f=pbrFixture();begin=f.json.find(prefix)+prefix.size();f.json.insert(begin+1,",\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.1,0]}}");reject(f,"each PBR map rejects unsupported texture transforms");
 }
 const std::vector<std::string> materials={R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})",R"({"pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0}}})",R"({"normalTexture":{"index":0}})",R"({"occlusionTexture":{"index":0}})",R"({"emissiveTexture":{"index":0}})"};
 for(const auto& specification:materials){auto f=pbrFixture(specification);f.replace(",\"TEXCOORD_0\":2","");reject(f,"any material map requires an actual UV0 accessor");}
 {auto f=pbrFixture();f.replace("\"source\":2","\"source\":99");reject(f,"PBR image references cannot escape image table");}
 {auto f=pbrFixture();f.replace("\"scale\":0.75","\"scale\":1e999");reject(f,"non-finite normal scale rejected");}
 {auto f=pbrFixture();f.replace("\"scale\":0.75","\"scale\":17");reject(f,"normal scale budget enforced");}
 for(const auto* value:{"-0.01","1.01","1e999"}){auto f=pbrFixture();f.replace("\"strength\":0.4",std::string("\"strength\":")+value);reject(f,"occlusion strength range and finiteness enforced");}
 {auto f=pbrFixture(R"({"normalTexture":{"index":0},"occlusionTexture":{"index":0}})");auto m=loadModelGlb(temp.save(f.bytes()))->materials[0];close(m.normalScale,1,"normal scale default is one");close(m.occlusionStrength,1,"occlusion strength default is one");}
 {auto f=pbrFixture(R"({"normalTexture":{"index":0,"scale":-0.5},"occlusionTexture":{"index":0,"strength":0}})");auto m=loadModelGlb(temp.save(f.bytes()))->materials[0];close(m.normalScale,-.5,"signed normal scale preserved");close(m.occlusionStrength,0,"zero occlusion strength preserved");}
}
void worldLoaderTests(Temp& temp,Fixture& fixture){
 const auto worlds=temp.dir/"worlds",directory=worlds/"model";std::filesystem::create_directories(directory);temp.save(fixture.bytes(),"worlds/model/model.glb");temp.save(fixture.bytes(),"worlds/outside.glb");
 auto write=[&](const char* filename,const std::string& text){std::ofstream out(directory/filename,std::ios::binary);out<<text;check(bool(out),"write world loader fixture");};
 auto manifest=[&](bool enabled){return std::string("world {version:\"0.1\";id:\"model\";name:\"Model fixture\";elements:\"scene.element\";style:\"appearance.style\";scripts:[];entry:{position:[0,1,3],target:[0,1,0]};extensions:")+(enabled?"[\"models-v1\"]":"[]")+";}";};
 auto scene=[&](const std::string& source){return "scene {object room {component surface {element hero {geometry:{type:\"model\",source:\""+source+"\"};} element clone {geometry:{type:\"model\",source:\""+source+"\"};}}}}";};
 write("appearance.style","");write("scene.element",scene("model.glb"));write("world.link",manifest(false));
 fails([&]{loadWorld("link://local/model",worlds);},"models-v1 opt-in required");write("world.link",manifest(true));
 auto world=loadWorld("link://local/model",worlds);check(world->models.size()==1,"model resources deduplicated in World.models");auto loaded=world->models.at("model.glb");check(loaded->vertices==3&&loaded->animations.size()==2,"World.models restores the actual GLB asset");auto hero=std::find_if(world->nodes.begin(),world->nodes.end(),[](const auto& n){return n.id=="hero";});check(hero!=world->nodes.end()&&hero->geometry.type==Geometry::Type::Model,"model element loaded");close(hero->geometry.modelMin[0],0,"loader restores bind bounds minimum");close(hero->geometry.modelMax[1],1,"loader restores bind bounds maximum");
 for(const auto* source:{"../outside.glb","%2e%2e/outside.glb","https://example.invalid/model.glb"}){write("scene.element",scene(source));fails([&]{loadWorld("link://local/model",worlds);},"model resource traversal/URL rejected");}
 write("scene.element",scene("model.glb"));auto restored=loadWorld("link://local/model",worlds);check(restored->models.size()==1&&restored->models.at("model.glb")->triangles==1,"successful model reload after rejected resource");check(world->models.at("model.glb")==loaded,"failed candidate loads preserve the current asset map");
}
}
int main(){try{
 Temp temp;Fixture fixture;pbrMaterialTests(temp);worldLoaderTests(temp,fixture);auto asset=loadModelGlb(temp.save(fixture.bytes()));check(asset->vertices==3&&asset->triangles==1,"geometry counts");check(asset->animations.size()==2&&asset->skins[0].joints.size()==2,"skin/clips loaded");check(asset->materials[0].unlit,"unlit material");check(!asset->sceneActive[3],"inactive scene excluded");close(asset->minimum[0],0,"skinned bounds avoid double mesh transform");close(asset->maximum[0],1,"inactive node excluded from bounds");close(asset->maximum[1],1,"bindpose bounds");auto& primitive=asset->meshes[0].primitives[0];auto bind=deformModelPrimitive(*asset,evaluateModel(*asset,"",0),0,primitive);close(bind[2].position[1],1,"inverse bind skin");close(bind[0].normal[2],1,"generated normal");auto walk=deformModelPrimitive(*asset,evaluateModel(*asset,"Walk",.5,false),0,primitive);close(walk[2].position[1],1.5,"linear joint motion");auto loop=deformModelPrimitive(*asset,evaluateModel(*asset,"Walk",1.5,true),0,primitive);close(loop[2].position[1],1.5,"animation looping");auto clamp=deformModelPrimitive(*asset,evaluateModel(*asset,"Walk",8,false),0,primitive);close(clamp[2].position[1],2,"animation clamping");auto slash=deformModelPrimitive(*asset,evaluateModel(*asset,"Slash",.5,false),0,primitive);close(slash[1].position[0],std::sqrt(.5),"quaternion slerp x");close(slash[1].position[1],std::sqrt(.5),"quaternion slerp y");auto end=deformModelPrimitive(*asset,evaluateModel(*asset,"Slash",1,false),0,primitive);close(end[2].position[0],-1,"joint hierarchy transform");fails([&]{evaluateModel(*asset,"missing",0);},"unknown clip rejected");fails([&]{evaluateModel(*asset,"Walk",std::numeric_limits<double>::quiet_NaN());},"NaN clock rejected");fails([&]{loadModelGlb(temp.dir/"missing.glb");},"missing path rejected");fails([&]{loadModelGlb(temp.save(fixture.bytes(),"fixture.gltf"));},"textual glTF extension rejected");
 auto reject=[&](Fixture f,const char* text){fails([&]{loadModelGlb(temp.save(f.bytes()));},text);};
 {auto b=fixture.bytes();b.pop_back();fails([&]{loadModelGlb(temp.save(b));},"truncation rejected");}
 {Fixture f;f.bin[36]=9;reject(f,"out-of-range triangle index rejected");}
 {Fixture f;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(f.bin.data(),&nan,4);reject(f,"NaN vertex rejected");}
 {Fixture f;f.replace("\"joints\":[1,2]","\"joints\":[1]");reject(f,"invalid skin rejected");}
 {Fixture f;f.bin[44]=9;reject(f,"invalid vertex joint rejected");}
 {Fixture f;f.replace("\"byteOffset\":0","\"byteOffset\":999999999999999");reject(f,"offset overflow rejected");}
 {Fixture f;f.replace("\"count\":3","\"count\":65537");reject(f,"vertex/accessor budget rejected");}
 {Fixture f;f.replace("\"byteLength\":"+std::to_string(f.bin.size()),"\"uri\":\"../outside.bin\",\"byteLength\":"+std::to_string(f.bin.size()));reject(f,"external path URI rejected");}
 {Fixture f;f.replace("\"byteLength\":"+std::to_string(f.bin.size()),"\"uri\":\"data:application/octet-stream;base64,AAAA\",\"byteLength\":"+std::to_string(f.bin.size()));reject(f,"data URI rejected");}
 {Fixture f;f.replace("\"interpolation\":\"LINEAR\"","\"interpolation\":\"CUBICSPLINE\"");reject(f,"CUBICSPLINE rejected");}
 {Fixture f;f.replace("\"name\":\"arm\",\"translation\"","\"name\":\"arm\",\"children\":[1],\"translation\"");reject(f,"node cycle rejected");}
 {Fixture f;f.replace("\"asset\":", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],\"asset\":");reject(f,"required extension rejected");}
 {Fixture f;f.replace("\"interpolation\":\"LINEAR\"","\"interpolation\":\"STEP\"");auto s=loadModelGlb(temp.save(f.bytes()));auto p=deformModelPrimitive(*s,evaluateModel(*s,"Walk",.5,false),0,s->meshes[0].primitives[0]);close(p[2].position[1],1,"STEP interpolation");}
 {auto b=fixture.bytes();b.resize(16*1024*1024+1);fails([&]{loadModelGlb(temp.save(b));},"file size budget rejected");}
 {Fixture f;std::string extras;for(int i=0;i<253;++i)extras+="{},";f.replace("\"nodes\":[{\"name\":\"mesh\"","\"nodes\":["+extras+"{\"name\":\"mesh\"");reject(f,"node budget rejected");}
 {Fixture f;f.replace("\"skin\":0,","");f.replace("\"translation\":[7,0,0]","\"matrix\":[2,0,0,0,0,3,0,0,0,0,4,0,7,0,0,1]");f.replace("\"indices\":1,","");auto a=loadModelGlb(temp.save(f.bytes()));check(a->triangles==1,"non-indexed triangles");close(a->minimum[0],7,"static matrix translation");close(a->maximum[0],9,"static nonuniform matrix scale");close(a->maximum[1],3,"static matrix y scale");auto v=deformModelPrimitive(*a,evaluateModel(*a,"",0),0,a->meshes[0].primitives[0]);close(v[0].normal[2],1,"inverse-transpose normal normalized");}
 {Fixture f;float two=2;std::memcpy(f.bin.data()+68,&two,4);auto a=loadModelGlb(temp.save(f.bytes()));close(a->meshes[0].primitives[0].vertices[0].weights[0],1,"weights normalized");}
 {Fixture f;f.replace("\"asset\":", "\"images\":[{\"uri\":\"https://example.invalid/image.png\"}],\"asset\":");reject(f,"external image URI rejected");}
 {Fixture f;f.replace("\"material\":0", "\"targets\":[{\"POSITION\":0}],\"material\":0");reject(f,"morph targets rejected");}
 {Fixture f;f.replace("\"count\":3", "\"count\":18446744073709551615");reject(f,"overflowing accessor count rejected");}
 {Fixture f;f.replace("\"skin\":0,","");f.replace("\"translation\":[7,0,0]","\"scale\":[2,3,4]");float one=1;std::memcpy(f.bin.data()+32,&one,4);auto a=loadModelGlb(temp.save(f.bytes()));auto v=deformModelPrimitive(*a,evaluateModel(*a,"",0),0,a->meshes[0].primitives[0]);close(v[0].normal[1],-.8,"nonuniform inverse-transpose y");close(v[0].normal[2],.6,"nonuniform inverse-transpose z");}
 {Fixture f;std::string extras;for(int i=0;i<32;++i)extras+="{},";f.replace("\"materials\":[","\"materials\":["+extras);reject(f,"unused material table budget rejected");}
 {Fixture f;std::string images;for(int i=0;i<9;++i){if(i)images+=",";images+="{}";}f.replace("\"asset\":","\"images\":["+images+"],\"asset\":");reject(f,"unused image table budget rejected");}
 {Fixture f;f.replace("\"path\":\"translation\"","\"path\":\"scale\"");float negative=-2;
  // The final VEC3 output begins 32 bytes before the final quaternion output.
  std::size_t walkOffset=f.bin.size()-32-24;float scale[6]={1,1,1,1,negative,1};std::memcpy(f.bin.data()+walkOffset,scale,sizeof(scale));reject(f,"scale interpolation through zero rejected");}
 {Fixture f;f.replace("\"nodes\":[0,1]","\"nodes\":[0,1,3]");auto a=loadModelGlb(temp.save(f.bytes()));check(a->vertices==3&&a->triangles==1,"unique mesh counts stay unique");check(a->drawVertices==6&&a->drawTriangles==2,"internal mesh instances counted for draw budget");close(a->maximum[0],101,"active instanced mesh included in bounds");}
 {Fixture f;f.bin.resize(36);for(std::uint32_t i=0;i<65536*3;++i){auto index=static_cast<std::uint16_t>(i%3);f.bin.push_back(static_cast<std::uint8_t>(index));f.bin.push_back(static_cast<std::uint8_t>(index>>8));}
  f.json=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":393252}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":393216}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":196608,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0},{"mesh":0},{"mesh":0},{"mesh":0}],"scenes":[{"nodes":[0,1,2,3]}],"scene":0})";
  reject(f,"GLB internal instances cannot bypass draw triangle budget");}
 std::cout<<"GLB model, animation, skinning and malformed-input tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
