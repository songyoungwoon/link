#include "link/model.hpp"
#define CGLTF_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
#include "../../third_party/cgltf/cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <cstdlib>
#include <cwctype>

namespace linkworld {
namespace {
constexpr std::size_t maxFile=16*1024*1024, maxVertices=65536, maxTriangles=65536;
[[noreturn]] void bad(const std::string& reason) {throw std::runtime_error("GLB: "+reason);}
void need(bool condition,const char* reason) {if(!condition) bad(reason);}
void need(bool condition,const std::string& reason) {if(!condition) bad(reason);}
void finite(double v) {need(std::isfinite(v),"non-finite numeric value");}
template<std::size_t N> void finite(const std::array<double,N>& v) {for(auto x:v) finite(x);}
std::string name(const char* value,const std::string& fallback="") {return value?value:fallback;}
ModelMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
// glTF matrices are column-major; world = parentWorld * local (T * R * S).
ModelMatrix multiply(const ModelMatrix& a,const ModelMatrix& b) {
 ModelMatrix out{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];finite(out);return out;
}
Vec3 point(const ModelMatrix& m,const Vec3& v) {
 Vec3 o{};for(int r=0;r<3;++r)o[r]=m[r]*v[0]+m[4+r]*v[1]+m[8+r]*v[2]+m[12+r];finite(o);return o;
}
Vec3 cross(const Vec3& a,const Vec3& b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec3 subtract(const Vec3& a,const Vec3& b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vec3 unit(Vec3 n) {finite(n);double l=std::hypot(n[0],n[1],n[2]);need(l>1e-15,"zero-length normal");for(auto& x:n)x/=l;return n;}
std::array<double,4> quaternion(std::array<double,4> q) {finite(q);double l=0;for(auto x:q)l+=x*x;need(l>1e-20,"zero quaternion");l=std::sqrt(l);for(auto& x:q)x/=l;return q;}
ModelMatrix normalMatrix(const ModelMatrix& m) {
 const Vec3 x{m[0],m[1],m[2]},y{m[4],m[5],m[6]},z{m[8],m[9],m[10]};
 auto a=cross(y,z),b=cross(z,x),c=cross(x,y);double det=x[0]*a[0]+x[1]*a[1]+x[2]*a[2];need(std::isfinite(det)&&std::abs(det)>1e-18,"singular transform is unsupported");
 auto out=identity();for(int r=0;r<3;++r){out[r]=a[r]/det;out[4+r]=b[r]/det;out[8+r]=c[r]/det;}return out;
}
ModelMatrix trs(const Vec3& t,const std::array<double,4>& source,const Vec3& s) {
 finite(t);finite(s);auto q=quaternion(source);double x=q[0],y=q[1],z=q[2],w=q[3];
 ModelMatrix m{(1-2*y*y-2*z*z)*s[0],(2*x*y+2*w*z)*s[0],(2*x*z-2*w*y)*s[0],0,
 (2*x*y-2*w*z)*s[1],(1-2*x*x-2*z*z)*s[1],(2*y*z+2*w*x)*s[1],0,
 (2*x*z+2*w*y)*s[2],(2*y*z-2*w*x)*s[2],(1-2*x*x-2*y*y)*s[2],0,t[0],t[1],t[2],1};return m;
}
void affine(const ModelMatrix& m) {finite(m);need(std::abs(m[3])<1e-9&&std::abs(m[7])<1e-9&&std::abs(m[11])<1e-9&&std::abs(m[15]-1)<1e-9,"matrix must be affine");(void)normalMatrix(m);}
std::array<double,4> slerp(std::array<double,4> a,std::array<double,4> b,double t) {
 a=quaternion(a);b=quaternion(b);double dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];if(dot<0){dot=-dot;for(auto& x:b)x=-x;}
 std::array<double,4> out{};if(dot>.9995){for(int i=0;i<4;++i)out[i]=a[i]+(b[i]-a[i])*t;}else{double angle=std::acos(std::clamp(dot,-1.,1.)),den=std::sin(angle);for(int i=0;i<4;++i)out[i]=(a[i]*std::sin((1-t)*angle)+b[i]*std::sin(t*angle))/den;}return quaternion(out);
}
std::uint32_t read32(const std::vector<std::uint8_t>& bytes,std::size_t at) {need(at<=bytes.size()&&bytes.size()-at>=4,"truncated GLB header");return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);}
// Bound parser allocations before JSON resource counts can be inspected.
struct AllocationBudget {std::size_t bytes=0;};
void* allocate(void* user,cgltf_size size) {auto& used=static_cast<AllocationBudget*>(user)->bytes;if(size>64*1024*1024||used>64*1024*1024-size||size>std::numeric_limits<std::size_t>::max()-sizeof(std::max_align_t))return nullptr;auto p=static_cast<std::max_align_t*>(std::malloc(size+sizeof(std::max_align_t)));if(!p)return nullptr;std::memcpy(p,&size,sizeof size);used+=size;return p+1;}
void release(void* user,void* ptr) {if(!ptr)return;auto p=static_cast<std::max_align_t*>(ptr)-1;std::size_t size;std::memcpy(&size,p,sizeof size);static_cast<AllocationBudget*>(user)->bytes-=size;std::free(p);}
std::array<double,4> read(const cgltf_accessor* a,std::size_t index,std::size_t count) {
 need(a&&index<a->count&&count<=4,"invalid accessor read");cgltf_float v[4]{};need(cgltf_accessor_read_float(a,index,v,count)!=0,"accessor cannot be read");std::array<double,4> out{};for(std::size_t i=0;i<count;++i){out[i]=v[i];finite(out[i]);}return out;
}
void shape(const cgltf_accessor* a,cgltf_type type,cgltf_component_type component) {need(a&&a->type==type&&a->component_type==component&&!a->normalized,"unsupported accessor type/component");}
void bounded(cgltf_data& d) {
 need(d.file_type==cgltf_file_type_glb,"only binary GLB 2 is supported");need(name(d.asset.version)=="2.0","only glTF 2.0 is supported");
 need(d.buffers_count==1&&d.buffers[0].uri==nullptr&&d.bin,"exactly one embedded BIN buffer is required; external/data URIs are unsupported");
 need(d.buffers[0].size<=d.bin_size&&d.bin_size-d.buffers[0].size<=3,"BIN length disagrees with buffer byteLength");
 need(d.nodes_count>0&&d.nodes_count<=256&&d.meshes_count<=256&&d.materials_count<=32&&d.images_count<=8&&d.skins_count<=64&&d.animations_count<=32,"resource budget exceeded");
 need(d.accessors_count<=2048&&d.buffer_views_count<=2048&&d.textures_count<=32&&d.scenes_count<=32,"resource table budget exceeded");
 for(std::size_t i=0;i<d.extensions_required_count;++i)need(name(d.extensions_required[i])=="KHR_materials_unlit","unsupported required extension: "+name(d.extensions_required[i]));
 // Never call cgltf_load_buffers: even optional external/data URIs are refused.
 d.buffers[0].data=const_cast<void*>(d.bin);d.buffers[0].data_free_method=cgltf_data_free_method_none;
 for(std::size_t i=0;i<d.buffer_views_count;++i){auto& v=d.buffer_views[i];need(v.buffer==&d.buffers[0]&&!v.has_meshopt_compression,"external/compressed bufferView is unsupported");need(v.offset<=d.buffers[0].size&&v.size<=d.buffers[0].size-v.offset,"bufferView exceeds BIN");need(v.stride==0||(v.stride>=4&&v.stride<=252&&v.stride%4==0),"invalid bufferView stride");}
 for(std::size_t i=0;i<d.accessors_count;++i){auto& a=d.accessors[i];need(!a.is_sparse&&a.buffer_view,"sparse/missing-buffer accessors are unsupported");need(a.count>0&&a.count<=maxTriangles*3,"accessor count exceeds budget");auto component=cgltf_component_size(a.component_type),size=cgltf_calc_size(a.type,a.component_type);need(component>0&&size>0&&a.stride>=size&&a.stride%component==0,"invalid accessor layout");auto& v=*a.buffer_view;need(a.offset<=v.size&&size<=v.size-a.offset,"accessor offset exceeds bufferView");need(a.count-1<=(v.size-a.offset-size)/a.stride,"accessor count/stride exceeds bufferView");need((v.offset+a.offset)%component==0,"misaligned accessor");if(a.has_min)for(std::size_t c=0;c<cgltf_num_components(a.type);++c)finite(a.min[c]);if(a.has_max)for(std::size_t c=0;c<cgltf_num_components(a.type);++c)finite(a.max[c]);}
 need(cgltf_validate(&d)==cgltf_result_success,"invalid glTF structure or index bounds");
}
}

ModelFrame evaluateModel(const ModelAsset& asset,const std::string& clip,double seconds,bool loop) {
 finite(seconds);need(seconds>=0,"animation time must be non-negative");auto nodes=asset.nodes;
 if(!clip.empty()){
  auto it=std::find_if(asset.animations.begin(),asset.animations.end(),[&](const auto& a){return a.name==clip;});need(it!=asset.animations.end(),"unknown animation clip: "+clip);const auto& anim=*it;
  double time=anim.duration>0?(loop?std::fmod(seconds,anim.duration):std::min(seconds,anim.duration)):0;
  for(const auto& channel:anim.channels){const auto& sampler=anim.samplers.at(channel.sampler);need(!sampler.times.empty(),"empty animation sampler");auto upper=std::upper_bound(sampler.times.begin(),sampler.times.end(),time);std::size_t first=upper==sampler.times.begin()?0:std::size_t(upper-sampler.times.begin()-1),last=std::min(first+1,sampler.times.size()-1);double factor=last==first||sampler.interpolation=="STEP"?0:std::clamp((time-sampler.times[first])/(sampler.times[last]-sampler.times[first]),0.,1.);auto value=sampler.values[first];if(channel.path=="rotation")value=slerp(value,sampler.values[last],factor);else for(int c=0;c<3;++c)value[c]+=(sampler.values[last][c]-value[c])*factor;
   auto& node=nodes.at(channel.node);if(channel.path=="translation")std::copy_n(value.begin(),3,node.translation.begin());else if(channel.path=="scale")std::copy_n(value.begin(),3,node.scale.begin());else if(channel.path=="rotation")node.rotation=value;else bad("unsupported animation path");
  }
 }
 ModelFrame frame;frame.nodes.resize(nodes.size());std::vector<unsigned char> state(nodes.size());
 std::function<void(std::size_t)> visit=[&](std::size_t i){need(i<nodes.size(),"invalid node index");need(state[i]!=1,"node hierarchy contains a cycle");if(state[i]==2)return;state[i]=1;const auto& n=nodes[i];auto local=n.hasMatrix?n.matrix:trs(n.translation,n.rotation,n.scale);affine(local);if(n.parent>=0){visit(static_cast<std::size_t>(n.parent));frame.nodes[i]=multiply(frame.nodes[n.parent],local);}else frame.nodes[i]=local;state[i]=2;};
 for(std::size_t i=0;i<nodes.size();++i)visit(i);return frame;
}

std::vector<ModelDrawVertex> deformModelPrimitive(const ModelAsset& asset,const ModelFrame& frame,std::size_t nodeIndex,const ModelPrimitive& primitive) {
 need(nodeIndex<asset.nodes.size()&&frame.nodes.size()==asset.nodes.size(),"invalid deformation frame");const auto& node=asset.nodes[nodeIndex];std::vector<ModelMatrix> palette,normals;
 // meshWorld * inverse(meshWorld) cancels in glTF skinning: palette is
 // jointWorld * inverseBind, so returned positions are already asset-world.
 if(node.skin>=0){need(static_cast<std::size_t>(node.skin)<asset.skins.size(),"invalid skin index");const auto& skin=asset.skins[node.skin];for(std::size_t i=0;i<skin.joints.size();++i){auto matrix=multiply(frame.nodes.at(skin.joints[i]),skin.inverseBindMatrices.at(i));palette.push_back(matrix);normals.push_back(normalMatrix(matrix));}}
 auto normal=normalMatrix(frame.nodes[nodeIndex]);std::vector<ModelDrawVertex> out;out.reserve(primitive.vertices.size());
 for(const auto& vertex:primitive.vertices){ModelDrawVertex v{vertex.position,vertex.normal,vertex.uv,vertex.color};if(node.skin<0){v.position=point(frame.nodes[nodeIndex],vertex.position);v.normal=unit(point(normal,vertex.normal));}else{v.position={};v.normal={};double sum=0;for(std::size_t j=0;j<4;++j){double w=vertex.weights[j];if(w<=0)continue;need(vertex.joints[j]<palette.size(),"vertex joint exceeds skin");sum+=w;auto p=point(palette[vertex.joints[j]],vertex.position),n=point(normals[vertex.joints[j]],vertex.normal);for(int c=0;c<3;++c){v.position[c]+=p[c]*w;v.normal[c]+=n[c]*w;}}need(sum>0,"skinned vertex has no weights");v.normal=unit(v.normal);finite(v.position);}out.push_back(v);}
 return out;
}

std::shared_ptr<const ModelAsset> loadModelGlb(const std::filesystem::path& path) {
 auto extension=path.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});need(extension==L".glb","only .glb files are supported");
 std::ifstream input(path,std::ios::binary|std::ios::ate);need(bool(input),"cannot open model file");auto length=input.tellg();need(length>=20&&length<=static_cast<std::streamoff>(maxFile),"model file is truncated or exceeds 16 MiB");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));input.seekg(0);need(bool(input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))),"model file read failed");
 need(read32(bytes,0)==0x46546c67&&read32(bytes,4)==2&&read32(bytes,8)==bytes.size(),"invalid GLB 2 header/length");std::size_t cursor=12;unsigned chunks=0;while(cursor<bytes.size()){need(bytes.size()-cursor>=8,"truncated GLB chunk");auto size=read32(bytes,cursor),type=read32(bytes,cursor+4);need(size%4==0&&size<=bytes.size()-cursor-8,"invalid GLB chunk length");need((chunks==0&&type==0x4e4f534a)||(chunks==1&&type==0x004e4942),"expected JSON followed by one embedded BIN chunk");cursor+=8+size;++chunks;}need(chunks==2,"JSON and BIN chunks are required");
 AllocationBudget budget;cgltf_options options{};options.type=cgltf_file_type_glb;options.memory={allocate,release,&budget};cgltf_data* raw=nullptr;auto result=cgltf_parse(&options,bytes.data(),bytes.size(),&raw);need(result==cgltf_result_success,"cannot parse bounded GLB (malformed JSON or allocation budget)");std::unique_ptr<cgltf_data,decltype(&cgltf_free)> guard(raw,cgltf_free);auto& d=*raw;bounded(d);auto asset=std::make_shared<ModelAsset>();
 std::size_t imageBytes=0;
 for(std::size_t i=0;i<d.images_count;++i){const auto& source=d.images[i];need(!source.uri&&source.buffer_view&&source.mime_type,"images must be embedded PNG/JPEG bufferViews");const auto& view=*source.buffer_view;need(view.size<=8*1024*1024-imageBytes,"embedded image budget exceeds 8 MiB");imageBytes+=view.size;ModelImage image;image.mimeType=source.mime_type;auto p=static_cast<const std::uint8_t*>(d.bin)+view.offset;image.bytes.assign(p,p+view.size);bool png=image.mimeType=="image/png"&&view.size>=8&&std::memcmp(p,"\x89PNG\r\n\x1a\n",8)==0;bool jpeg=image.mimeType=="image/jpeg"&&view.size>=3&&p[0]==255&&p[1]==216&&p[2]==255;need(png||jpeg,"unsupported or invalid embedded image format");asset->images.push_back(std::move(image));}
 for(std::size_t i=0;i<d.textures_count;++i){const auto& t=d.textures[i];need(t.image&&!t.has_basisu&&!t.has_webp,"unsupported texture image extension");if(t.sampler)need(t.sampler->wrap_s==cgltf_wrap_mode_repeat&&t.sampler->wrap_t==cgltf_wrap_mode_repeat,"initial GLB profile requires repeat texture wrapping");}
 for(std::size_t i=0;i<d.materials_count;++i){const auto& s=d.materials[i];need(!s.has_pbr_specular_glossiness&&!s.has_clearcoat&&!s.has_transmission&&!s.has_volume&&!s.has_ior&&!s.has_specular&&!s.has_sheen&&!s.has_emissive_strength&&!s.has_iridescence&&!s.has_diffuse_transmission&&!s.has_anisotropy&&!s.has_dispersion,"unsupported material extension");ModelMaterial m;m.name=name(s.name);for(int c=0;c<4;++c){m.baseColor[c]=s.pbr_metallic_roughness.base_color_factor[c];finite(m.baseColor[c]);need(m.baseColor[c]>=0&&m.baseColor[c]<=1,"base color must be in [0,1]");}for(int c=0;c<3;++c){m.emissive[c]=s.emissive_factor[c];finite(m.emissive[c]);need(m.emissive[c]>=0&&m.emissive[c]<=1,"emissive factor must be in [0,1]");}m.metallic=s.pbr_metallic_roughness.metallic_factor;m.roughness=s.pbr_metallic_roughness.roughness_factor;finite(m.metallic);finite(m.roughness);need(m.metallic>=0&&m.metallic<=1&&m.roughness>=0&&m.roughness<=1,"invalid metallic/roughness factor");m.doubleSided=s.double_sided!=0;m.unlit=s.unlit!=0;m.alphaMode=s.alpha_mode==cgltf_alpha_mode_blend?"BLEND":s.alpha_mode==cgltf_alpha_mode_mask?"MASK":"OPAQUE";m.alphaCutoff=s.alpha_cutoff;finite(m.alphaCutoff);need(m.alphaCutoff>=0&&m.alphaCutoff<=1,"invalid alpha cutoff");const auto imageIndex=[&](const cgltf_texture_view& view){
   if(!view.texture)return -1;
   need(view.texcoord==0&&!view.has_transform,"material texture requires untransformed TEXCOORD_0");
   need(view.texture->image!=nullptr,"material texture has no embedded image");
   const auto image=static_cast<std::size_t>(view.texture->image-d.images);
   need(image<asset->images.size(),"material texture image index exceeds image table");
   return static_cast<int>(image);
  };
  m.baseColorImage=imageIndex(s.pbr_metallic_roughness.base_color_texture);
  m.normalImage=imageIndex(s.normal_texture);
  m.metallicRoughnessImage=imageIndex(s.pbr_metallic_roughness.metallic_roughness_texture);
  m.occlusionImage=imageIndex(s.occlusion_texture);
  m.emissiveImage=imageIndex(s.emissive_texture);
  if(m.normalImage>=0){m.normalScale=s.normal_texture.scale;finite(m.normalScale);need(std::abs(m.normalScale)<=16,"normal scale must be finite and between -16 and 16");}
  if(m.occlusionImage>=0){m.occlusionStrength=s.occlusion_texture.scale;finite(m.occlusionStrength);need(m.occlusionStrength>=0&&m.occlusionStrength<=1,"occlusion strength must be between 0 and 1");}
  asset->materials.push_back(std::move(m));}
 for(std::size_t mi=0;mi<d.meshes_count;++mi){const auto& mesh=d.meshes[mi];need(!mesh.weights_count&&mesh.primitives_count<=1024,"morph weights or too many primitives");ModelMesh converted;
  for(std::size_t pi=0;pi<mesh.primitives_count;++pi){const auto& p=mesh.primitives[pi];need(p.type==cgltf_primitive_type_triangles&&!p.targets_count&&!p.has_draco_mesh_compression&&!p.mappings_count,"only uncompressed triangles without morph targets/material variants are supported");const cgltf_accessor *position=nullptr,*normal=nullptr,*uv=nullptr,*color=nullptr,*joints=nullptr,*weights=nullptr;
   for(std::size_t ai=0;ai<p.attributes_count;++ai){const auto& a=p.attributes[ai];const cgltf_accessor** target=nullptr;if(a.type==cgltf_attribute_type_position)target=&position;else if(a.type==cgltf_attribute_type_normal)target=&normal;else if(a.type==cgltf_attribute_type_texcoord)target=&uv;else if(a.type==cgltf_attribute_type_color)target=&color;else if(a.type==cgltf_attribute_type_joints)target=&joints;else if(a.type==cgltf_attribute_type_weights)target=&weights;else if(a.type==cgltf_attribute_type_tangent)continue;else bad("unsupported vertex attribute: "+name(a.name));need(a.index==0&&!*target,"duplicate or unsupported attribute set");*target=a.data;}
   shape(position,cgltf_type_vec3,cgltf_component_type_r_32f);need(position->count<=maxVertices-asset->vertices,"vertex budget exceeds 65536");asset->vertices+=position->count;if(normal)shape(normal,cgltf_type_vec3,cgltf_component_type_r_32f);if(uv)need(uv->type==cgltf_type_vec2&&((uv->component_type==cgltf_component_type_r_32f&&!uv->normalized)||((uv->component_type==cgltf_component_type_r_8u||uv->component_type==cgltf_component_type_r_16u)&&uv->normalized)),"invalid TEXCOORD_0 format");if(color)need((color->type==cgltf_type_vec3||color->type==cgltf_type_vec4)&&((color->component_type==cgltf_component_type_r_32f&&!color->normalized)||((color->component_type==cgltf_component_type_r_8u||color->component_type==cgltf_component_type_r_16u)&&color->normalized)),"invalid COLOR_0 format");need(bool(joints)==bool(weights),"JOINTS_0 and WEIGHTS_0 must occur together");if(joints){need(joints->type==cgltf_type_vec4&&!joints->normalized&&(joints->component_type==cgltf_component_type_r_8u||joints->component_type==cgltf_component_type_r_16u),"invalid JOINTS_0 format");need(weights->type==cgltf_type_vec4&&((weights->component_type==cgltf_component_type_r_32f&&!weights->normalized)||((weights->component_type==cgltf_component_type_r_8u||weights->component_type==cgltf_component_type_r_16u)&&weights->normalized)),"invalid WEIGHTS_0 format");}
   ModelPrimitive primitive;primitive.material=p.material?static_cast<int>(p.material-d.materials):-1;primitive.vertices.resize(position->count);
   for(std::size_t vi=0;vi<position->count;++vi){auto& v=primitive.vertices[vi];auto pos=read(position,vi,3);std::copy_n(pos.begin(),3,v.position.begin());if(normal){auto n=read(normal,vi,3);v.normal=unit({n[0],n[1],n[2]});}else v.normal={};if(uv){auto t=read(uv,vi,2);v.uv={t[0],t[1]};}if(color){auto c=read(color,vi,cgltf_num_components(color->type));if(color->type==cgltf_type_vec3)c[3]=1;for(auto x:c)need(x>=0&&x<=1,"vertex color must be in [0,1]");v.color=c;}if(joints){cgltf_uint joint[4]{};need(cgltf_accessor_read_uint(joints,vi,joint,4)!=0,"invalid joint accessor");v.weights=read(weights,vi,4);double sum=0;for(int j=0;j<4;++j){need(joint[j]<=65535&&v.weights[j]>=0,"invalid joint/negative weight");v.joints[j]=static_cast<std::uint16_t>(joint[j]);sum+=v.weights[j];}need(sum>1e-15,"skin weights must have positive sum");for(auto& w:v.weights)w/=sum;}}
   if(p.indices)need(!p.indices->normalized,"index accessor must not be normalized");
   auto count=p.indices?p.indices->count:position->count;need(count%3==0&&count/3<=maxTriangles-asset->triangles,"triangle budget exceeded or incomplete triangle");asset->triangles+=count/3;primitive.indices.reserve(count);for(std::size_t ii=0;ii<count;++ii){auto index=p.indices?cgltf_accessor_read_index(p.indices,ii):ii;need(index<position->count,"triangle index exceeds vertices");primitive.indices.push_back(static_cast<std::uint32_t>(index));}
   if(!normal){for(std::size_t ii=0;ii<count;ii+=3){auto a=primitive.indices[ii],b=primitive.indices[ii+1],c=primitive.indices[ii+2];auto n=cross(subtract(primitive.vertices[b].position,primitive.vertices[a].position),subtract(primitive.vertices[c].position,primitive.vertices[a].position));for(auto index:{a,b,c})for(int k=0;k<3;++k)primitive.vertices[index].normal[k]+=n[k];}for(auto& v:primitive.vertices){if(std::hypot(v.normal[0],v.normal[1],v.normal[2])<1e-15)v.normal={0,1,0};else v.normal=unit(v.normal);}}
   if(primitive.material>=0){const auto& m=asset->materials[primitive.material];
    if(m.baseColorImage>=0||m.normalImage>=0||m.metallicRoughnessImage>=0||m.occlusionImage>=0||m.emissiveImage>=0)need(uv,"textured primitive requires TEXCOORD_0");
   }converted.primitives.push_back(std::move(primitive));
  }asset->meshes.push_back(std::move(converted));
 }
 asset->nodes.resize(d.nodes_count);std::vector<int> parent(d.nodes_count,-1);
 for(std::size_t i=0;i<d.nodes_count;++i){auto& s=d.nodes[i];need(!s.has_mesh_gpu_instancing&&!s.weights_count,"node instancing/morph weights are unsupported");for(std::size_t c=0;c<s.children_count;++c){auto child=static_cast<std::size_t>(s.children[c]-d.nodes);need(child<d.nodes_count&&parent[child]<0,"node has duplicate/multiple parents");parent[child]=static_cast<int>(i);}auto& n=asset->nodes[i];n.name=name(s.name);n.mesh=s.mesh?static_cast<int>(s.mesh-d.meshes):-1;n.skin=s.skin?static_cast<int>(s.skin-d.skins):-1;need(n.skin<0||n.mesh>=0,"skin requires a mesh node");n.hasMatrix=s.has_matrix!=0;need(!n.hasMatrix||(!s.has_translation&&!s.has_rotation&&!s.has_scale),"matrix and TRS must not coexist");std::copy_n(s.translation,3,n.translation.begin());std::copy_n(s.scale,3,n.scale.begin());std::copy_n(s.rotation,4,n.rotation.begin());n.rotation=quaternion(n.rotation);std::copy_n(s.matrix,16,n.matrix.begin());}
 for(std::size_t i=0;i<d.nodes_count;++i)asset->nodes[i].parent=parent[i];
 std::size_t jointCount=0;
 for(std::size_t si=0;si<d.skins_count;++si){const auto& s=d.skins[si];need(s.joints_count>0&&s.joints_count<=64&&jointCount<=256-s.joints_count,"skin joint budget exceeded");jointCount+=s.joints_count;ModelSkin skin;std::set<std::size_t> unique;for(std::size_t ji=0;ji<s.joints_count;++ji){auto index=static_cast<std::size_t>(s.joints[ji]-d.nodes);need(index<d.nodes_count&&unique.insert(index).second,"invalid/duplicate skin joint");skin.joints.push_back(index);}if(s.inverse_bind_matrices){shape(s.inverse_bind_matrices,cgltf_type_mat4,cgltf_component_type_r_32f);need(s.inverse_bind_matrices->count==s.joints_count,"inverse-bind count disagrees with joints");}for(std::size_t ji=0;ji<s.joints_count;++ji){auto m=identity();if(s.inverse_bind_matrices){cgltf_float values[16]{};need(cgltf_accessor_read_float(s.inverse_bind_matrices,ji,values,16)!=0,"cannot read inverse-bind matrix");std::copy_n(values,16,m.begin());}affine(m);skin.inverseBindMatrices.push_back(m);}asset->skins.push_back(std::move(skin));}
 for(const auto& n:asset->nodes)if(n.skin>=0)for(const auto& p:asset->meshes[n.mesh].primitives)for(const auto& v:p.vertices){double sum=0;for(int j=0;j<4;++j){sum+=v.weights[j];need(v.joints[j]<asset->skins[n.skin].joints.size(),"vertex joint index exceeds skin");}need(sum>.99&&sum<1.01,"mesh bound to skin requires JOINTS_0/WEIGHTS_0");}
 asset->sceneActive.resize(d.nodes_count,false);auto scene=d.scene?d.scene:(d.scenes_count?&d.scenes[0]:nullptr);if(scene){for(std::size_t i=0;i<scene->nodes_count;++i)asset->sceneRoots.push_back(static_cast<std::size_t>(scene->nodes[i]-d.nodes));}else for(std::size_t i=0;i<d.nodes_count;++i)if(parent[i]<0)asset->sceneRoots.push_back(i);std::function<void(std::size_t)> activate=[&](std::size_t i){need(i<d.nodes_count&&!asset->sceneActive[i],"invalid/duplicate scene root or cycle");asset->sceneActive[i]=true;for(std::size_t c=0;c<d.nodes[i].children_count;++c)activate(static_cast<std::size_t>(d.nodes[i].children[c]-d.nodes));};for(auto i:asset->sceneRoots)activate(i);
 std::size_t timekeys=0;std::set<std::string> clipNames;
 for(std::size_t ai=0;ai<d.animations_count;++ai){const auto& a=d.animations[ai];need(a.samplers_count>0&&a.samplers_count<=256&&a.channels_count>0&&a.channels_count<=768,"animation sampler/channel budget exceeded");ModelAnimation animation;animation.name=name(a.name,"clip-"+std::to_string(ai));need(!animation.name.empty()&&clipNames.insert(animation.name).second,"animation names must be unique");for(std::size_t si=0;si<a.samplers_count;++si){const auto& s=a.samplers[si];shape(s.input,cgltf_type_scalar,cgltf_component_type_r_32f);need(s.output&&s.output->component_type==cgltf_component_type_r_32f&&!s.output->normalized&&(s.output->type==cgltf_type_vec3||s.output->type==cgltf_type_vec4),"unsupported animation output");need((s.interpolation==cgltf_interpolation_type_linear||s.interpolation==cgltf_interpolation_type_step)&&s.output->count==s.input->count,"CUBICSPLINE and mismatched sampler counts are unsupported");need(s.input->count<=10000-timekeys,"animation key budget exceeds 10000");timekeys+=s.input->count;ModelAnimationSampler sampler;sampler.interpolation=s.interpolation==cgltf_interpolation_type_step?"STEP":"LINEAR";for(std::size_t ki=0;ki<s.input->count;++ki){double t=read(s.input,ki,1)[0];need(t>=0&&(ki==0||t>sampler.times.back()),"animation times must be non-negative and strictly increasing");sampler.times.push_back(t);sampler.values.push_back(read(s.output,ki,cgltf_num_components(s.output->type)));}animation.duration=std::max(animation.duration,sampler.times.back());animation.samplers.push_back(std::move(sampler));}
  std::set<std::pair<std::size_t,std::string>> targets;
  for(std::size_t ci=0;ci<a.channels_count;++ci){const auto& c=a.channels[ci];need(c.target_node&&c.sampler,"animation channel requires node and sampler");ModelAnimationChannel channel;channel.node=static_cast<std::size_t>(c.target_node-d.nodes);channel.sampler=static_cast<std::size_t>(c.sampler-a.samplers);need(channel.node<asset->nodes.size()&&channel.sampler<animation.samplers.size()&&!asset->nodes[channel.node].hasMatrix,"animation requires a TRS node");channel.path=c.target_path==cgltf_animation_path_type_translation?"translation":c.target_path==cgltf_animation_path_type_rotation?"rotation":c.target_path==cgltf_animation_path_type_scale?"scale":"";need(!channel.path.empty()&&targets.insert({channel.node,channel.path}).second,"unsupported/duplicate animation channel");need(c.sampler->output->type==(channel.path=="rotation"?cgltf_type_vec4:cgltf_type_vec3),"animation channel output shape mismatch");if(channel.path=="rotation")for(auto& value:animation.samplers[channel.sampler].values)value=quaternion(value);if(channel.path=="scale"){const auto& sampler=animation.samplers[channel.sampler];for(std::size_t ki=0;ki<sampler.values.size();++ki)for(int k=0;k<3;++k){need(std::abs(sampler.values[ki][k])>1e-9,"zero animated scale is unsupported");if(ki&&sampler.interpolation=="LINEAR")need(sampler.values[ki][k]*sampler.values[ki-1][k]>0,"animated scale must not interpolate through zero");}}animation.channels.push_back(std::move(channel));}asset->animations.push_back(std::move(animation));
 }
 auto frame=evaluateModel(*asset,"",0);bool first=true;
 for(std::size_t ni=0;ni<asset->nodes.size();++ni){const auto& n=asset->nodes[ni];if(!asset->sceneActive[ni]||n.mesh<0)continue;for(const auto& p:asset->meshes[n.mesh].primitives){need(p.vertices.size()<=262144-asset->drawVertices,"draw-instance vertex budget exceeded");need(p.indices.size()/3<=200000-asset->drawTriangles,"draw-instance triangle budget exceeded");asset->drawVertices+=p.vertices.size();asset->drawTriangles+=p.indices.size()/3;for(const auto& v:deformModelPrimitive(*asset,frame,ni,p)){if(first){asset->minimum=asset->maximum=v.position;first=false;}else for(int c=0;c<3;++c){asset->minimum[c]=std::min(asset->minimum[c],v.position[c]);asset->maximum[c]=std::max(asset->maximum[c],v.position[c]);}}}}
 need(!first,"active scene contains no triangle geometry");return asset;
}
} // namespace linkworld
