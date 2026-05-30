#include "undecedent/runtime_render_cache.hpp"

#include "undecedent/materials.hpp"

#include <cmath>
#include <cstddef>

namespace undecedent {
namespace {

void append_runtime_vertex(
    std::vector<RuntimeRenderVertex>& vertices,
    const Vec3 point,
    const float r,
    const float g,
    const float b,
    const Vec3 normal
) {
    vertices.push_back(RuntimeRenderVertex{point.x, point.y, point.z, r, g, b, normal.x, normal.y, normal.z});
}

void append_runtime_triangle(
    std::vector<RuntimeRenderVertex>& vertices,
    const RuntimeTaggedTriangle& tagged_triangle
) {
    const RuntimeTriangle& triangle = tagged_triangle.triangle;
    const MaterialColor color = material_color(tagged_triangle.material_id);
    const Vec3 normal = runtime_triangle_lighting_normal(triangle);
    append_runtime_vertex(vertices, triangle.a, color.r, color.g, color.b, normal);
    append_runtime_vertex(vertices, triangle.b, color.r, color.g, color.b, normal);
    append_runtime_vertex(vertices, triangle.c, color.r, color.g, color.b, normal);
}

} // namespace

Vec3 runtime_triangle_lighting_normal(const RuntimeTriangle& triangle) {
    const Vec3 edge_ab{
        triangle.b.x - triangle.a.x,
        triangle.b.y - triangle.a.y,
        triangle.b.z - triangle.a.z,
    };
    const Vec3 edge_ac{
        triangle.c.x - triangle.a.x,
        triangle.c.y - triangle.a.y,
        triangle.c.z - triangle.a.z,
    };
    Vec3 normal{
        -((edge_ab.y * edge_ac.z) - (edge_ab.z * edge_ac.y)),
        -((edge_ab.z * edge_ac.x) - (edge_ab.x * edge_ac.z)),
        -((edge_ab.x * edge_ac.y) - (edge_ab.y * edge_ac.x)),
    };
    const float length = std::sqrt((normal.x * normal.x) + (normal.y * normal.y) + (normal.z * normal.z));
    if (length > 0.00001F) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
        return normal;
    }

    return Vec3{0.0F, 1.0F, 0.0F};
}

void rebuild_runtime_render_cache(RuntimeRenderCache& render_cache, const RuntimeWorld& world) {
    std::vector<RuntimeRenderVertex> vertices;
    render_cache.sector_ranges.assign(world.sectors.size(), RuntimeRenderRange{});

    for (std::size_t sector_index = 0; sector_index < world.sectors.size(); ++sector_index) {
        RuntimeRenderRange range{};
        range.first_vertex = static_cast<GLint>(vertices.size());
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (tagged_triangle.sector_id != static_cast<int>(sector_index)) {
                continue;
            }
            append_runtime_triangle(vertices, tagged_triangle);
        }
        range.vertex_count = static_cast<GLsizei>(vertices.size()) - range.first_vertex;
        render_cache.sector_ranges[sector_index] = range;
    }

    if (render_cache.vertex_buffer == 0) {
        glGenBuffers(1, &render_cache.vertex_buffer);
    }

    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(RuntimeRenderVertex)),
        vertices.empty() ? nullptr : vertices.data(),
        GL_STATIC_DRAW
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    render_cache.total_vertices = static_cast<GLsizei>(vertices.size());
}

void destroy_runtime_render_cache(RuntimeRenderCache& render_cache) {
    if (render_cache.vertex_buffer != 0) {
        glDeleteBuffers(1, &render_cache.vertex_buffer);
    }
    render_cache = {};
}

} // namespace undecedent
