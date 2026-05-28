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
CsgAddResult csg_subtract_sector(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& cut_loop
);
CsgAddResult csg_rebuild_sectors(const std::vector<SectorPlane>& sectors);
CsgAddResult csg_merge_sectors(
    const std::vector<SectorPlane>& sectors,
    const std::vector<int>& selected_indices
);

} // namespace undecedent
