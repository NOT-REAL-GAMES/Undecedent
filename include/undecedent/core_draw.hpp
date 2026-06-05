#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>

namespace undecedent {

constexpr GLenum kCoreQuads = 0x0007;

void core_draw_shutdown();
void core_set_identity_mvp();
void core_set_mvp(const std::array<float, 16>& matrix);

void core_begin(GLenum mode);
void core_color4f(float r, float g, float b, float a);
void core_vertex2f(float x, float y);
void core_vertex3f(float x, float y, float z);
void core_end();

void core_draw_colored_arrays(
    GLenum primitive,
    GLuint vertex_buffer,
    GLint first_vertex,
    GLsizei vertex_count,
    GLsizei stride,
    std::size_t position_offset,
    std::size_t color_offset,
    GLint color_components = 4
);

} // namespace undecedent
