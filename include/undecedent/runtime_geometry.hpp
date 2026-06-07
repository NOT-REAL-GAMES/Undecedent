#pragma once

#include "undecedent/geometry.hpp"

#include <vector>

namespace undecedent {

struct RuntimeTriangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

enum class RuntimeSurfaceKind {
    Floor,
    Ceiling,
    Wall,
    HoleWall,
};

struct RuntimeSurfaceRef {
    RuntimeSurfaceKind kind = RuntimeSurfaceKind::Floor;
    int index = -1;
    int sub_index = -1;
};

struct RuntimeGeometry {
    std::vector<RuntimeTriangle> triangles;
    std::vector<int> material_ids;
    std::vector<RuntimeSurfaceRef> surfaces;
    std::vector<Vec2> uv_a;
    std::vector<Vec2> uv_b;
    std::vector<Vec2> uv_c;
};

RuntimeGeometry build_runtime_geometry(const std::vector<SectorPlane>& sectors);

} // namespace undecedent
