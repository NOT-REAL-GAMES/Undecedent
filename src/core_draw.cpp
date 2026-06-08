#include "undecedent/core_draw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace undecedent {
namespace {

struct CoreVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct CoreDrawState {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint mvp_uniform = -1;
    std::array<float, 16> mvp{};
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    float line_width = 1.0F;
    GLenum mode = 0;
    std::vector<CoreVertex> submitted;
    struct Batch {
        GLenum primitive = GL_TRIANGLES;
        std::array<float, 16> mvp{};
        std::vector<CoreVertex> vertices;
    };
    std::vector<Batch> batches;
    std::vector<CoreVertex> flush_vertices;
    std::vector<GLint> flush_first_vertices;
    std::vector<GLsizei> flush_vertex_counts;
    int screen_width = 1;
    int screen_height = 1;
    bool initialized = false;
    bool failed = false;
};

CoreDrawState& state() {
    static CoreDrawState value;
    return value;
}

std::array<float, 16> identity_matrix() {
    std::array<float, 16> matrix{};
    matrix[0] = 1.0F;
    matrix[5] = 1.0F;
    matrix[10] = 1.0F;
    matrix[15] = 1.0F;
    return matrix;
}

std::string shader_log(const GLuint object, const bool program) {
    GLint length = 0;
    if (program) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 1) {
        return {};
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    if (program) {
        glGetProgramInfoLog(object, length, &written, log.data());
    } else {
        glGetShaderInfoLog(object, length, &written, log.data());
    }
    log.resize(static_cast<std::size_t>(written));
    return log;
}

GLuint compile_shader(const GLenum type, const char* source, const char* label) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        std::cerr << "Core draw shader compile failed (" << label << "): "
                  << shader_log(shader, false) << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ensure_core_draw() {
    CoreDrawState& draw = state();
    if (draw.initialized) {
        return true;
    }
    if (draw.failed) {
        return false;
    }

    static constexpr const char* vertex_source = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 uMvp;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";
    static constexpr const char* fragment_source = R"(
#version 430 core
in vec4 vColor;
layout(location = 0) out vec4 oColor;
void main() {
    oColor = vColor;
}
)";

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, "core draw vertex");
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, "core draw fragment");
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        draw.failed = true;
        return false;
    }

    draw.program = glCreateProgram();
    glAttachShader(draw.program, vertex_shader);
    glAttachShader(draw.program, fragment_shader);
    glLinkProgram(draw.program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(draw.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::cerr << "Core draw shader link failed: " << shader_log(draw.program, true) << '\n';
        glDeleteProgram(draw.program);
        draw.program = 0;
        draw.failed = true;
        return false;
    }

    draw.mvp_uniform = glGetUniformLocation(draw.program, "uMvp");
    draw.mvp = identity_matrix();
    glGenVertexArrays(1, &draw.vao);
    glGenBuffers(1, &draw.vbo);
    draw.initialized = draw.vao != 0 && draw.vbo != 0;
    if (!draw.initialized) {
        draw.failed = true;
    }
    return draw.initialized;
}

void append_converted_vertices(std::vector<CoreVertex>& out, const GLenum mode, const std::vector<CoreVertex>& in) {
    switch (mode) {
        case GL_LINES:
        case GL_TRIANGLES:
            out.insert(out.end(), in.begin(), in.end());
            break;
        case GL_LINE_STRIP:
            for (std::size_t i = 1; i < in.size(); ++i) {
                out.push_back(in[i - 1]);
                out.push_back(in[i]);
            }
            break;
        case GL_LINE_LOOP:
            if (in.size() >= 2) {
                for (std::size_t i = 1; i < in.size(); ++i) {
                    out.push_back(in[i - 1]);
                    out.push_back(in[i]);
                }
                out.push_back(in.back());
                out.push_back(in.front());
            }
            break;
        case kCoreQuads:
            for (std::size_t i = 0; i + 3 < in.size(); i += 4) {
                out.push_back(in[i]);
                out.push_back(in[i + 1]);
                out.push_back(in[i + 2]);
                out.push_back(in[i]);
                out.push_back(in[i + 2]);
                out.push_back(in[i + 3]);
            }
            break;
        default:
            break;
    }
}

std::size_t converted_vertex_count(const GLenum mode, const std::size_t submitted_vertex_count) {
    switch (mode) {
        case GL_LINES:
        case GL_TRIANGLES:
            return submitted_vertex_count;
        case GL_LINE_STRIP:
            return submitted_vertex_count >= 2 ? (submitted_vertex_count - 1) * 2 : 0;
        case GL_LINE_LOOP:
            return submitted_vertex_count >= 2 ? submitted_vertex_count * 2 : 0;
        case kCoreQuads:
            return (submitted_vertex_count / 4) * 6;
        default:
            return 0;
    }
}

bool is_identity_matrix(const std::array<float, 16>& matrix) {
    return matrix == identity_matrix();
}

GLenum converted_primitive(const GLenum mode) {
    if (mode == GL_LINE_STRIP || mode == GL_LINE_LOOP) {
        return GL_LINES;
    }
    if (mode == kCoreQuads) {
        return GL_TRIANGLES;
    }
    return mode;
}

void append_thick_ndc_line(
    std::vector<CoreVertex>& out,
    const CoreVertex& a,
    const CoreVertex& b,
    const float line_width,
    const int screen_width,
    const int screen_height
) {
    const float dx_pixels = (b.x - a.x) * 0.5F * static_cast<float>(screen_width);
    const float dy_pixels = (b.y - a.y) * -0.5F * static_cast<float>(screen_height);
    const float length = std::sqrt(dx_pixels * dx_pixels + dy_pixels * dy_pixels);
    if (length <= 0.0001F) {
        return;
    }

    const float nx_pixels = -dy_pixels / length;
    const float ny_pixels = dx_pixels / length;
    const float half_width = std::max(1.0F, line_width) * 0.5F;
    const float ox = nx_pixels * half_width * 2.0F / static_cast<float>(std::max(1, screen_width));
    const float oy = -ny_pixels * half_width * 2.0F / static_cast<float>(std::max(1, screen_height));

    const CoreVertex a0{a.x - ox, a.y - oy, a.z, a.r, a.g, a.b, a.a};
    const CoreVertex a1{a.x + ox, a.y + oy, a.z, a.r, a.g, a.b, a.a};
    const CoreVertex b0{b.x - ox, b.y - oy, b.z, b.r, b.g, b.b, b.a};
    const CoreVertex b1{b.x + ox, b.y + oy, b.z, b.r, b.g, b.b, b.a};
    out.push_back(a0);
    out.push_back(b0);
    out.push_back(b1);
    out.push_back(a0);
    out.push_back(b1);
    out.push_back(a1);
}

void append_screen_line_triangles(
    std::vector<CoreVertex>& out,
    const GLenum mode,
    const std::vector<CoreVertex>& in,
    const float line_width,
    const int screen_width,
    const int screen_height
) {
    switch (mode) {
        case GL_LINES:
            for (std::size_t i = 0; i + 1 < in.size(); i += 2) {
                append_thick_ndc_line(out, in[i], in[i + 1], line_width, screen_width, screen_height);
            }
            break;
        case GL_LINE_STRIP:
            for (std::size_t i = 1; i < in.size(); ++i) {
                append_thick_ndc_line(out, in[i - 1], in[i], line_width, screen_width, screen_height);
            }
            break;
        case GL_LINE_LOOP:
            if (in.size() >= 2) {
                for (std::size_t i = 1; i < in.size(); ++i) {
                    append_thick_ndc_line(out, in[i - 1], in[i], line_width, screen_width, screen_height);
                }
                append_thick_ndc_line(out, in.back(), in.front(), line_width, screen_width, screen_height);
            }
            break;
        default:
            break;
    }
}

bool is_line_primitive(const GLenum mode) {
    return mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP;
}

CoreDrawState::Batch& append_batch(CoreDrawState& draw, const GLenum primitive, const std::array<float, 16>& mvp) {
    if (!draw.batches.empty()) {
        CoreDrawState::Batch& last = draw.batches.back();
        if (last.primitive == primitive && last.mvp == mvp) {
            return last;
        }
    }
    draw.batches.push_back(CoreDrawState::Batch{primitive, mvp, {}});
    return draw.batches.back();
}

void bind_core_vertex_layout(
    const GLsizei stride,
    const std::size_t position_offset,
    const std::size_t color_offset,
    const GLint color_components
) {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        stride,
        reinterpret_cast<const void*>(position_offset)
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        color_components,
        GL_FLOAT,
        GL_FALSE,
        stride,
        reinterpret_cast<const void*>(color_offset)
    );
}

} // namespace

void core_draw_shutdown() {
    CoreDrawState& draw = state();
    if (draw.vbo != 0) {
        glDeleteBuffers(1, &draw.vbo);
    }
    if (draw.vao != 0) {
        glDeleteVertexArrays(1, &draw.vao);
    }
    if (draw.program != 0) {
        glDeleteProgram(draw.program);
    }
    draw = {};
}

void core_draw_begin_frame(const int screen_width, const int screen_height) {
    CoreDrawState& draw = state();
    draw.screen_width = std::max(1, screen_width);
    draw.screen_height = std::max(1, screen_height);
    draw.submitted.clear();
    draw.batches.clear();
    draw.mode = 0;
}

void core_draw_flush() {
    CoreDrawState& draw = state();
    if (draw.batches.empty() || !ensure_core_draw()) {
        draw.batches.clear();
        return;
    }

    std::size_t total_vertices = 0;
    for (const CoreDrawState::Batch& batch : draw.batches) {
        total_vertices += batch.vertices.size();
    }
    if (total_vertices == 0) {
        draw.batches.clear();
        return;
    }
    draw.flush_vertices.clear();
    draw.flush_first_vertices.clear();
    draw.flush_vertex_counts.clear();
    draw.flush_vertices.reserve(total_vertices);
    draw.flush_first_vertices.reserve(draw.batches.size());
    draw.flush_vertex_counts.reserve(draw.batches.size());
    for (const CoreDrawState::Batch& batch : draw.batches) {
        draw.flush_first_vertices.push_back(static_cast<GLint>(draw.flush_vertices.size()));
        draw.flush_vertex_counts.push_back(static_cast<GLsizei>(batch.vertices.size()));
        draw.flush_vertices.insert(draw.flush_vertices.end(), batch.vertices.begin(), batch.vertices.end());
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(draw.program);
    glBindVertexArray(draw.vao);
    glBindBuffer(GL_ARRAY_BUFFER, draw.vbo);
    bind_core_vertex_layout(sizeof(CoreVertex), offsetof(CoreVertex, x), offsetof(CoreVertex, r), 4);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(draw.flush_vertices.size() * sizeof(CoreVertex)),
        draw.flush_vertices.data(),
        GL_STREAM_DRAW
    );

    std::array<float, 16> active_mvp{};
    bool has_active_mvp = false;
    for (std::size_t batch_index = 0; batch_index < draw.batches.size(); ++batch_index) {
        const CoreDrawState::Batch& batch = draw.batches[batch_index];
        if (draw.flush_vertex_counts[batch_index] <= 0) {
            continue;
        }
        if (!has_active_mvp || active_mvp != batch.mvp) {
            glUniformMatrix4fv(draw.mvp_uniform, 1, GL_FALSE, batch.mvp.data());
            active_mvp = batch.mvp;
            has_active_mvp = true;
        }
        if (batch.primitive == GL_LINES) {
            glLineWidth(std::max(1.0F, draw.line_width));
        }
        glDrawArrays(batch.primitive, draw.flush_first_vertices[batch_index], draw.flush_vertex_counts[batch_index]);
    }

    glLineWidth(1.0F);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    draw.batches.clear();
}

void core_set_identity_mvp() {
    state().mvp = identity_matrix();
}

void core_set_mvp(const std::array<float, 16>& matrix) {
    state().mvp = matrix;
}

void core_set_line_width(const float width) {
    state().line_width = std::max(1.0F, width);
}

void core_begin(const GLenum mode) {
    CoreDrawState& draw = state();
    draw.mode = mode;
    draw.submitted.clear();
}

void core_color4f(const float r, const float g, const float b, const float a) {
    state().color = {r, g, b, a};
}

void core_vertex2f(const float x, const float y) {
    core_vertex3f(x, y, 0.0F);
}

void core_vertex3f(const float x, const float y, const float z) {
    CoreDrawState& draw = state();
    draw.submitted.push_back(CoreVertex{x, y, z, draw.color[0], draw.color[1], draw.color[2], draw.color[3]});
}

void core_end() {
    CoreDrawState& draw = state();
    if (draw.submitted.empty()) {
        draw.submitted.clear();
        return;
    }

    std::vector<CoreVertex> vertices;
    GLenum primitive = converted_primitive(draw.mode);
    std::array<float, 16> mvp = draw.mvp;
    if (is_identity_matrix(draw.mvp) && is_line_primitive(draw.mode) && draw.line_width > 1.0F) {
        primitive = GL_TRIANGLES;
        mvp = identity_matrix();
        vertices.reserve(draw.submitted.size() * 6);
        append_screen_line_triangles(
            vertices,
            draw.mode,
            draw.submitted,
            draw.line_width,
            draw.screen_width,
            draw.screen_height
        );
    } else {
        vertices.reserve(draw.submitted.size() * 2);
        append_converted_vertices(vertices, draw.mode, draw.submitted);
    }
    draw.submitted.clear();
    if (vertices.empty()) {
        return;
    }

    CoreDrawState::Batch& batch = append_batch(draw, primitive, mvp);
    batch.vertices.insert(batch.vertices.end(), vertices.begin(), vertices.end());
}

void core_draw_colored_arrays(
    const GLenum primitive,
    const GLuint vertex_buffer,
    const GLint first_vertex,
    const GLsizei vertex_count,
    const GLsizei stride,
    const std::size_t position_offset,
    const std::size_t color_offset,
    const GLint color_components
) {
    if (vertex_buffer == 0 || vertex_count <= 0 || !ensure_core_draw()) {
        return;
    }
    CoreDrawState& draw = state();
    glUseProgram(draw.program);
    glUniformMatrix4fv(draw.mvp_uniform, 1, GL_FALSE, draw.mvp.data());
    glBindVertexArray(draw.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    bind_core_vertex_layout(stride, position_offset, color_offset, color_components);
    if (primitive == GL_LINES) {
        glLineWidth(std::max(1.0F, draw.line_width));
    }
    glDrawArrays(primitive, first_vertex, vertex_count);
    glLineWidth(1.0F);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

CoreDrawStats core_draw_pending_stats() {
    const CoreDrawState& draw = state();
    CoreDrawStats stats;
    stats.batches = static_cast<int>(draw.batches.size());
    for (const CoreDrawState::Batch& batch : draw.batches) {
        stats.vertices += static_cast<int>(batch.vertices.size());
    }
    return stats;
}

std::size_t core_debug_converted_vertex_count(const GLenum mode, const std::size_t submitted_vertex_count) {
    return converted_vertex_count(mode, submitted_vertex_count);
}

std::size_t core_debug_screen_line_triangle_count(
    const GLenum mode,
    const std::size_t submitted_vertex_count,
    const float line_width
) {
    if (!is_line_primitive(mode) || line_width <= 1.0F) {
        return 0;
    }
    switch (mode) {
        case GL_LINES:
            return (submitted_vertex_count / 2) * 6;
        case GL_LINE_STRIP:
            return submitted_vertex_count >= 2 ? (submitted_vertex_count - 1) * 6 : 0;
        case GL_LINE_LOOP:
            return submitted_vertex_count >= 2 ? submitted_vertex_count * 6 : 0;
        default:
            return 0;
    }
}

} // namespace undecedent
