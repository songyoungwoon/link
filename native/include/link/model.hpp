#pragma once
#include "core.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace linkworld {
using ModelMatrix=std::array<double,16>;
struct ModelVertex {
  Vec3 position{0,0,0},normal{0,1,0};
  std::array<double,2> uv{0,0};std::array<double,4> color{1,1,1,1};
  std::array<std::uint16_t,4> joints{0,0,0,0};std::array<double,4> weights{0,0,0,0};
};
struct ModelPrimitive {std::vector<ModelVertex> vertices;std::vector<std::uint32_t> indices;int material=-1;};
struct ModelMesh {std::vector<ModelPrimitive> primitives;};
struct ModelMaterial {
  std::string name;std::array<double,4> baseColor{1,1,1,1};Vec3 emissive{0,0,0};
  double metallic=1,roughness=1;int baseColorImage=-1;bool doubleSided=false,unlit=false;
  std::string alphaMode="OPAQUE";double alphaCutoff=.5;
  // Texture references are indices into ModelAsset::images, not glTF texture slots.
  int normalImage=-1;double normalScale=1;int metallicRoughnessImage=-1;
  int occlusionImage=-1;double occlusionStrength=1;int emissiveImage=-1;
};
struct ModelImage {std::vector<std::uint8_t> bytes;std::string mimeType;};
struct ModelNode {
  std::string name;int parent=-1,mesh=-1,skin=-1;Vec3 translation{0,0,0},scale{1,1,1};
  std::array<double,4> rotation{0,0,0,1};bool hasMatrix=false;ModelMatrix matrix{};
};
struct ModelSkin {std::vector<std::size_t> joints;std::vector<ModelMatrix> inverseBindMatrices;};
struct ModelAnimationSampler {std::vector<double> times;std::vector<std::array<double,4>> values;std::string interpolation="LINEAR";};
struct ModelAnimationChannel {std::size_t sampler=0,node=0;std::string path;};
struct ModelAnimation {std::string name;double duration=0;std::vector<ModelAnimationSampler> samplers;std::vector<ModelAnimationChannel> channels;};
struct ModelAsset {
  std::vector<ModelMesh> meshes;std::vector<ModelNode> nodes;std::vector<ModelSkin> skins;
  std::vector<ModelMaterial> materials;std::vector<ModelImage> images;std::vector<ModelAnimation> animations;
  std::vector<std::size_t> sceneRoots;std::vector<bool> sceneActive;
  Vec3 minimum{0,0,0},maximum{0,0,0};std::size_t vertices=0,triangles=0;
  std::size_t drawVertices=0,drawTriangles=0;
};
struct ModelFrame {std::vector<ModelMatrix> nodes;};
struct ModelDrawVertex {Vec3 position,normal;std::array<double,2> uv;std::array<double,4> color;};
std::shared_ptr<const ModelAsset> loadModelGlb(const std::filesystem::path&);
ModelFrame evaluateModel(const ModelAsset&,const std::string& clip,double seconds,bool loop=true);
std::vector<ModelDrawVertex> deformModelPrimitive(const ModelAsset&,const ModelFrame&,std::size_t node,const ModelPrimitive&);
} // namespace linkworld
