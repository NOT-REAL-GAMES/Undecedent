#include "undecedent/runtime_geometry.hpp"

#include "undecedent/displacement.hpp"

#include <algorithm>
#include <cmath>
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

Vec3 floor_vertex(const Vec2 point, const float y) {
    return Vec3{point.x, y, point.y};
}

float lerp(const float a, const float b, const float t) {
    return a + ((b - a) * t);
}

Vec2 lerp(const Vec2 a, const Vec2 b, const float t) {
    return Vec2{lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

Vec2 floor_uv(const Vec2 point) {
    return point;
}

Vec2 ceiling_uv(const Vec2 point) {
    return Vec2{point.x, -point.y};
}

float wall_v(const float height) {
    return -height;
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

void add_triangle(
    RuntimeGeometry& geometry,
    const RuntimeTriangle triangle,
    const int material_id,
    const RuntimeSurfaceRef surface,
    const Vec2 uv_a,
    const Vec2 uv_b,
    const Vec2 uv_c
) {
    geometry.triangles.push_back(triangle);
    geometry.material_ids.push_back(clamped_material_id(material_id));
    geometry.surfaces.push_back(surface);
    geometry.uv_a.push_back(uv_a);
    geometry.uv_b.push_back(uv_b);
    geometry.uv_c.push_back(uv_c);
}

void add_wall_span(
    RuntimeGeometry& geometry,
    const Vec2 a,
    const Vec2 b,
    const float bottom_a,
    const float bottom_b,
    const float top_a,
    const float top_b,
    const int material_id,
    const RuntimeSurfaceRef surface,
    const float u_a,
    const float u_b
) {
    if (top_a <= bottom_a + 0.001F && top_b <= bottom_b + 0.001F) {
        return;
    }

    const Vec3 bottom_va = floor_vertex(a, bottom_a);
    const Vec3 bottom_vb = floor_vertex(b, bottom_b);
    const Vec3 top_va = floor_vertex(a, top_a);
    const Vec3 top_vb = floor_vertex(b, top_b);

    add_triangle(
        geometry,
        RuntimeTriangle{bottom_va, top_va, top_vb},
        material_id,
        surface,
        Vec2{u_a, wall_v(bottom_a)},
        Vec2{u_a, wall_v(top_a)},
        Vec2{u_b, wall_v(top_b)}
    );
    add_triangle(
        geometry,
        RuntimeTriangle{bottom_va, top_vb, bottom_vb},
        material_id,
        surface,
        Vec2{u_a, wall_v(bottom_a)},
        Vec2{u_b, wall_v(top_b)},
        Vec2{u_b, wall_v(bottom_b)}
    );
}

void add_wall(
    RuntimeGeometry& geometry,
    const SectorPlane& sector,
    const Vec2 a,
    const Vec2 b,
    const int material_id,
    const RuntimeSurfaceRef surface,
    const float edge_u_start
) {
    const int segments = surface_segment_count(sector);
    const float edge_length = std::sqrt(((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y)));
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec2 p0 = lerp(a, b, t0);
        const Vec2 p1 = lerp(a, b, t1);
        add_wall_span(
            geometry,
            p0,
            p1,
            sample_surface_height(sector, SectorSurfaceKind::Floor, p0),
            sample_surface_height(sector, SectorSurfaceKind::Floor, p1),
            sample_surface_height(sector, SectorSurfaceKind::Ceiling, p0),
            sample_surface_height(sector, SectorSurfaceKind::Ceiling, p1),
            material_id,
            surface,
            edge_u_start + (edge_length * t0),
            edge_u_start + (edge_length * t1)
        );
    }
}

float loop_u_start(const PolygonLoop& loop, const std::size_t edge_index) {
    float u = 0.0F;
    for (std::size_t i = 0; i < edge_index && i < loop.vertices.size(); ++i) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[(i + 1) % loop.vertices.size()];
        u += std::sqrt(((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y)));
    }
    return u;
}

void add_loop_walls(
    RuntimeGeometry& geometry,
    const PolygonLoop& loop,
    const std::vector<int>& materials,
    const int hole_index,
    const SectorPlane& sector
) {
    if (loop.vertices.size() < 2) {
        return;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const int material_id = i < materials.size() ? materials[i] : kDefaultMaterialId;
        add_wall(
            geometry,
            sector,
            loop.vertices[i],
            loop.vertices[(i + 1) % loop.vertices.size()],
            material_id,
            RuntimeSurfaceRef{RuntimeSurfaceKind::HoleWall, hole_index, static_cast<int>(i)},
            loop_u_start(loop, i)
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
    const RuntimeSurfaceRef surface,
    const float edge_u_start
) {
    const int segments = std::max(surface_segment_count(sector), surface_segment_count(neighbor));
    const float edge_length = std::sqrt(((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y)));
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

        if (overlap_ceiling_0 <= overlap_floor_0 + 0.001F &&
            overlap_ceiling_1 <= overlap_floor_1 + 0.001F) {
            add_wall_span(
                geometry,
                p0,
                p1,
                sector_floor_0,
                sector_floor_1,
                sector_ceiling_0,
                sector_ceiling_1,
                material_id,
                surface,
                edge_u_start + (edge_length * t0),
                edge_u_start + (edge_length * t1)
            );
            continue;
        }

        add_wall_span(
            geometry,
            p0,
            p1,
            sector_floor_0,
            sector_floor_1,
            overlap_floor_0,
            overlap_floor_1,
            material_id,
            surface,
            edge_u_start + (edge_length * t0),
            edge_u_start + (edge_length * t1)
        );
        add_wall_span(
            geometry,
            p0,
            p1,
            overlap_ceiling_0,
            overlap_ceiling_1,
            sector_ceiling_0,
            sector_ceiling_1,
            material_id,
            surface,
            edge_u_start + (edge_length * t0),
            edge_u_start + (edge_length * t1)
        );
    }
}

} // namespace

RuntimeGeometry build_runtime_geometry(const std::vector<SectorPlane>& sectors) {
    RuntimeGeometry geometry;

    for (SectorPlane sector : sectors) {
        normalize_materials(sector);
        const std::vector<SurfaceSampleTriangle> floor_triangles =
            build_surface_sample_triangles(sector, SectorSurfaceKind::Floor);
        const std::vector<SurfaceSampleTriangle> ceiling_triangles =
            build_surface_sample_triangles(sector, SectorSurfaceKind::Ceiling);
        geometry.triangles.reserve(
            geometry.triangles.size() + floor_triangles.size() + ceiling_triangles.size() + sector.outer.vertices.size() * 2
        );

        for (const SurfaceSampleTriangle& triangle : floor_triangles) {
            add_triangle(geometry, RuntimeTriangle{
                floor_vertex(triangle.a.position, triangle.a.height),
                floor_vertex(triangle.b.position, triangle.b.height),
                floor_vertex(triangle.c.position, triangle.c.height),
            }, sector.floor_material, RuntimeSurfaceRef{RuntimeSurfaceKind::Floor, -1, -1},
            floor_uv(triangle.a.position), floor_uv(triangle.b.position), floor_uv(triangle.c.position));
        }
        for (const SurfaceSampleTriangle& triangle : ceiling_triangles) {
            add_triangle(geometry, RuntimeTriangle{
                floor_vertex(triangle.c.position, triangle.c.height),
                floor_vertex(triangle.b.position, triangle.b.height),
                floor_vertex(triangle.a.position, triangle.a.height),
            }, sector.ceiling_material, RuntimeSurfaceRef{RuntimeSurfaceKind::Ceiling, -1, -1},
            ceiling_uv(triangle.c.position), ceiling_uv(triangle.b.position), ceiling_uv(triangle.a.position));
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
                add_neighbor_gap_walls(
                    geometry,
                    sector,
                    sectors[static_cast<std::size_t>(neighbor)],
                    a,
                    b,
                    material_id,
                    surface,
                    loop_u_start(sector.outer, edge_index)
                );
            } else {
                add_wall(geometry, sector, a, b, material_id, surface, loop_u_start(sector.outer, edge_index));
            }
        }

        for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
            if (hole_index < sector.hole_wall_materials.size()) {
                add_loop_walls(
                    geometry,
                    sector.holes[hole_index],
                    sector.hole_wall_materials[hole_index],
                    static_cast<int>(hole_index),
                    sector
                );
            } else {
                const std::vector<int> materials;
                add_loop_walls(
                    geometry,
                    sector.holes[hole_index],
                    materials,
                    static_cast<int>(hole_index),
                    sector
                );
            }
        }
    }

    return geometry;
}

} // namespace undecedent
