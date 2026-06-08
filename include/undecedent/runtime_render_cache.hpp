#pragma once

#include "undecedent/geometry.hpp"
#include "undecedent/materials.hpp"
#include "undecedent/runtime_world.hpp"

#include <glad/glad.h>

#include <cstdint>
#include <vector>

namespace undecedent {

struct RuntimeRenderVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float roughness = 0.72F;
    float metallic = 0.0F;
    float specular = 0.04F;
    float u = 0.0F;
    float v = 0.0F;
    float material_slot = 0.0F;
    float tx = 1.0F;
    float ty = 0.0F;
    float tz = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    float bz = 1.0F;
};

struct RuntimeRenderRange {
    GLint first_vertex = 0;
    GLsizei vertex_count = 0;
};

struct RuntimeRenderCache {
    GLuint vertex_buffer = 0;
    GLsizei total_vertices = 0;
    std::uint64_t shadow_revision = 1;
    std::vector<RuntimeRenderRange> sector_ranges;
};

Vec3 runtime_triangle_lighting_normal(const RuntimeTriangle& triangle);
void rebuild_runtime_render_cache(RuntimeRenderCache& render_cache, const RuntimeWorld& world);
void rebuild_runtime_render_cache(
    RuntimeRenderCache& render_cache,
    const RuntimeWorld& world,
    const MaterialLibrary& material_library
);
void destroy_runtime_render_cache(RuntimeRenderCache& render_cache);

} // namespace undecedent
