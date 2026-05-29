#pragma once

#include "undecedent/triangulator.hpp"

#include <cmath>

namespace undecedent {

inline bool same_floor_height(const float a, const float b) {
    return std::abs(a - b) <= kGeometryEpsilon;
}

inline bool sector_intersects_z_slice(const SectorPlane& sector, const float slice_z) {
    return slice_z >= sector.floor_height - kGeometryEpsilon &&
        slice_z <= sector.floor_height + sector.height + kGeometryEpsilon;
}

inline bool sector_floor_matches_slice(const SectorPlane& sector, const float slice_z) {
    return same_floor_height(sector.floor_height, slice_z);
}

} // namespace undecedent
