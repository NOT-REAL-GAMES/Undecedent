#include "undecedent/runtime_world.hpp"

#include "undecedent/displacement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace undecedent {
namespace {

constexpr long long kMaxSpatialCellsPerPrimitive = 4096;
constexpr long long kMaxSpatialQueryCells = 4096;

void normalize_materials(SectorPlane& sector) {
    sector.floor_material = clamped_material_id(sector.floor_material);
    sector.ceiling_material = clamped_material_id(sector.ceiling_material);
    sector.wall_materials.resize(sector.outer.vertices.size(), kDefaultMaterialId);
    for (int& material : sector.wall_materials) {
        material = clamped_material_id(material);
    }
    sector.hole_wall_materials.resize(sector.holes.size());
    for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
        sector.hole_wall_materials[hole_index].resize(sector.holes[hole_index].vertices.size(), kDefaultMaterialId);
        for (int& material : sector.hole_wall_materials[hole_index]) {
            material = clamped_material_id(material);
        }
    }
}

Vec3 floor_vertex(const Vec2 point, const float y) {
    return Vec3{point.x, y, point.y};
}

float lerp(const float a, const float b, const float t) {
    return a + ((b - a) * t);
}

Vec2 lerp(const Vec2 a, const Vec2 b, const float t) {
    return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

int surface_segment_count(const SectorPlane& sector) {
    int count = 1;
    if (sector.floor_displacement.enabled) {
        count = std::max(count, clamped_displacement_resolution(sector.floor_displacement.resolution));
    }
    if (sector.ceiling_displacement.enabled) {
        count = std::max(count, clamped_displacement_resolution(sector.ceiling_displacement.resolution));
    }
    return count;
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

long long cell_span_count(const int min_x, const int max_x, const int min_y, const int max_y) {
    if (max_x < min_x || max_y < min_y) {
        return 0;
    }
    const long long width = static_cast<long long>(max_x) - static_cast<long long>(min_x) + 1LL;
    const long long height = static_cast<long long>(max_y) - static_cast<long long>(min_y) + 1LL;
    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (width > (std::numeric_limits<long long>::max() / height)) {
        return std::numeric_limits<long long>::max();
    }
    return width * height;
}

bool bounds_cell_range(
    const RuntimeWorld& world,
    const RuntimeBounds2 bounds,
    int& min_x,
    int& max_x,
    int& min_y,
    int& max_y,
    long long& cell_count
) {
    min_x = cell_coord(bounds.min_x, world.cell_size);
    max_x = cell_coord(bounds.max_x, world.cell_size);
    min_y = cell_coord(bounds.min_y, world.cell_size);
    max_y = cell_coord(bounds.max_y, world.cell_size);
    cell_count = cell_span_count(min_x, max_x, min_y, max_y);
    return cell_count > 0;
}

void insert_sector_cells(RuntimeWorld& world, const int sector_id, const RuntimeBounds2 bounds) {
    const int min_x = cell_coord(bounds.min_x, world.cell_size);
    const int max_x = cell_coord(bounds.max_x, world.cell_size);
    const int min_y = cell_coord(bounds.min_y, world.cell_size);
    const int max_y = cell_coord(bounds.max_y, world.cell_size);
    if (cell_span_count(min_x, max_x, min_y, max_y) > kMaxSpatialCellsPerPrimitive) {
        insert_unique(world.unindexed_sector_ids, sector_id);
        return;
    }

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
    if (cell_span_count(min_x, max_x, min_y, max_y) > kMaxSpatialCellsPerPrimitive) {
        insert_unique(world.unindexed_wall_ids, wall_id);
        return;
    }

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

bool point_on_loop(const PolygonLoop& loop, const Vec2 point) {
    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        if (point_on_segment(loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()], point)) {
            return true;
        }
    }
    return false;
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

bool point_in_sector_strict(const RuntimeSector& sector, const Vec2 point) {
    if (point.x <= sector.bounds.min_x + 0.001F || point.x >= sector.bounds.max_x - 0.001F ||
        point.y <= sector.bounds.min_y + 0.001F || point.y >= sector.bounds.max_y - 0.001F) {
        return false;
    }
    if (point_on_loop(sector.outer, point) || !point_in_loop_or_on(sector.outer, point)) {
        return false;
    }
    return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
        return point_in_loop_or_on(hole, point);
    });
}

bool point_in_sector_volume(const RuntimeSector& sector, const Vec3 point) {
    if (point.y < sector.min_floor_height - 0.001F || point.y > sector.max_ceiling_height + 0.001F) {
        return false;
    }
    const Vec2 point_2d{point.x, point.z};
    if (!point_in_sector(sector, point_2d)) {
        return false;
    }
    return point.y >= runtime_floor_height_at(sector, point_2d) - 0.001F &&
        point.y <= runtime_ceiling_height_at(sector, point_2d) + 0.001F;
}

bool vertical_ranges_overlap(const float floor_a, const float height_a, const float floor_b, const float height_b) {
    const float lower = std::max(floor_a, floor_b);
    const float upper = std::min(floor_a + height_a, floor_b + height_b);
    return upper > lower + 0.001F;
}

bool vertical_ranges_overlap(const RuntimeSector& a, const RuntimeSector& b) {
    const float lower = std::max(a.min_floor_height, b.min_floor_height);
    const float upper = std::min(a.max_ceiling_height, b.max_ceiling_height);
    return upper > lower + 0.001F;
}

bool bounds_overlap(const RuntimeBounds2 a, const RuntimeBounds2 b) {
    return a.max_x >= b.min_x - 0.001F &&
        b.max_x >= a.min_x - 0.001F &&
        a.max_y >= b.min_y - 0.001F &&
        b.max_y >= a.min_y - 0.001F;
}

float loop_area_abs(const PolygonLoop& loop) {
    if (loop.vertices.size() < 3) {
        return 0.0F;
    }

    float area = 0.0F;
    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[(i + 1) % loop.vertices.size()];
        area += (a.x * b.y) - (b.x * a.y);
    }
    return std::abs(area) * 0.5F;
}

float sector_area(const RuntimeSector& sector) {
    float area = loop_area_abs(sector.outer);
    for (const PolygonLoop& hole : sector.holes) {
        area -= loop_area_abs(hole);
    }
    return std::max(area, 0.0F);
}

bool sectors_overlap_for_visibility(const RuntimeSector& a, const RuntimeSector& b) {
    if (!bounds_overlap(a.bounds, b.bounds) || !vertical_ranges_overlap(a, b)) {
        return false;
    }

    if (!a.outer.vertices.empty() && point_in_sector_strict(b, a.outer.vertices.front())) {
        return true;
    }
    if (!b.outer.vertices.empty() && point_in_sector_strict(a, b.outer.vertices.front())) {
        return true;
    }
    return false;
}

void add_portal_record(
    RuntimeWorld& world,
    const int from_sector,
    const int to_sector,
    const Vec2 a,
    const Vec2 b,
    const float bottom,
    const float top
) {
    if (from_sector < 0 || from_sector >= static_cast<int>(world.sectors.size()) || top <= bottom + 0.001F) {
        return;
    }

    const int portal_id = static_cast<int>(world.portals.size());
    world.portals.push_back(RuntimePortal{a, b, from_sector, to_sector, bottom, top});
    world.sectors[static_cast<std::size_t>(from_sector)].portal_ids.push_back(portal_id);
}

void add_triangle(
    RuntimeWorld& world,
    const int sector_id,
    const RuntimeTriangle triangle,
    const Vec2 uv_a,
    const Vec2 uv_b,
    const Vec2 uv_c,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    world.triangles.push_back(RuntimeTaggedTriangle{
        triangle,
        uv_a,
        uv_b,
        uv_c,
        sector_id,
        clamped_material_id(material_id),
        surface
    });
}

void add_wall_record(RuntimeWorld& world, const int sector_id, const Vec2 a, const Vec2 b) {
    const int wall_id = static_cast<int>(world.walls.size());
    world.walls.push_back(RuntimeWallSegment{a, b, sector_id});
    insert_wall_cells(world, wall_id, bounds_for_segment(a, b));
}

void add_wall_span(
    RuntimeWorld& world,
    const int sector_id,
    const Vec2 a,
    const Vec2 b,
    const float bottom_a,
    const float bottom_b,
    const float top_a,
    const float top_b,
    const float u_a,
    const float u_b,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    if (top_a <= bottom_a + 0.001F && top_b <= bottom_b + 0.001F) {
        return;
    }
    add_wall_record(world, sector_id, a, b);

    const Vec3 bottom_va = floor_vertex(a, bottom_a);
    const Vec3 bottom_vb = floor_vertex(b, bottom_b);
    const Vec3 top_va = floor_vertex(a, top_a);
    const Vec3 top_vb = floor_vertex(b, top_b);
    add_triangle(
        world,
        sector_id,
        RuntimeTriangle{bottom_va, top_va, top_vb},
        Vec2{u_a, bottom_a},
        Vec2{u_a, top_a},
        Vec2{u_b, top_b},
        material_id,
        surface
    );
    add_triangle(
        world,
        sector_id,
        RuntimeTriangle{bottom_va, top_vb, bottom_vb},
        Vec2{u_a, bottom_a},
        Vec2{u_b, top_b},
        Vec2{u_b, bottom_b},
        material_id,
        surface
    );
}

void add_wall(
    RuntimeWorld& world,
    const int sector_id,
    const SectorPlane& sector,
    const Vec2 a,
    const Vec2 b,
    const float edge_u_start,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    const int segments = surface_segment_count(sector);
    const float edge_length = std::hypot(b.x - a.x, b.y - a.y);
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec2 p0 = lerp(a, b, t0);
        const Vec2 p1 = lerp(a, b, t1);
        add_wall_span(
            world,
            sector_id,
            p0,
            p1,
            sample_surface_height(sector, SectorSurfaceKind::Floor, p0),
            sample_surface_height(sector, SectorSurfaceKind::Floor, p1),
            sample_surface_height(sector, SectorSurfaceKind::Ceiling, p0),
            sample_surface_height(sector, SectorSurfaceKind::Ceiling, p1),
            edge_u_start + (edge_length * t0),
            edge_u_start + (edge_length * t1),
            material_id,
            surface
        );
    }
}

void add_neighbor_gap_walls(
    RuntimeWorld& world,
    const int sector_id,
    const SectorPlane& sector,
    const SectorPlane& neighbor,
    const Vec2 a,
    const Vec2 b,
    const float edge_u_start,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    const int segments = std::max(surface_segment_count(sector), surface_segment_count(neighbor));
    const float edge_length = std::hypot(b.x - a.x, b.y - a.y);
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec2 p0 = lerp(a, b, t0);
        const Vec2 p1 = lerp(a, b, t1);

        const float sector_floor_0 = sample_surface_height(sector, SectorSurfaceKind::Floor, p0);
        const float sector_floor_1 = sample_surface_height(sector, SectorSurfaceKind::Floor, p1);
        const float sector_ceiling_0 = sample_surface_height(sector, SectorSurfaceKind::Ceiling, p0);
        const float sector_ceiling_1 = sample_surface_height(sector, SectorSurfaceKind::Ceiling, p1);
        const float neighbor_floor_0 = sample_surface_height(neighbor, SectorSurfaceKind::Floor, p0);
        const float neighbor_floor_1 = sample_surface_height(neighbor, SectorSurfaceKind::Floor, p1);
        const float neighbor_ceiling_0 = sample_surface_height(neighbor, SectorSurfaceKind::Ceiling, p0);
        const float neighbor_ceiling_1 = sample_surface_height(neighbor, SectorSurfaceKind::Ceiling, p1);
        const float overlap_floor_0 = std::max(sector_floor_0, neighbor_floor_0);
        const float overlap_floor_1 = std::max(sector_floor_1, neighbor_floor_1);
        const float overlap_ceiling_0 = std::min(sector_ceiling_0, neighbor_ceiling_0);
        const float overlap_ceiling_1 = std::min(sector_ceiling_1, neighbor_ceiling_1);
        const float u0 = edge_u_start + (edge_length * t0);
        const float u1 = edge_u_start + (edge_length * t1);

        if (overlap_ceiling_0 <= overlap_floor_0 + 0.001F &&
            overlap_ceiling_1 <= overlap_floor_1 + 0.001F) {
            add_wall_span(
                world,
                sector_id,
                p0,
                p1,
                sector_floor_0,
                sector_floor_1,
                sector_ceiling_0,
                sector_ceiling_1,
                u0,
                u1,
                material_id,
                surface
            );
            continue;
        }

        add_wall_span(
            world,
            sector_id,
            p0,
            p1,
            sector_floor_0,
            sector_floor_1,
            overlap_floor_0,
            overlap_floor_1,
            u0,
            u1,
            material_id,
            surface
        );
        add_wall_span(
            world,
            sector_id,
            p0,
            p1,
            overlap_ceiling_0,
            overlap_ceiling_1,
            sector_ceiling_0,
            sector_ceiling_1,
            u0,
            u1,
            material_id,
            surface
        );
    }
}

float loop_u_start(const PolygonLoop& loop, const std::size_t edge_index) {
    float u = 0.0F;
    const std::size_t count = loop.vertices.size();
    for (std::size_t i = 0; i < edge_index && i < count; ++i) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[(i + 1) % count];
        u += std::hypot(b.x - a.x, b.y - a.y);
    }
    return u;
}

std::vector<int> unique_sorted(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<int> ids_in_bounds(const RuntimeWorld& world, const RuntimeBounds2 bounds, const bool walls) {
    std::vector<int> ids;
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    long long cell_count = 0;
    if (!bounds_cell_range(world, bounds, min_x, max_x, min_y, max_y, cell_count)) {
        return {};
    }

    if (cell_count > kMaxSpatialQueryCells) {
        ids.reserve(walls ? world.walls.size() : world.sectors.size());
        const std::size_t count = walls ? world.walls.size() : world.sectors.size();
        for (std::size_t id = 0; id < count; ++id) {
            ids.push_back(static_cast<int>(id));
        }
        return ids;
    }

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
    const std::vector<int>& unindexed = walls ? world.unindexed_wall_ids : world.unindexed_sector_ids;
    ids.insert(ids.end(), unindexed.begin(), unindexed.end());
    return unique_sorted(std::move(ids));
}

RuntimeHeightTriangle to_runtime_height_triangle(const SurfaceSampleTriangle& triangle) {
    return RuntimeHeightTriangle{
        triangle.a.position,
        triangle.b.position,
        triangle.c.position,
        triangle.a.height,
        triangle.b.height,
        triangle.c.height,
    };
}

} // namespace

RuntimeWorld build_runtime_world(const std::vector<SectorPlane>& sectors, const float cell_size) {
    RuntimeWorld world;
    world.cell_size = std::max(cell_size, 1.0F);
    world.sectors.reserve(sectors.size());

    for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
        SectorPlane source = sectors[sector_index];
        normalize_materials(source);
        RuntimeSector runtime_sector;
        runtime_sector.source_sector_id = source.id;
        runtime_sector.outer = source.outer;
        runtime_sector.holes = source.holes;
        runtime_sector.bounds = bounds_for_loop(source.outer);
        runtime_sector.floor_height = source.floor_height;
        runtime_sector.height = source.height;
        const SurfaceHeightRange floor_range = sector_surface_height_range(source, SectorSurfaceKind::Floor);
        const SurfaceHeightRange ceiling_range = sector_surface_height_range(source, SectorSurfaceKind::Ceiling);
        runtime_sector.min_floor_height = floor_range.min_height;
        runtime_sector.max_floor_height = floor_range.max_height;
        runtime_sector.min_ceiling_height = ceiling_range.min_height;
        runtime_sector.max_ceiling_height = ceiling_range.max_height;

        const std::vector<SurfaceSampleTriangle> floor_triangles =
            build_surface_sample_triangles(source, SectorSurfaceKind::Floor);
        const std::vector<SurfaceSampleTriangle> ceiling_triangles =
            build_surface_sample_triangles(source, SectorSurfaceKind::Ceiling);
        runtime_sector.floor_triangles.reserve(floor_triangles.size());
        runtime_sector.ceiling_triangles.reserve(ceiling_triangles.size());
        for (const SurfaceSampleTriangle& triangle : floor_triangles) {
            runtime_sector.floor_triangles.push_back(to_runtime_height_triangle(triangle));
        }
        for (const SurfaceSampleTriangle& triangle : ceiling_triangles) {
            runtime_sector.ceiling_triangles.push_back(to_runtime_height_triangle(triangle));
        }

        for (const int neighbor : source.edge_neighbors) {
            if (neighbor >= 0 && neighbor < static_cast<int>(sectors.size())) {
                const SurfaceHeightRange neighbor_floor =
                    sector_surface_height_range(sectors[static_cast<std::size_t>(neighbor)], SectorSurfaceKind::Floor);
                const SurfaceHeightRange neighbor_ceiling =
                    sector_surface_height_range(sectors[static_cast<std::size_t>(neighbor)], SectorSurfaceKind::Ceiling);
                const bool overlaps =
                    std::min(runtime_sector.max_ceiling_height, neighbor_ceiling.max_height) >
                    std::max(runtime_sector.min_floor_height, neighbor_floor.min_height) + 0.001F;
                if (!overlaps) {
                    continue;
                }
                insert_unique(runtime_sector.neighbors, neighbor);
            }
        }

        world.sectors.push_back(std::move(runtime_sector));
        insert_sector_cells(world, static_cast<int>(sector_index), world.sectors.back().bounds);

        for (const SurfaceSampleTriangle& triangle : floor_triangles) {
            add_triangle(
                world,
                static_cast<int>(sector_index),
                RuntimeTriangle{
                    floor_vertex(triangle.a.position, triangle.a.height),
                    floor_vertex(triangle.b.position, triangle.b.height),
                    floor_vertex(triangle.c.position, triangle.c.height),
                },
                triangle.a.position,
                triangle.b.position,
                triangle.c.position,
                source.floor_material,
                RuntimeSurfaceRef{RuntimeSurfaceKind::Floor, -1, -1}
            );
        }
        for (const SurfaceSampleTriangle& triangle : ceiling_triangles) {
            add_triangle(
                world,
                static_cast<int>(sector_index),
                RuntimeTriangle{
                    floor_vertex(triangle.c.position, triangle.c.height),
                    floor_vertex(triangle.b.position, triangle.b.height),
                    floor_vertex(triangle.a.position, triangle.a.height),
                },
                triangle.c.position,
                triangle.b.position,
                triangle.a.position,
                source.ceiling_material,
                RuntimeSurfaceRef{RuntimeSurfaceKind::Ceiling, -1, -1}
            );
        }

        for (std::size_t edge_index = 0; edge_index < source.outer.vertices.size(); ++edge_index) {
            const int neighbor = edge_index < source.edge_neighbors.size() ? source.edge_neighbors[edge_index] : -1;
            const Vec2 a = source.outer.vertices[edge_index];
            const Vec2 b = source.outer.vertices[(edge_index + 1) % source.outer.vertices.size()];
            const int material_id = edge_index < source.wall_materials.size()
                ? source.wall_materials[edge_index]
                : kDefaultMaterialId;
            const RuntimeSurfaceRef surface{RuntimeSurfaceKind::Wall, static_cast<int>(edge_index), -1};
            const float edge_u_start = loop_u_start(source.outer, edge_index);
            if (neighbor >= 0 && neighbor < static_cast<int>(sectors.size())) {
                const SectorPlane& neighbor_sector = sectors[static_cast<std::size_t>(neighbor)];
                const int portal_segments = std::max(surface_segment_count(source), surface_segment_count(neighbor_sector));
                for (int segment = 0; segment < portal_segments; ++segment) {
                    const float t0 = static_cast<float>(segment) / static_cast<float>(portal_segments);
                    const float t1 = static_cast<float>(segment + 1) / static_cast<float>(portal_segments);
                    const Vec2 p0 = lerp(a, b, t0);
                    const Vec2 p1 = lerp(a, b, t1);
                    const float portal_bottom = std::max({
                        sample_surface_height(source, SectorSurfaceKind::Floor, p0),
                        sample_surface_height(source, SectorSurfaceKind::Floor, p1),
                        sample_surface_height(neighbor_sector, SectorSurfaceKind::Floor, p0),
                        sample_surface_height(neighbor_sector, SectorSurfaceKind::Floor, p1),
                    });
                    const float portal_top = std::min({
                        sample_surface_height(source, SectorSurfaceKind::Ceiling, p0),
                        sample_surface_height(source, SectorSurfaceKind::Ceiling, p1),
                        sample_surface_height(neighbor_sector, SectorSurfaceKind::Ceiling, p0),
                        sample_surface_height(neighbor_sector, SectorSurfaceKind::Ceiling, p1),
                    });
                    add_portal_record(
                        world,
                        static_cast<int>(sector_index),
                        neighbor,
                        p0,
                        p1,
                        portal_bottom,
                        portal_top
                    );
                }
                add_neighbor_gap_walls(
                    world,
                    static_cast<int>(sector_index),
                    source,
                    neighbor_sector,
                    a,
                    b,
                    edge_u_start,
                    material_id,
                    surface
                );
            } else {
                add_wall(world, static_cast<int>(sector_index), source, a, b, edge_u_start, material_id, surface);
            }
        }

        for (std::size_t hole_index = 0; hole_index < source.holes.size(); ++hole_index) {
            const PolygonLoop& hole = source.holes[hole_index];
            for (std::size_t i = 0; i < hole.vertices.size(); ++i) {
                const int material_id = hole_index < source.hole_wall_materials.size() &&
                        i < source.hole_wall_materials[hole_index].size()
                    ? source.hole_wall_materials[hole_index][i]
                    : kDefaultMaterialId;
                add_wall(
                    world,
                    static_cast<int>(sector_index),
                    source,
                    hole.vertices[i],
                    hole.vertices[(i + 1) % hole.vertices.size()],
                    loop_u_start(hole, i),
                    material_id,
                    RuntimeSurfaceRef{RuntimeSurfaceKind::HoleWall, static_cast<int>(hole_index), static_cast<int>(i)}
                );
            }
        }
    }

    for (std::size_t a = 0; a < world.sectors.size(); ++a) {
        for (std::size_t b = a + 1; b < world.sectors.size(); ++b) {
            if (!sectors_overlap_for_visibility(world.sectors[a], world.sectors[b])) {
                continue;
            }
            insert_unique(world.sectors[a].overlap_visibility_ids, static_cast<int>(b));
            insert_unique(world.sectors[b].overlap_visibility_ids, static_cast<int>(a));
        }
    }

    return world;
}

float sample_runtime_height(
    const std::vector<RuntimeHeightTriangle>& triangles,
    const Vec2 point,
    const float fallback
) {
    for (const RuntimeHeightTriangle& triangle : triangles) {
        const Vec2 v0{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y};
        const Vec2 v1{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y};
        const Vec2 v2{point.x - triangle.a.x, point.y - triangle.a.y};
        const float denom = (v0.x * v1.y) - (v0.y * v1.x);
        if (std::abs(denom) <= 0.001F) {
            continue;
        }
        const float wb = ((v2.x * v1.y) - (v2.y * v1.x)) / denom;
        const float wc = ((v0.x * v2.y) - (v0.y * v2.x)) / denom;
        const float wa = 1.0F - wb - wc;
        if (wa >= -0.001F && wb >= -0.001F && wc >= -0.001F &&
            wa <= 1.001F && wb <= 1.001F && wc <= 1.001F) {
            return (wa * triangle.ay) + (wb * triangle.by) + (wc * triangle.cy);
        }
    }
    return fallback;
}

float runtime_floor_height_at(const RuntimeSector& sector, const Vec2 point) {
    return sample_runtime_height(sector.floor_triangles, point, sector.floor_height);
}

float runtime_ceiling_height_at(const RuntimeSector& sector, const Vec2 point) {
    return sample_runtime_height(sector.ceiling_triangles, point, sector.floor_height + sector.height);
}

int sector_at_point(const RuntimeWorld& world, const Vec3 point) {
    const Vec2 point_2d{point.x, point.z};
    int best_sector = -1;
    float best_area = 0.0F;
    const auto consider = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (!point_in_sector_volume(sector, point)) {
            return;
        }
        const float area = sector_area(sector);
        if (best_sector < 0 || area < best_area) {
            best_sector = sector_id;
            best_area = area;
        }
    };

    const auto found = world.spatial_cells.find({cell_coord(point_2d.x, world.cell_size), cell_coord(point_2d.y, world.cell_size)});
    if (found != world.spatial_cells.end()) {
        for (const int sector_id : found->second.sector_ids) {
            consider(sector_id);
        }
    }

    for (const int sector_id : world.unindexed_sector_ids) {
        consider(sector_id);
    }
    if (best_sector >= 0) {
        return best_sector;
    }

    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        consider(static_cast<int>(sector_id));
    }
    return best_sector;
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
        for (const int neighbor : world.sectors[static_cast<std::size_t>(current)].overlap_visibility_ids) {
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
