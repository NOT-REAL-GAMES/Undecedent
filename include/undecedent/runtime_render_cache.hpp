#pragma once

#include "undecedent/geometry.hpp"
#include "undecedent/runtime_world.hpp"

#include <glad/glad.h>

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
};

struct RuntimeRenderRange {
    GLint first_vertex = 0;
    GLsizei vertex_count = 0;
};

struct RuntimeRenderCache {
    GLuint vertex_buffer = 0;
    GLsizei total_vertices = 0;
    std::vector<RuntimeRenderRange> sector_ranges;
};

Vec3 runtime_triangle_lighting_normal(const RuntimeTriangle& triangle);
void rebuild_runtime_render_cache(RuntimeRenderCache& render_cache, const RuntimeWorld& world);
void destroy_runtime_render_cache(RuntimeRenderCache& render_cache);

} // namespace undecedent
