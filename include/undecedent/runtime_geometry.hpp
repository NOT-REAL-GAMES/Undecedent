#pragma once

#include "undecedent/geometry.hpp"

#include <vector>

namespace undecedent {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct RuntimeTriangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct RuntimeGeometry {
    std::vector<RuntimeTriangle> triangles;
};

RuntimeGeometry build_runtime_geometry(const std::vector<SectorPlane>& sectors);

} // namespace undecedent
