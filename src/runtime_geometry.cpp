#include "undecedent/runtime_geometry.hpp"

#include <algorithm>
#include <cstddef>

namespace undecedent {
namespace {

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

Vec3 floor_vertex(const Vec2 point, const float floor_height) {
    return Vec3{point.x, floor_height, point.y};
}

Vec3 ceiling_vertex(const Vec2 point, const float floor_height, const float height) {
    return Vec3{point.x, floor_height + height, point.y};
}

void add_triangle(
    RuntimeGeometry& geometry,
    const RuntimeTriangle triangle,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    geometry.triangles.push_back(triangle);
    geometry.material_ids.push_back(clamped_material_id(material_id));
    geometry.surfaces.push_back(surface);
}

void add_wall_span(
    RuntimeGeometry& geometry,
    const Vec2 a,
    const Vec2 b,
    const float bottom,
    const float top,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    if (top <= bottom + 0.001F) {
        return;
    }

    const Vec3 bottom_a = floor_vertex(a, bottom);
    const Vec3 bottom_b = floor_vertex(b, bottom);
    const Vec3 top_a = floor_vertex(a, top);
    const Vec3 top_b = floor_vertex(b, top);

    add_triangle(geometry, RuntimeTriangle{bottom_a, top_a, top_b}, material_id, surface);
    add_triangle(geometry, RuntimeTriangle{bottom_a, top_b, bottom_b}, material_id, surface);
}

void add_wall(
    RuntimeGeometry& geometry,
    const Vec2 a,
    const Vec2 b,
    const float floor_height,
    const float height,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    add_wall_span(geometry, a, b, floor_height, floor_height + height, material_id, surface);
}

void add_loop_walls(
    RuntimeGeometry& geometry,
    const PolygonLoop& loop,
    const std::vector<int>& materials,
    const int hole_index,
    const float floor_height,
    const float height
) {
    if (loop.vertices.size() < 2) {
        return;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const int material_id = i < materials.size() ? materials[i] : kDefaultMaterialId;
        add_wall(
            geometry,
            loop.vertices[i],
            loop.vertices[(i + 1) % loop.vertices.size()],
            floor_height,
            height,
            material_id,
            RuntimeSurfaceRef{RuntimeSurfaceKind::HoleWall, hole_index, static_cast<int>(i)}
        );
    }
}

void add_neighbor_gap_walls(
    RuntimeGeometry& geometry,
    const SectorPlane& sector,
    const SectorPlane& neighbor,
    const Vec2 a,
    const Vec2 b,
    const int material_id,
    const RuntimeSurfaceRef surface
) {
    const float sector_floor = sector.floor_height;
    const float sector_ceiling = sector.floor_height + sector.height;
    const float neighbor_floor = neighbor.floor_height;
    const float neighbor_ceiling = neighbor.floor_height + neighbor.height;
    const float overlap_floor = std::max(sector_floor, neighbor_floor);
    const float overlap_ceiling = std::min(sector_ceiling, neighbor_ceiling);

    if (overlap_ceiling <= overlap_floor + 0.001F) {
        add_wall_span(geometry, a, b, sector_floor, sector_ceiling, material_id, surface);
        return;
    }

    add_wall_span(geometry, a, b, sector_floor, overlap_floor, material_id, surface);
    add_wall_span(geometry, a, b, overlap_ceiling, sector_ceiling, material_id, surface);
}

} // namespace

RuntimeGeometry build_runtime_geometry(const std::vector<SectorPlane>& sectors) {
    RuntimeGeometry geometry;

    for (SectorPlane sector : sectors) {
        normalize_materials(sector);
        geometry.triangles.reserve(geometry.triangles.size() + sector.triangles.size() * 2 + sector.outer.vertices.size() * 2);

        for (const Triangle& triangle : sector.triangles) {
            add_triangle(geometry, RuntimeTriangle{
                floor_vertex(triangle.a, sector.floor_height),
                floor_vertex(triangle.b, sector.floor_height),
                floor_vertex(triangle.c, sector.floor_height),
            }, sector.floor_material, RuntimeSurfaceRef{RuntimeSurfaceKind::Floor, -1, -1});
            add_triangle(geometry, RuntimeTriangle{
                ceiling_vertex(triangle.c, sector.floor_height, sector.height),
                ceiling_vertex(triangle.b, sector.floor_height, sector.height),
                ceiling_vertex(triangle.a, sector.floor_height, sector.height),
            }, sector.ceiling_material, RuntimeSurfaceRef{RuntimeSurfaceKind::Ceiling, -1, -1});
        }

        for (std::size_t edge_index = 0; edge_index < sector.outer.vertices.size(); ++edge_index) {
            const int neighbor = edge_index < sector.edge_neighbors.size() ? sector.edge_neighbors[edge_index] : -1;
            const Vec2 a = sector.outer.vertices[edge_index];
            const Vec2 b = sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()];
            const int material_id = edge_index < sector.wall_materials.size()
                ? sector.wall_materials[edge_index]
                : kDefaultMaterialId;
            const RuntimeSurfaceRef surface{RuntimeSurfaceKind::Wall, static_cast<int>(edge_index), -1};

            if (neighbor >= 0 && neighbor < static_cast<int>(sectors.size())) {
                add_neighbor_gap_walls(geometry, sector, sectors[static_cast<std::size_t>(neighbor)], a, b, material_id, surface);
            } else {
                add_wall(geometry, a, b, sector.floor_height, sector.height, material_id, surface);
            }
        }

        for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
            if (hole_index < sector.hole_wall_materials.size()) {
                add_loop_walls(
                    geometry,
                    sector.holes[hole_index],
                    sector.hole_wall_materials[hole_index],
                    static_cast<int>(hole_index),
                    sector.floor_height,
                    sector.height
                );
            } else {
                const std::vector<int> materials;
                add_loop_walls(
                    geometry,
                    sector.holes[hole_index],
                    materials,
                    static_cast<int>(hole_index),
                    sector.floor_height,
                    sector.height
                );
            }
        }
    }

    return geometry;
}

} // namespace undecedent
