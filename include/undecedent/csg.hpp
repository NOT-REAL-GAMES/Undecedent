#pragma once

#include "undecedent/geometry.hpp"

namespace undecedent {

struct SectorEdge {
    int start = 0;
    int end = 0;
    int neighbor = -1;
};

struct CsgAddResult {
    bool ok = false;
    std::string message;
    std::vector<SectorPlane> sectors;
};

CsgAddResult csg_add_sector(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& added_outer
);

} // namespace undecedent

