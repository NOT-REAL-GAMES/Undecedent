#pragma once

#include "undecedent/geometry.hpp"
#include "undecedent/runtime_geometry.hpp"

#include <map>
#include <utility>
#include <vector>

namespace undecedent {

struct RuntimeBounds2 {
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
};

struct RuntimeTaggedTriangle {
    RuntimeTriangle triangle;
    int sector_id = -1;
};

struct RuntimeWallSegment {
    Vec2 a;
    Vec2 b;
    int sector_id = -1;
};

struct RuntimeSector {
    PolygonLoop outer;
    std::vector<PolygonLoop> holes;
    RuntimeBounds2 bounds;
    std::vector<int> neighbors;
    float floor_height = 0.0F;
    float height = 96.0F;
};

struct RuntimeSpatialCell {
    std::vector<int> sector_ids;
    std::vector<int> wall_ids;
};

struct RuntimeWorld {
    float cell_size = 128.0F;
    std::vector<RuntimeSector> sectors;
    std::vector<RuntimeTaggedTriangle> triangles;
    std::vector<RuntimeWallSegment> walls;
    std::map<std::pair<int, int>, RuntimeSpatialCell> spatial_cells;
};

RuntimeWorld build_runtime_world(const std::vector<SectorPlane>& sectors, float cell_size = 128.0F);

int sector_at_point(const RuntimeWorld& world, Vec3 point);
std::vector<int> visible_sectors_from(const RuntimeWorld& world, int sector_id);
std::vector<int> sectors_near_point(const RuntimeWorld& world, Vec3 point);
std::vector<int> walls_near_point(const RuntimeWorld& world, Vec3 point);
std::vector<int> sectors_in_bounds(const RuntimeWorld& world, RuntimeBounds2 bounds);
std::vector<int> walls_in_bounds(const RuntimeWorld& world, RuntimeBounds2 bounds);

} // namespace undecedent
