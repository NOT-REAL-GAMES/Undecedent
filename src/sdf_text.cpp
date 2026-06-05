#include "undecedent/sdf_text.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace undecedent {
namespace {

struct SdfGlyph {
    int code = 0;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float x_offset = 0.0F;
    float y_offset = 0.0F;
    float advance = 0.0F;
};

struct SdfVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct SdfTextState {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint texture = 0;
    GLint atlas_uniform = -1;
    std::filesystem::path metrics_path;
    std::filesystem::path atlas_path;
    std::unordered_map<int, SdfGlyph> glyphs;
    float font_size = 48.0F;
    float spread = 8.0F;
    float ascent = 0.0F;
    float descent = 0.0F;
    float line_height = 56.0F;
    int atlas_width = 0;
    int atlas_height = 0;
    bool metrics_loaded = false;
    bool initialized = false;
    bool failed = false;
};

SdfTextState& state() {
    static SdfTextState value;
    return value;
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
        std::cerr << "SDF text shader compile failed (" << label << "): " << shader_log(shader, false) << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool read_token(std::istream& input, std::string& token) {
    input >> token;
    return static_cast<bool>(input);
}

bool load_ppm(const std::filesystem::path& path, int& width, int& height, std::vector<unsigned char>& pixels) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::string magic;
    int max_value = 0;
    input >> magic >> width >> height >> max_value;
    if (magic != "P6" || width <= 0 || height <= 0 || max_value != 255) {
        return false;
    }
    input.get();
    pixels.resize(static_cast<std::size_t>(width * height * 3));
    input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return input.gcount() == static_cast<std::streamsize>(pixels.size());
}

std::vector<std::filesystem::path> default_metrics_candidates() {
    return {
        "assets/fonts/google_sans_code_msdf.fnt",
        "../assets/fonts/google_sans_code_msdf.fnt",
        "../../assets/fonts/google_sans_code_msdf.fnt",
    };
}

bool ensure_metrics_loaded() {
    SdfTextState& text = state();
    if (text.metrics_loaded) {
        return true;
    }
    for (const std::filesystem::path& candidate : default_metrics_candidates()) {
        if (load_sdf_text_font(candidate.string())) {
            return true;
        }
    }
    return false;
}

bool ensure_renderer() {
    SdfTextState& text = state();
    if (text.initialized) {
        return true;
    }
    if (text.failed || !ensure_metrics_loaded()) {
        return false;
    }

    static constexpr const char* vertex_source = R"(
#version 430 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";
    static constexpr const char* fragment_source = R"(
#version 430 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uAtlas;
layout(location = 0) out vec4 oColor;
float median3(vec3 value) {
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}
float signed_distance_at(vec2 uv) {
    return median3(texture(uAtlas, uv).rgb) - 0.5;
}
float coverage_from_distance(float signed_distance, float pixel_width) {
    return clamp((signed_distance / pixel_width) + 0.5, 0.0, 1.0);
}
float coverage_at(vec2 uv, float pixel_bias, float softness) {
    float signed_distance = signed_distance_at(uv);
    float pixel_width = max(fwidth(signed_distance), 0.0008) * softness;
    return coverage_from_distance(signed_distance + pixel_bias, pixel_width);
}
void main() {
    vec2 subpixel_step = dFdx(vUv) * 0.3333333;
    float center_distance = signed_distance_at(vUv);
    float pixel_width = max(fwidth(center_distance), 0.0008);

    float fill_r = coverage_at(vUv - subpixel_step, 0.0, 1.0);
    float fill_g = coverage_at(vUv, 0.0, 1.0);
    float fill_b = coverage_at(vUv + subpixel_step, 0.0, 1.0);
    vec3 fill_coverage = vec3(fill_r, fill_g, fill_b);
    float fill_alpha = max(max(fill_r, fill_g), fill_b);

    float outline_alpha = max(coverage_from_distance(center_distance + pixel_width * 1.35, pixel_width), fill_alpha);
    float outline_only = max(outline_alpha - fill_alpha, 0.0) * 0.96;

    vec2 shadow_uv = vUv + dFdx(vUv) * 1.15 + dFdy(vUv) * 1.35;
    float shadow_distance = signed_distance_at(shadow_uv);
    float shadow_alpha = coverage_from_distance(shadow_distance + pixel_width * 2.4, pixel_width * 2.4) * 0.42;
    shadow_alpha *= 1.0 - fill_alpha;

    vec3 premultiplied_fill = vColor.rgb * fill_coverage * vColor.a;
    float alpha = max(max(shadow_alpha, outline_only), fill_alpha * vColor.a);
    oColor = vec4(premultiplied_fill, alpha);
}
)";

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, "sdf text vertex");
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, "sdf text fragment");
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        text.failed = true;
        return false;
    }

    text.program = glCreateProgram();
    glAttachShader(text.program, vertex_shader);
    glAttachShader(text.program, fragment_shader);
    glLinkProgram(text.program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(text.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::cerr << "SDF text shader link failed: " << shader_log(text.program, true) << '\n';
        glDeleteProgram(text.program);
        text.program = 0;
        text.failed = true;
        return false;
    }

    std::vector<unsigned char> pixels;
    if (!load_ppm(text.atlas_path, text.atlas_width, text.atlas_height, pixels)) {
        std::cerr << "Could not load MSDF font atlas: " << text.atlas_path.string() << '\n';
        text.failed = true;
        return false;
    }

    glGenTextures(1, &text.texture);
    glBindTexture(GL_TEXTURE_2D, text.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB8,
        text.atlas_width,
        text.atlas_height,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &text.vao);
    glGenBuffers(1, &text.vbo);
    text.atlas_uniform = glGetUniformLocation(text.program, "uAtlas");
    text.initialized = text.vao != 0 && text.vbo != 0 && text.texture != 0;
    text.failed = !text.initialized;
    return text.initialized;
}

const SdfGlyph* glyph_for_char(const char ch) {
    const SdfTextState& text = state();
    const auto found = text.glyphs.find(static_cast<unsigned char>(ch));
    if (found != text.glyphs.end()) {
        return &found->second;
    }
    const auto fallback = text.glyphs.find(static_cast<int>('?'));
    return fallback == text.glyphs.end() ? nullptr : &fallback->second;
}

void append_quad(std::vector<SdfVertex>& vertices, const std::array<SdfVertex, 4>& quad) {
    vertices.push_back(quad[0]);
    vertices.push_back(quad[1]);
    vertices.push_back(quad[2]);
    vertices.push_back(quad[0]);
    vertices.push_back(quad[2]);
    vertices.push_back(quad[3]);
}

bool inside_clip(const SdfTextClip& clip, const float x, const float y, const float w, const float h) {
    if (!clip.enabled) {
        return true;
    }
    return x + w >= clip.x && x <= clip.x + clip.width && y + h >= clip.y && y <= clip.y + clip.height;
}

float text_screen_to_ndc_x(const float screen_x, const int width) {
    return (2.0F * screen_x / static_cast<float>(width)) - 1.0F;
}

float text_screen_to_ndc_y(const float screen_y, const int height) {
    return 1.0F - (2.0F * screen_y / static_cast<float>(height));
}

} // namespace

bool sdf_text_ready() {
    return ensure_metrics_loaded();
}

void sdf_text_shutdown() {
    SdfTextState& text = state();
    if (text.texture != 0) {
        glDeleteTextures(1, &text.texture);
    }
    if (text.vbo != 0) {
        glDeleteBuffers(1, &text.vbo);
    }
    if (text.vao != 0) {
        glDeleteVertexArrays(1, &text.vao);
    }
    if (text.program != 0) {
        glDeleteProgram(text.program);
    }
    text = {};
}

bool load_sdf_text_font(const std::string& metrics_path) {
    SdfTextState& text = state();
    std::ifstream input(metrics_path);
    if (!input) {
        return false;
    }

    SdfTextState loaded;
    loaded.metrics_path = metrics_path;
    std::string token;
    int version = 0;
    if (!read_token(input, token) || token != "msdf_font" || !(input >> version) || version != 1) {
        return false;
    }
    std::string atlas_name;
    if (!read_token(input, token) || token != "atlas" ||
        !(input >> atlas_name >> loaded.atlas_width >> loaded.atlas_height)) {
        return false;
    }
    loaded.atlas_path = loaded.metrics_path.parent_path() / atlas_name;
    if (!read_token(input, token) || token != "size" || !(input >> loaded.font_size)) {
        return false;
    }
    if (!read_token(input, token) || token != "spread" || !(input >> loaded.spread)) {
        return false;
    }
    if (!read_token(input, token) || token != "ascent" || !(input >> loaded.ascent)) {
        return false;
    }
    if (!read_token(input, token) || token != "descent" || !(input >> loaded.descent)) {
        return false;
    }
    if (!read_token(input, token) || token != "line_height" || !(input >> loaded.line_height)) {
        return false;
    }
    std::size_t glyph_count = 0;
    if (!read_token(input, token) || token != "glyphs" || !(input >> glyph_count)) {
        return false;
    }
    for (std::size_t i = 0; i < glyph_count; ++i) {
        SdfGlyph glyph;
        if (!read_token(input, token) || token != "glyph" ||
            !(input >> glyph.code >> glyph.x >> glyph.y >> glyph.width >> glyph.height >>
                glyph.x_offset >> glyph.y_offset >> glyph.advance)) {
            return false;
        }
        loaded.glyphs[glyph.code] = glyph;
    }
    loaded.metrics_loaded = true;
    loaded.initialized = false;
    loaded.failed = false;
    if (text.initialized || text.program != 0 || text.texture != 0) {
        sdf_text_shutdown();
    }
    text = std::move(loaded);
    return true;
}

SdfTextMetrics measure_sdf_text(const std::string& text_value, const float pixel_size) {
    SdfTextMetrics metrics;
    if (!ensure_metrics_loaded() || pixel_size <= 0.0F) {
        return metrics;
    }
    const SdfTextState& text = state();
    const float scale = pixel_size / std::max(text.font_size, 1.0F);
    const float line_height = text.line_height * scale;
    const SdfGlyph* space = glyph_for_char(' ');
    const float tab_advance = (space != nullptr ? space->advance : text.font_size * 0.5F) * scale * 4.0F;
    float line_width = 0.0F;
    metrics.lines = 1;
    metrics.height = line_height;
    for (const char ch : text_value) {
        if (ch == '\n') {
            metrics.width = std::max(metrics.width, line_width);
            line_width = 0.0F;
            metrics.height += line_height;
            ++metrics.lines;
            continue;
        }
        if (ch == '\t') {
            line_width += tab_advance;
            continue;
        }
        const SdfGlyph* glyph = glyph_for_char(ch);
        if (glyph != nullptr) {
            line_width += glyph->advance * scale;
        }
    }
    metrics.width = std::max(metrics.width, line_width);
    return metrics;
}

bool draw_sdf_text(
    const std::string& text_value,
    const float x,
    const float y,
    const float pixel_size,
    const int screen_width,
    const int screen_height,
    const SdfTextColor color,
    const SdfTextClip clip
) {
    if (text_value.empty() || pixel_size <= 0.0F || screen_width <= 0 || screen_height <= 0) {
        return true;
    }
    if (!ensure_renderer()) {
        return false;
    }

    const SdfTextState& text = state();
    const float scale = pixel_size / std::max(text.font_size, 1.0F);
    const float line_height = text.line_height * scale;
    const SdfGlyph* space = glyph_for_char(' ');
    const float tab_advance = (space != nullptr ? space->advance : text.font_size * 0.5F) * scale * 4.0F;

    std::vector<SdfVertex> vertices;
    vertices.reserve(text_value.size() * 6U);
    float cursor_x = x;
    float cursor_y = y;
    for (const char ch : text_value) {
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += line_height;
            continue;
        }
        if (ch == '\t') {
            cursor_x += tab_advance;
            continue;
        }

        const SdfGlyph* glyph = glyph_for_char(ch);
        if (glyph == nullptr) {
            continue;
        }
        const float gx = cursor_x + glyph->x_offset * scale;
        const float gy = cursor_y + glyph->y_offset * scale;
        const float gw = glyph->width * scale;
        const float gh = glyph->height * scale;
        cursor_x += glyph->advance * scale;
        if (!inside_clip(clip, gx, gy, gw, gh)) {
            continue;
        }

        const float u0 = glyph->x / static_cast<float>(text.atlas_width);
        const float v0 = glyph->y / static_cast<float>(text.atlas_height);
        const float u1 = (glyph->x + glyph->width) / static_cast<float>(text.atlas_width);
        const float v1 = (glyph->y + glyph->height) / static_cast<float>(text.atlas_height);
        const float x0 = text_screen_to_ndc_x(gx, screen_width);
        const float y0 = text_screen_to_ndc_y(gy, screen_height);
        const float x1 = text_screen_to_ndc_x(gx + gw, screen_width);
        const float y1 = text_screen_to_ndc_y(gy + gh, screen_height);
        append_quad(vertices, {{
            SdfVertex{x0, y0, u0, v0, color.r, color.g, color.b, color.a},
            SdfVertex{x1, y0, u1, v0, color.r, color.g, color.b, color.a},
            SdfVertex{x1, y1, u1, v1, color.r, color.g, color.b, color.a},
            SdfVertex{x0, y1, u0, v1, color.r, color.g, color.b, color.a},
        }});
    }
    if (vertices.empty()) {
        return true;
    }

    GLint previous_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);

    glUseProgram(text.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text.texture);
    glUniform1i(text.atlas_uniform, 0);
    glBindVertexArray(text.vao);
    glBindBuffer(GL_ARRAY_BUFFER, text.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(SdfVertex)),
        vertices.data(),
        GL_STREAM_DRAW
    );
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), reinterpret_cast<const void*>(offsetof(SdfVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), reinterpret_cast<const void*>(offsetof(SdfVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SdfVertex), reinterpret_cast<const void*>(offsetof(SdfVertex, r)));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glUseProgram(static_cast<GLuint>(previous_program));
    return true;
}

} // namespace undecedent
