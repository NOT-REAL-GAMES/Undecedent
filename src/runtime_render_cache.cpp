#include "undecedent/runtime_render_cache.hpp"

#include "undecedent/materials.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>

namespace undecedent {
namespace {

constexpr std::int64_t kSmoothNormalScale = 1024;

struct SmoothNormalKey {
    int sector_id = -1;
    RuntimeSurfaceKind kind = RuntimeSurfaceKind::Floor;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
};

bool operator<(const SmoothNormalKey& a, const SmoothNormalKey& b) {
    if (a.sector_id != b.sector_id) {
        return a.sector_id < b.sector_id;
    }
    if (a.kind != b.kind) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    }
    if (a.x != b.x) {
        return a.x < b.x;
    }
    if (a.y != b.y) {
        return a.y < b.y;
    }
    return a.z < b.z;
}

bool uses_smooth_normals(const RuntimeTaggedTriangle& tagged_triangle) {
    return tagged_triangle.surface.kind == RuntimeSurfaceKind::Floor ||
        tagged_triangle.surface.kind == RuntimeSurfaceKind::Ceiling;
}

std::int64_t normal_coord(const float value) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value) * kSmoothNormalScale));
}

SmoothNormalKey smooth_normal_key(const RuntimeTaggedTriangle& tagged_triangle, const Vec3 point) {
    return SmoothNormalKey{
        tagged_triangle.sector_id,
        tagged_triangle.surface.kind,
        normal_coord(point.x),
        normal_coord(point.y),
        normal_coord(point.z),
    };
}

Vec3 add_vec3(const Vec3 a, const Vec3 b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 normalized_vec3(const Vec3 value) {
    const float length = std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
    if (length <= 0.00001F) {
        return Vec3{0.0F, 1.0F, 0.0F};
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

Vec3 sub_vec3_local(const Vec3 a, const Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul_vec3_local(const Vec3 value, const float scale) {
    return Vec3{value.x * scale, value.y * scale, value.z * scale};
}

float dot_vec3_local(const Vec3 a, const Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

Vec3 cross_vec3_local(const Vec3 a, const Vec3 b) {
    return Vec3{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

Vec3 orthogonalized(const Vec3 value, const Vec3 normal) {
    return normalized_vec3(sub_vec3_local(value, mul_vec3_local(normal, dot_vec3_local(value, normal))));
}

void fallback_tangent_basis(const Vec3 normal, Vec3& tangent, Vec3& bitangent) {
    const Vec3 helper = std::abs(normal.y) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    tangent = normalized_vec3(cross_vec3_local(helper, normal));
    bitangent = normalized_vec3(cross_vec3_local(normal, tangent));
}

void triangle_tangent_basis(
    const RuntimeTriangle& triangle,
    const Vec2 uv_a,
    const Vec2 uv_b,
    const Vec2 uv_c,
    const Vec3 normal,
    Vec3& tangent,
    Vec3& bitangent
) {
    const Vec3 edge_ab = sub_vec3_local(triangle.b, triangle.a);
    const Vec3 edge_ac = sub_vec3_local(triangle.c, triangle.a);
    const float du_ab = uv_b.x - uv_a.x;
    const float dv_ab = uv_b.y - uv_a.y;
    const float du_ac = uv_c.x - uv_a.x;
    const float dv_ac = uv_c.y - uv_a.y;
    const float determinant = (du_ab * dv_ac) - (du_ac * dv_ab);
    if (std::abs(determinant) <= 0.000001F) {
        fallback_tangent_basis(normal, tangent, bitangent);
        return;
    }

    const float inv_det = 1.0F / determinant;
    tangent = Vec3{
        ((edge_ab.x * dv_ac) - (edge_ac.x * dv_ab)) * inv_det,
        ((edge_ab.y * dv_ac) - (edge_ac.y * dv_ab)) * inv_det,
        ((edge_ab.z * dv_ac) - (edge_ac.z * dv_ab)) * inv_det,
    };
    bitangent = Vec3{
        ((edge_ac.x * du_ab) - (edge_ab.x * du_ac)) * inv_det,
        ((edge_ac.y * du_ab) - (edge_ab.y * du_ac)) * inv_det,
        ((edge_ac.z * du_ab) - (edge_ab.z * du_ac)) * inv_det,
    };
    tangent = orthogonalized(tangent, normal);
    bitangent = normalized_vec3(cross_vec3_local(normal, tangent));
    if (dot_vec3_local(bitangent, bitangent) <= 0.0001F) {
        fallback_tangent_basis(normal, tangent, bitangent);
    }
}

Vec3 runtime_triangle_area_normal(const RuntimeTriangle& triangle) {
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
    return Vec3{
        -((edge_ab.y * edge_ac.z) - (edge_ab.z * edge_ac.y)),
        -((edge_ab.z * edge_ac.x) - (edge_ab.x * edge_ac.z)),
        -((edge_ab.x * edge_ac.y) - (edge_ab.y * edge_ac.x)),
    };
}

const MaterialSlot& normalized_slot(const MaterialLibrary& library, const int material_id) {
    return library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
}

void append_runtime_vertex(
    std::vector<RuntimeRenderVertex>& vertices,
    const Vec3 point,
    const MaterialSlot& material,
    const Vec3 normal,
    const Vec3 tangent,
    const Vec3 bitangent,
    const Vec2 uv,
    const int material_id
) {
    const float uv_scale = std::max(material.uv_scale, 0.001F);
    vertices.push_back(RuntimeRenderVertex{
        point.x,
        point.y,
        point.z,
        material.base_color.r,
        material.base_color.g,
        material.base_color.b,
        normal.x,
        normal.y,
        normal.z,
        material.roughness,
        material.metallic,
        material.specular,
        uv.x / uv_scale,
        uv.y / uv_scale,
        static_cast<float>(clamped_material_id(material_id)),
        tangent.x,
        tangent.y,
        tangent.z,
        bitangent.x,
        bitangent.y,
        bitangent.z,
    });
}

void append_runtime_triangle(
    std::vector<RuntimeRenderVertex>& vertices,
    const RuntimeTaggedTriangle& tagged_triangle,
    const std::map<SmoothNormalKey, Vec3>& smooth_normals,
    const MaterialLibrary& material_library
) {
    const RuntimeTriangle& triangle = tagged_triangle.triangle;
    const MaterialSlot& material = normalized_slot(material_library, tagged_triangle.material_id);
    const Vec3 face_normal = runtime_triangle_lighting_normal(triangle);
    Vec3 tangent{};
    Vec3 bitangent{};
    triangle_tangent_basis(
        triangle,
        tagged_triangle.uv_a,
        tagged_triangle.uv_b,
        tagged_triangle.uv_c,
        face_normal,
        tangent,
        bitangent
    );
    const auto vertex_normal = [&](const Vec3 point) {
        if (!uses_smooth_normals(tagged_triangle)) {
            return face_normal;
        }
        const auto found = smooth_normals.find(smooth_normal_key(tagged_triangle, point));
        return found == smooth_normals.end() ? face_normal : found->second;
    };
    append_runtime_vertex(vertices, triangle.a, material, vertex_normal(triangle.a), tangent, bitangent, tagged_triangle.uv_a, tagged_triangle.material_id);
    append_runtime_vertex(vertices, triangle.b, material, vertex_normal(triangle.b), tangent, bitangent, tagged_triangle.uv_b, tagged_triangle.material_id);
    append_runtime_vertex(vertices, triangle.c, material, vertex_normal(triangle.c), tangent, bitangent, tagged_triangle.uv_c, tagged_triangle.material_id);
}

std::map<SmoothNormalKey, Vec3> build_smooth_normals(const RuntimeWorld& world) {
    std::map<SmoothNormalKey, Vec3> accumulated;
    for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
        if (!uses_smooth_normals(tagged_triangle)) {
            continue;
        }
        const Vec3 area_normal = runtime_triangle_area_normal(tagged_triangle.triangle);
        accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.a)] =
            add_vec3(accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.a)], area_normal);
        accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.b)] =
            add_vec3(accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.b)], area_normal);
        accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.c)] =
            add_vec3(accumulated[smooth_normal_key(tagged_triangle, tagged_triangle.triangle.c)], area_normal);
    }

    for (auto& [key, normal] : accumulated) {
        (void)key;
        normal = normalized_vec3(normal);
    }
    return accumulated;
}

} // namespace

Vec3 runtime_triangle_lighting_normal(const RuntimeTriangle& triangle) {
    return normalized_vec3(runtime_triangle_area_normal(triangle));
}

void rebuild_runtime_render_cache(RuntimeRenderCache& render_cache, const RuntimeWorld& world) {
    rebuild_runtime_render_cache(render_cache, world, default_material_library());
}

void rebuild_runtime_render_cache(
    RuntimeRenderCache& render_cache,
    const RuntimeWorld& world,
    const MaterialLibrary& material_library
) {
    if (glGenBuffers == nullptr || glBindBuffer == nullptr || glBufferData == nullptr) {
        render_cache.total_vertices = 0;
        render_cache.sector_ranges.clear();
        return;
    }

    std::vector<RuntimeRenderVertex> vertices;
    const std::map<SmoothNormalKey, Vec3> smooth_normals = build_smooth_normals(world);
    const MaterialLibrary normalized_library = normalized_material_library(material_library);
    render_cache.sector_ranges.assign(world.sectors.size(), RuntimeRenderRange{});

    for (std::size_t sector_index = 0; sector_index < world.sectors.size(); ++sector_index) {
        RuntimeRenderRange range{};
        range.first_vertex = static_cast<GLint>(vertices.size());
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (tagged_triangle.sector_id != static_cast<int>(sector_index)) {
                continue;
            }
            append_runtime_triangle(vertices, tagged_triangle, smooth_normals, normalized_library);
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
    if (glDeleteBuffers == nullptr) {
        render_cache = {};
        return;
    }

    if (render_cache.vertex_buffer != 0) {
        glDeleteBuffers(1, &render_cache.vertex_buffer);
    }
    render_cache = {};
}

} // namespace undecedent
