#pragma once
#include "core.hpp"

namespace linkworld {
// Parses only the environment.lighting value. World extension opt-in, resource
// confinement and HDR decoding are enforced by the world loader.
LightingSettings parseLightingSettings(const Value&,Location={});
} // namespace linkworld
