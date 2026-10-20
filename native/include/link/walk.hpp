#pragma once
#include "core.hpp"

namespace linkworld {
// Movement follows the camera on the horizontal plane; bounds describe the eye.
Vec3 advanceWalk(const WalkSettings&,Vec3 eye,Vec3 forward,double forwardInput,double rightInput,double dt);
// Returns the earliest volume entered by the eye segment; a start inside is ignored.
const Portal* enteredPortal(const World&,Vec3 from,Vec3 to);
} // namespace linkworld
