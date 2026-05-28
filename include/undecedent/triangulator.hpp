#pragma once

#include "undecedent/geometry.hpp"

namespace undecedent {

constexpr float kGeometryEpsilon = 0.001F;

TriangulationResult triangulate_polygon(
    const PolygonLoop& outer,
    const std::vector<PolygonLoop>& holes = {}
);

const char* triangulation_status_name(TriangulationStatus status);

} // namespace undecedent

