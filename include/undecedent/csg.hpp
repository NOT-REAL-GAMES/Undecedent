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
CsgAddResult csg_add_sector_at_floor(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& added_outer,
    float floor_height,
    float default_height = 96.0F
);
CsgAddResult csg_subtract_sector(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& cut_loop
);
CsgAddResult csg_subtract_sector_at_floor(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& cut_loop,
    float floor_height
);
CsgAddResult csg_split_sectors_by_line_at_floor(
    const std::vector<SectorPlane>& existing_sectors,
    Vec2 a,
    Vec2 b,
    float floor_height
);
CsgAddResult csg_rebuild_sectors(const std::vector<SectorPlane>& sectors);
CsgAddResult csg_merge_sectors(
    const std::vector<SectorPlane>& sectors,
    const std::vector<int>& selected_indices
);
CsgAddResult csg_delete_sectors(
    const std::vector<SectorPlane>& sectors,
    const std::vector<int>& selected_indices
);

} // namespace undecedent
