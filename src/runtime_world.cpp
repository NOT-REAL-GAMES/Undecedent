#include "undecedent/runtime_world.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace undecedent {
namespace {

Vec3 floor_vertex(const Vec2 point, const float floor_height) {
    return Vec3{point.x, floor_height, point.y};
}

Vec3 ceiling_vertex(const Vec2 point, const float floor_height, const float height) {
    return Vec3{point.x, floor_height + height, point.y};
}

RuntimeBounds2 bounds_for_loop(const PolygonLoop& loop) {
    RuntimeBounds2 bounds{};
    if (loop.vertices.empty()) {
        return bounds;
    }

    bounds.min_x = bounds.max_x = loop.vertices.front().x;
    bounds.min_y = bounds.max_y = loop.vertices.front().y;
    for (const Vec2 point : loop.vertices) {
        bounds.min_x = std::min(bounds.min_x, point.x);
        bounds.min_y = std::min(bounds.min_y, point.y);
        bounds.max_x = std::max(bounds.max_x, point.x);
        bounds.max_y = std::max(bounds.max_y, point.y);
    }
    return bounds;
}

RuntimeBounds2 bounds_for_segment(const Vec2 a, const Vec2 b) {
    return RuntimeBounds2{
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::max(a.x, b.x),
        std::max(a.y, b.y),
    };
}

int cell_coord(const float coordinate, const float cell_size) {
    return static_cast<int>(std::floor(coordinate / cell_size));
}

void insert_unique(std::vector<int>& values, const int value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void insert_sector_cells(RuntimeWorld& world, const int sector_id, const RuntimeBounds2 bounds) {
    const int min_x = cell_coord(bounds.min_x, world.cell_size);
    const int max_x = cell_coord(bounds.max_x, world.cell_size);
    const int min_y = cell_coord(bounds.min_y, world.cell_size);
    const int max_y = cell_coord(bounds.max_y, world.cell_size);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            insert_unique(world.spatial_cells[{x, y}].sector_ids, sector_id);
        }
    }
}

void insert_wall_cells(RuntimeWorld& world, const int wall_id, const RuntimeBounds2 bounds) {
    const int min_x = cell_coord(bounds.min_x, world.cell_size);
    const int max_x = cell_coord(bounds.max_x, world.cell_size);
    const int min_y = cell_coord(bounds.min_y, world.cell_size);
    const int max_y = cell_coord(bounds.max_y, world.cell_size);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            insert_unique(world.spatial_cells[{x, y}].wall_ids, wall_id);
        }
    }
}

bool point_on_segment(const Vec2 a, const Vec2 b, const Vec2 point) {
    constexpr float epsilon = 0.001F;
    const float cross = ((b.x - a.x) * (point.y - a.y)) - ((b.y - a.y) * (point.x - a.x));
    if (std::abs(cross) > epsilon) {
        return false;
    }

    return point.x >= std::min(a.x, b.x) - epsilon &&
        point.x <= std::max(a.x, b.x) + epsilon &&
        point.y >= std::min(a.y, b.y) - epsilon &&
        point.y <= std::max(a.y, b.y) + epsilon;
}

bool point_in_loop_or_on(const PolygonLoop& loop, const Vec2 point) {
    if (loop.vertices.size() < 3) {
        return false;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        if (point_on_segment(loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()], point)) {
            return true;
        }
    }

    bool inside = false;
    for (std::size_t i = 0, j = loop.vertices.size() - 1; i < loop.vertices.size(); j = i++) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[j];
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
            (point.x < ((b.x - a.x) * (point.y - a.y) / (b.y - a.y)) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_sector(const RuntimeSector& sector, const Vec2 point) {
    if (point.x < sector.bounds.min_x || point.x > sector.bounds.max_x ||
        point.y < sector.bounds.min_y || point.y > sector.bounds.max_y) {
        return false;
    }

    if (!point_in_loop_or_on(sector.outer, point)) {
        return false;
    }

    return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
        return point_in_loop_or_on(hole, point);
    });
}

bool point_in_sector_volume(const RuntimeSector& sector, const Vec3 point) {
    if (point.y < sector.floor_height || point.y > sector.floor_height + sector.height) {
        return false;
    }
    return point_in_sector(sector, Vec2{point.x, point.z});
}

void add_wall(
    RuntimeWorld& world,
    const int sector_id,
    const Vec2 a,
    const Vec2 b,
    const float floor_height,
    const float height
) {
    const int wall_id = static_cast<int>(world.walls.size());
    world.walls.push_back(RuntimeWallSegment{a, b, sector_id});
    insert_wall_cells(world, wall_id, bounds_for_segment(a, b));

    const Vec3 bottom_a = floor_vertex(a, floor_height);
    const Vec3 bottom_b = floor_vertex(b, floor_height);
    const Vec3 top_a = ceiling_vertex(a, floor_height, height);
    const Vec3 top_b = ceiling_vertex(b, floor_height, height);
    world.triangles.push_back(RuntimeTaggedTriangle{RuntimeTriangle{bottom_a, top_a, top_b}, sector_id});
    world.triangles.push_back(RuntimeTaggedTriangle{RuntimeTriangle{bottom_a, top_b, bottom_b}, sector_id});
}

std::vector<int> unique_sorted(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<int> ids_in_bounds(const RuntimeWorld& world, const RuntimeBounds2 bounds, const bool walls) {
    std::vector<int> ids;
    const int min_x = cell_coord(bounds.min_x, world.cell_size);
    const int max_x = cell_coord(bounds.max_x, world.cell_size);
    const int min_y = cell_coord(bounds.min_y, world.cell_size);
    const int max_y = cell_coord(bounds.max_y, world.cell_size);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const auto found = world.spatial_cells.find({x, y});
            if (found == world.spatial_cells.end()) {
                continue;
            }

            const std::vector<int>& source = walls ? found->second.wall_ids : found->second.sector_ids;
            ids.insert(ids.end(), source.begin(), source.end());
        }
    }
    return unique_sorted(std::move(ids));
}

} // namespace

RuntimeWorld build_runtime_world(const std::vector<SectorPlane>& sectors, const float cell_size) {
    RuntimeWorld world;
    world.cell_size = std::max(cell_size, 1.0F);
    world.sectors.reserve(sectors.size());

    for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
        const SectorPlane& source = sectors[sector_index];
        RuntimeSector runtime_sector;
        runtime_sector.outer = source.outer;
        runtime_sector.holes = source.holes;
        runtime_sector.bounds = bounds_for_loop(source.outer);
        runtime_sector.floor_height = source.floor_height;
        runtime_sector.height = source.height;

        for (const int neighbor : source.edge_neighbors) {
            if (neighbor >= 0) {
                insert_unique(runtime_sector.neighbors, neighbor);
            }
        }

        world.sectors.push_back(std::move(runtime_sector));
        insert_sector_cells(world, static_cast<int>(sector_index), world.sectors.back().bounds);

        for (const Triangle& triangle : source.triangles) {
            world.triangles.push_back(RuntimeTaggedTriangle{
                RuntimeTriangle{
                    floor_vertex(triangle.a, source.floor_height),
                    floor_vertex(triangle.b, source.floor_height),
                    floor_vertex(triangle.c, source.floor_height),
                },
                static_cast<int>(sector_index),
            });
            world.triangles.push_back(RuntimeTaggedTriangle{
                RuntimeTriangle{
                    ceiling_vertex(triangle.c, source.floor_height, source.height),
                    ceiling_vertex(triangle.b, source.floor_height, source.height),
                    ceiling_vertex(triangle.a, source.floor_height, source.height),
                },
                static_cast<int>(sector_index),
            });
        }

        for (std::size_t edge_index = 0; edge_index < source.outer.vertices.size(); ++edge_index) {
            const int neighbor = edge_index < source.edge_neighbors.size() ? source.edge_neighbors[edge_index] : -1;
            if (neighbor >= 0) {
                continue;
            }

            add_wall(
                world,
                static_cast<int>(sector_index),
                source.outer.vertices[edge_index],
                source.outer.vertices[(edge_index + 1) % source.outer.vertices.size()],
                source.floor_height,
                source.height
            );
        }

        for (const PolygonLoop& hole : source.holes) {
            for (std::size_t i = 0; i < hole.vertices.size(); ++i) {
                add_wall(
                    world,
                    static_cast<int>(sector_index),
                    hole.vertices[i],
                    hole.vertices[(i + 1) % hole.vertices.size()],
                    source.floor_height,
                    source.height
                );
            }
        }
    }

    return world;
}

int sector_at_point(const RuntimeWorld& world, const Vec3 point) {
    const Vec2 point_2d{point.x, point.z};
    const auto found = world.spatial_cells.find({cell_coord(point_2d.x, world.cell_size), cell_coord(point_2d.y, world.cell_size)});
    if (found != world.spatial_cells.end()) {
        for (const int sector_id : found->second.sector_ids) {
            if (sector_id >= 0 && sector_id < static_cast<int>(world.sectors.size()) &&
                point_in_sector_volume(world.sectors[static_cast<std::size_t>(sector_id)], point)) {
                return sector_id;
            }
        }
    }

    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        if (point_in_sector_volume(world.sectors[sector_id], point)) {
            return static_cast<int>(sector_id);
        }
    }
    return -1;
}

std::vector<int> visible_sectors_from(const RuntimeWorld& world, const int sector_id) {
    if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
        return {};
    }

    std::set<int> visited;
    std::vector<int> stack{sector_id};
    visited.insert(sector_id);
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();

        for (const int neighbor : world.sectors[static_cast<std::size_t>(current)].neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<int>(world.sectors.size()) || visited.contains(neighbor)) {
                continue;
            }
            visited.insert(neighbor);
            stack.push_back(neighbor);
        }
    }

    return std::vector<int>(visited.begin(), visited.end());
}

std::vector<int> sectors_near_point(const RuntimeWorld& world, const Vec3 point) {
    return sectors_in_bounds(world, RuntimeBounds2{point.x, point.z, point.x, point.z});
}

std::vector<int> walls_near_point(const RuntimeWorld& world, const Vec3 point) {
    return walls_in_bounds(world, RuntimeBounds2{point.x, point.z, point.x, point.z});
}

std::vector<int> sectors_in_bounds(const RuntimeWorld& world, const RuntimeBounds2 bounds) {
    return ids_in_bounds(world, bounds, false);
}

std::vector<int> walls_in_bounds(const RuntimeWorld& world, const RuntimeBounds2 bounds) {
    return ids_in_bounds(world, bounds, true);
}

} // namespace undecedent
