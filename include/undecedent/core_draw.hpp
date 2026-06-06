#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace undecedent {

constexpr GLenum kCoreQuads = 0x0007;

struct CoreDrawStats {
    int batches = 0;
    int vertices = 0;
};

void core_draw_shutdown();
void core_draw_begin_frame(int screen_width, int screen_height);
void core_draw_flush();
void core_set_identity_mvp();
void core_set_mvp(const std::array<float, 16>& matrix);
void core_set_line_width(float width);

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

CoreDrawStats core_draw_pending_stats();
std::size_t core_debug_converted_vertex_count(GLenum mode, std::size_t submitted_vertex_count);
std::size_t core_debug_screen_line_triangle_count(GLenum mode, std::size_t submitted_vertex_count, float line_width);

} // namespace undecedent
