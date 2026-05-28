#include "undecedent/runtime_geometry.hpp"

#include <cstddef>

namespace undecedent {
namespace {

Vec3 floor_vertex(const Vec2 point, const float floor_height) {
    return Vec3{point.x, floor_height, point.y};
}

Vec3 ceiling_vertex(const Vec2 point, const float floor_height, const float height) {
    return Vec3{point.x, floor_height + height, point.y};
}

void add_wall(RuntimeGeometry& geometry, const Vec2 a, const Vec2 b, const float floor_height, const float height) {
    const Vec3 bottom_a = floor_vertex(a, floor_height);
    const Vec3 bottom_b = floor_vertex(b, floor_height);
    const Vec3 top_a = ceiling_vertex(a, floor_height, height);
    const Vec3 top_b = ceiling_vertex(b, floor_height, height);

    geometry.triangles.push_back(RuntimeTriangle{bottom_a, top_a, top_b});
    geometry.triangles.push_back(RuntimeTriangle{bottom_a, top_b, bottom_b});
}

void add_loop_walls(RuntimeGeometry& geometry, const PolygonLoop& loop, const float floor_height, const float height) {
    if (loop.vertices.size() < 2) {
        return;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        add_wall(geometry, loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()], floor_height, height);
    }
}

} // namespace

RuntimeGeometry build_runtime_geometry(const std::vector<SectorPlane>& sectors) {
    RuntimeGeometry geometry;

    for (const SectorPlane& sector : sectors) {
        geometry.triangles.reserve(geometry.triangles.size() + sector.triangles.size() * 2 + sector.outer.vertices.size() * 2);

        for (const Triangle& triangle : sector.triangles) {
            geometry.triangles.push_back(RuntimeTriangle{
                floor_vertex(triangle.a, sector.floor_height),
                floor_vertex(triangle.b, sector.floor_height),
                floor_vertex(triangle.c, sector.floor_height),
            });
            geometry.triangles.push_back(RuntimeTriangle{
                ceiling_vertex(triangle.c, sector.floor_height, sector.height),
                ceiling_vertex(triangle.b, sector.floor_height, sector.height),
                ceiling_vertex(triangle.a, sector.floor_height, sector.height),
            });
        }

        for (std::size_t edge_index = 0; edge_index < sector.outer.vertices.size(); ++edge_index) {
            const int neighbor = edge_index < sector.edge_neighbors.size() ? sector.edge_neighbors[edge_index] : -1;
            if (neighbor >= 0) {
                continue;
            }

            add_wall(
                geometry,
                sector.outer.vertices[edge_index],
                sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()],
                sector.floor_height,
                sector.height
            );
        }

        for (const PolygonLoop& hole : sector.holes) {
            add_loop_walls(geometry, hole, sector.floor_height, sector.height);
        }
    }

    return geometry;
}

} // namespace undecedent
