#pragma once
#include <filesystem>
#include <vector>

namespace linkworld {
// Linear RGB radiance, row 0 is +Y/north. Equirectangular UV convention:
// u=atan2(direction.z,direction.x)/(2*pi)+.5, v=acos(direction.y)/pi.
struct EnvironmentImage {
  unsigned width=0,height=0;
  std::vector<float> rgb;
};
struct EnvironmentLighting {
  EnvironmentImage radiance,irradiance;
  std::vector<EnvironmentImage> specularMips;
};
// Bounded, self-contained Radiance RGBE .hdr import: encoded file <=8 MiB,
// axes <=2048, source pixels <=2097152, finite linear RGB in [0,1000000].
// Diffuse irradiance stores
// the cosine-weighted radiance average (integral/pi); specular GGX levels are
// 256x128 through 1x1, with perceptual roughness=level/(levelCount-1).
EnvironmentLighting loadEnvironmentLighting(const std::filesystem::path&);
} // namespace linkworld
