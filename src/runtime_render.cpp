#include "undecedent/runtime_render.hpp"

#include "undecedent/core_draw.hpp"
#include "undecedent/runtime_visibility.hpp"
#include "undecedent/shadow_atlas.hpp"
#include "undecedent/shadow_cache.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace undecedent {
namespace {

struct Mat4 {
    std::array<float, 16> m{};
};

struct SunShadowCascade {
    Mat4 matrix{};
    std::array<float, 4> rect{};
    float split_distance = 0.0F;
};

using SunShadowCascades = std::array<SunShadowCascade, kSunShadowCascadeCount>;

struct ForwardRuntimeProgram {
    GLuint program = 0;
    GLuint vao = 0;
    GLint mvp = -1;
    GLint material_albedo = -1;
    bool failed = false;
};

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
        std::cerr << "Runtime shader compile failed (" << label << "): "
                  << shader_log(shader, false) << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

ForwardRuntimeProgram& forward_runtime_program() {
    static ForwardRuntimeProgram renderer;
    return renderer;
}

bool ensure_forward_runtime_program() {
    ForwardRuntimeProgram& renderer = forward_runtime_program();
    if (renderer.program != 0 && renderer.vao != 0) {
        return true;
    }
    if (renderer.failed) {
        return false;
    }

    static constexpr const char* vertex_source = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in float aMaterialSlot;
uniform mat4 uMvp;
out vec3 vColor;
out vec2 vTexCoord;
flat out int vMaterialSlot;
void main() {
    vColor = aColor;
    vTexCoord = aTexCoord;
    vMaterialSlot = int(clamp(floor(aMaterialSlot + 0.5), 0.0, 7.0));
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";
    static constexpr const char* fragment_source = R"(
#version 430 core
in vec3 vColor;
in vec2 vTexCoord;
flat in int vMaterialSlot;
uniform sampler2DArray uMaterialAlbedo;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 texel = texture(uMaterialAlbedo, vec3(vTexCoord, float(vMaterialSlot))).rgb;
    oColor = vec4(vColor * texel, 1.0);
}
)";

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, "runtime forward");
    if (vertex_shader == 0) {
        renderer.failed = true;
        return false;
    }
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, "runtime forward");
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        renderer.failed = true;
        return false;
    }

    renderer.program = glCreateProgram();
    glAttachShader(renderer.program, vertex_shader);
    glAttachShader(renderer.program, fragment_shader);
    glBindAttribLocation(renderer.program, 0, "aPosition");
    glBindAttribLocation(renderer.program, 1, "aColor");
    glBindAttribLocation(renderer.program, 2, "aTexCoord");
    glBindAttribLocation(renderer.program, 3, "aMaterialSlot");
    glLinkProgram(renderer.program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(renderer.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::cerr << "Runtime shader link failed: " << shader_log(renderer.program, true) << '\n';
        glDeleteProgram(renderer.program);
        renderer.program = 0;
        renderer.failed = true;
        return false;
    }

    glGenVertexArrays(1, &renderer.vao);
    renderer.mvp = glGetUniformLocation(renderer.program, "uMvp");
    renderer.material_albedo = glGetUniformLocation(renderer.program, "uMaterialAlbedo");
    renderer.failed = renderer.vao == 0;
    return !renderer.failed;
}

std::array<std::array<float, 16>, kSunShadowCascadeCount> sun_cascade_matrices(
    const SunShadowCascades& cascades
) {
    std::array<std::array<float, 16>, kSunShadowCascadeCount> matrices{};
    for (std::size_t i = 0; i < cascades.size(); ++i) {
        matrices[i] = cascades[i].matrix.m;
    }
    return matrices;
}

void cache_sun_cascades(DeferredRenderer& renderer, const SunShadowCascades& cascades) {
    for (std::size_t i = 0; i < cascades.size(); ++i) {
        renderer.cached_sun_shadow_matrices[i] = cascades[i].matrix.m;
        renderer.cached_sun_shadow_rects[i] = cascades[i].rect;
        renderer.cached_sun_shadow_splits[i] = cascades[i].split_distance;
    }
}

void restore_cached_sun_cascades(DeferredRenderer& renderer, SunShadowCascades& cascades) {
    for (std::size_t i = 0; i < cascades.size(); ++i) {
        cascades[i].matrix.m = renderer.cached_sun_shadow_matrices[i];
        cascades[i].rect = renderer.cached_sun_shadow_rects[i];
        cascades[i].split_distance = renderer.cached_sun_shadow_splits[i];
    }
}

float dot3(const Vec3 a, const Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

Vec3 sub3(const Vec3 a, const Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 add3(const Vec3 a, const Vec3 b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scale3(const Vec3 value, const float scale) {
    return Vec3{value.x * scale, value.y * scale, value.z * scale};
}

Vec3 cross3(const Vec3 a, const Vec3 b) {
    return Vec3{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

Vec3 normalize3(const Vec3 value, const Vec3 fallback = Vec3{0.0F, 1.0F, 0.0F}) {
    const float length = std::sqrt(dot3(value, value));
    if (!std::isfinite(length) || length <= 0.0001F) {
        return fallback;
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

std::array<float, 4> transform_point(const Mat4& matrix, const Vec3 point) {
    return {
        (matrix.m[0] * point.x) + (matrix.m[4] * point.y) + (matrix.m[8] * point.z) + matrix.m[12],
        (matrix.m[1] * point.x) + (matrix.m[5] * point.y) + (matrix.m[9] * point.z) + matrix.m[13],
        (matrix.m[2] * point.x) + (matrix.m[6] * point.y) + (matrix.m[10] * point.z) + matrix.m[14],
        (matrix.m[3] * point.x) + (matrix.m[7] * point.y) + (matrix.m[11] * point.z) + matrix.m[15],
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (int k = 0; k < 4; ++k) {
                value += a.m[static_cast<std::size_t>(k * 4 + row)] *
                    b.m[static_cast<std::size_t>(column * 4 + k)];
            }
            out.m[static_cast<std::size_t>(column * 4 + row)] = value;
        }
    }
    return out;
}

Mat4 perspective_matrix(const float fov_y_radians, const float aspect, const float near_plane, const float far_plane) {
    const float f = 1.0F / std::tan(fov_y_radians * 0.5F);
    Mat4 out{};
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    out.m[11] = -1.0F;
    out.m[14] = (2.0F * far_plane * near_plane) / (near_plane - far_plane);
    return out;
}

Mat4 orthographic_matrix(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float near_plane,
    const float far_plane
) {
    Mat4 out{};
    out.m[0] = 2.0F / (right - left);
    out.m[5] = 2.0F / (top - bottom);
    out.m[10] = -2.0F / (far_plane - near_plane);
    out.m[12] = -(right + left) / (right - left);
    out.m[13] = -(top + bottom) / (top - bottom);
    out.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    out.m[15] = 1.0F;
    return out;
}

Mat4 look_at_matrix(const Vec3 eye, const Vec3 target, const Vec3 up_hint) {
    const Vec3 f = normalize3(sub3(target, eye), Vec3{0.0F, 0.0F, -1.0F});
    Vec3 s = normalize3(cross3(f, up_hint), Vec3{1.0F, 0.0F, 0.0F});
    Vec3 u = cross3(s, f);
    Mat4 out{};
    out.m[0] = s.x;
    out.m[1] = u.x;
    out.m[2] = -f.x;
    out.m[4] = s.y;
    out.m[5] = u.y;
    out.m[6] = -f.y;
    out.m[8] = s.z;
    out.m[9] = u.z;
    out.m[10] = -f.z;
    out.m[12] = -dot3(s, eye);
    out.m[13] = -dot3(u, eye);
    out.m[14] = dot3(f, eye);
    out.m[15] = 1.0F;
    return out;
}

Mat4 translation_matrix(const Vec3 offset) {
    Mat4 out{};
    out.m[0] = 1.0F;
    out.m[5] = 1.0F;
    out.m[10] = 1.0F;
    out.m[12] = offset.x;
    out.m[13] = offset.y;
    out.m[14] = offset.z;
    out.m[15] = 1.0F;
    return out;
}

Mat4 rotation_x_matrix(const float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    Mat4 out{};
    out.m[0] = 1.0F;
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    out.m[15] = 1.0F;
    return out;
}

Mat4 rotation_y_matrix(const float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    Mat4 out{};
    out.m[0] = c;
    out.m[2] = -s;
    out.m[5] = 1.0F;
    out.m[8] = s;
    out.m[10] = c;
    out.m[15] = 1.0F;
    return out;
}

Vec3 camera_forward(const GameCamera& camera) {
    const float forward_flat = std::cos(camera.pitch);
    return normalize3(
        Vec3{
            -std::sin(camera.yaw) * forward_flat,
            std::sin(camera.pitch),
            -std::cos(camera.yaw) * forward_flat,
        },
        Vec3{0.0F, 0.0F, -1.0F}
    );
}

Vec3 camera_right(const GameCamera& camera) {
    return normalize3(
        Vec3{std::cos(camera.yaw), 0.0F, -std::sin(camera.yaw)},
        Vec3{1.0F, 0.0F, 0.0F}
    );
}

Vec3 camera_up(const GameCamera& camera) {
    return normalize3(cross3(camera_right(camera), camera_forward(camera)), Vec3{0.0F, 1.0F, 0.0F});
}

Mat4 game_view_projection_matrix(
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
    const float fov_y_radians = config.fov_y_degrees * 3.14159265F / 180.0F;
    const Mat4 projection = perspective_matrix(fov_y_radians, aspect, config.near_plane, config.far_plane);
    const Mat4 view =
        multiply(
            multiply(rotation_x_matrix(-camera.pitch), rotation_y_matrix(-camera.yaw)),
            translation_matrix(Vec3{-camera.x, -camera.y, -camera.z})
        );
    return multiply(projection, view);
}

void draw_textured_runtime_arrays(
    const RuntimeRenderCache& render_cache,
    const Mat4& view_projection,
    const GLuint material_texture_array,
    const GLint first_vertex,
    const GLsizei vertex_count
) {
    if (material_texture_array == 0 || !ensure_forward_runtime_program()) {
        core_draw_colored_arrays(
            GL_TRIANGLES,
            render_cache.vertex_buffer,
            first_vertex,
            vertex_count,
            sizeof(RuntimeRenderVertex),
            offsetof(RuntimeRenderVertex, x),
            offsetof(RuntimeRenderVertex, r),
            3
        );
        return;
    }

    ForwardRuntimeProgram& renderer = forward_runtime_program();
    glUseProgram(renderer.program);
    glUniformMatrix4fv(renderer.mvp, 1, GL_FALSE, view_projection.m.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, material_texture_array);
    glUniform1i(renderer.material_albedo, 0);
    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
    );
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, r))
    );
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, u))
    );
    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, material_slot))
    );
    glDrawArrays(GL_TRIANGLES, first_vertex, vertex_count);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glUseProgram(0);
}

std::array<Mat4, 6> point_shadow_matrices(const PointLight& light) {
    const Mat4 projection = perspective_matrix(3.14159265F * 0.5F, 1.0F, 1.0F, std::max(light.radius, 1.0F));
    const Vec3 p = light.position;
    return {{
        multiply(projection, look_at_matrix(p, add3(p, Vec3{1.0F, 0.0F, 0.0F}), Vec3{0.0F, -1.0F, 0.0F})),
        multiply(projection, look_at_matrix(p, add3(p, Vec3{-1.0F, 0.0F, 0.0F}), Vec3{0.0F, -1.0F, 0.0F})),
        multiply(projection, look_at_matrix(p, add3(p, Vec3{0.0F, 1.0F, 0.0F}), Vec3{0.0F, 0.0F, 1.0F})),
        multiply(projection, look_at_matrix(p, add3(p, Vec3{0.0F, -1.0F, 0.0F}), Vec3{0.0F, 0.0F, -1.0F})),
        multiply(projection, look_at_matrix(p, add3(p, Vec3{0.0F, 0.0F, 1.0F}), Vec3{0.0F, -1.0F, 0.0F})),
        multiply(projection, look_at_matrix(p, add3(p, Vec3{0.0F, 0.0F, -1.0F}), Vec3{0.0F, -1.0F, 0.0F})),
    }};
}

std::array<Vec3, 8> frustum_slice_corners(
    const GameCamera& camera,
    const GameRenderConfig& config,
    const int width,
    const int height,
    const float near_distance,
    const float far_distance
) {
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
    const float fov_y_radians = config.fov_y_degrees * 3.14159265F / 180.0F;
    const float tan_y = std::tan(fov_y_radians * 0.5F);
    const float tan_x = tan_y * aspect;
    const Vec3 position{camera.x, camera.y, camera.z};
    const Vec3 forward = camera_forward(camera);
    const Vec3 right = camera_right(camera);
    const Vec3 up = camera_up(camera);
    std::array<Vec3, 8> corners{};
    int index = 0;
    for (const float distance : {near_distance, far_distance}) {
        const Vec3 center = add3(position, scale3(forward, distance));
        const Vec3 x = scale3(right, distance * tan_x);
        const Vec3 y = scale3(up, distance * tan_y);
        corners[static_cast<std::size_t>(index++)] = add3(add3(center, x), y);
        corners[static_cast<std::size_t>(index++)] = add3(sub3(center, x), y);
        corners[static_cast<std::size_t>(index++)] = sub3(sub3(center, x), y);
        corners[static_cast<std::size_t>(index++)] = add3(sub3(center, y), x);
    }
    return corners;
}

SunShadowCascades sun_shadow_cascades(
    const WorldLighting& lighting,
    const GameCamera& camera,
    const GameRenderConfig& config,
    const int width,
    const int height
) {
    constexpr std::array<float, kSunShadowCascadeCount> splits{2048.0F, 8192.0F, 32768.0F, 131072.0F};
    const Vec3 sun_direction = normalize3(lighting.sun_direction, WorldLighting{}.sun_direction);
    const Vec3 up = std::abs(sun_direction.y) > 0.92F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{0.0F, 1.0F, 0.0F};
    const Vec3 light_forward = sun_direction;
    const Vec3 light_right = normalize3(cross3(light_forward, up), Vec3{1.0F, 0.0F, 0.0F});
    const Vec3 light_up = cross3(light_right, light_forward);
    SunShadowCascades cascades{};
    float cascade_near = std::max(config.near_plane, 1.0F);
    for (int cascade_index = 0; cascade_index < kSunShadowCascadeCount; ++cascade_index) {
        const float cascade_far = splits[static_cast<std::size_t>(cascade_index)];
        const std::array<Vec3, 8> corners =
            frustum_slice_corners(camera, config, width, height, cascade_near, cascade_far);
        Vec3 center{};
        for (const Vec3 corner : corners) {
            center = add3(center, corner);
        }
        center = scale3(center, 1.0F / static_cast<float>(corners.size()));

        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float min_z = std::numeric_limits<float>::max();
        float max_x = -std::numeric_limits<float>::max();
        float max_y = -std::numeric_limits<float>::max();
        float max_z = -std::numeric_limits<float>::max();
        for (const Vec3 corner : corners) {
            const Vec3 relative = sub3(corner, center);
            const float x = dot3(relative, light_right);
            const float y = dot3(relative, light_up);
            const float z = dot3(relative, light_forward);
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            min_z = std::min(min_z, z);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
            max_z = std::max(max_z, z);
        }

        const float span = std::max({max_x - min_x, max_y - min_y, max_z - min_z, 1.0F});
        const float padding = std::max(64.0F, span * 0.025F);
        const float extent_x = std::max((max_x - min_x) + (padding * 2.0F), 1.0F);
        const float extent_y = std::max((max_y - min_y) + (padding * 2.0F), 1.0F);
        const float world_texel = std::max(std::max(extent_x, extent_y) / static_cast<float>(kSunShadowResolution), 0.001F);
        const float center_right = dot3(center, light_right);
        const float center_up = dot3(center, light_up);
        const float snapped_right = std::round(center_right / world_texel) * world_texel;
        const float snapped_up = std::round(center_up / world_texel) * world_texel;
        center = add3(
            center,
            add3(
                scale3(light_right, snapped_right - center_right),
                scale3(light_up, snapped_up - center_up)
            )
        );
        const Vec3 eye = add3(center, scale3(light_forward, min_z - padding));
        const Vec3 target = add3(eye, light_forward);
        const float depth_extent = std::max((max_z - min_z) + (padding * 2.0F), 2.0F);
        const Mat4 projection = orthographic_matrix(
            min_x - padding,
            max_x + padding,
            min_y - padding,
            max_y + padding,
            1.0F,
            depth_extent
        );
        cascades[static_cast<std::size_t>(cascade_index)].matrix =
            multiply(projection, look_at_matrix(eye, target, up));
        const int tile_x = (cascade_index % kSunShadowCascadeGrid) * kSunShadowResolution;
        const int tile_y = (cascade_index / kSunShadowCascadeGrid) * kSunShadowResolution;
        const float inv_atlas = 1.0F / static_cast<float>(kSunShadowAtlasSize);
        cascades[static_cast<std::size_t>(cascade_index)].rect = {
            static_cast<float>(tile_x) * inv_atlas,
            static_cast<float>(tile_y) * inv_atlas,
            static_cast<float>(kSunShadowResolution) * inv_atlas,
            static_cast<float>(kSunShadowResolution) * inv_atlas,
        };
        cascades[static_cast<std::size_t>(cascade_index)].split_distance = cascade_far;
        cascade_near = cascade_far;
    }
    return cascades;
}

float point_light_priority(const PointLight& light, const Vec3 camera_position) {
    const float dx = light.position.x - camera_position.x;
    const float dy = light.position.y - camera_position.y;
    const float dz = light.position.z - camera_position.z;
    const float distance = std::max(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)), 1.0F);
    return std::max(light.intensity, 0.0F) * std::max(light.radius, 0.0F) / distance;
}

void draw_shadow_sector_ranges(const RuntimeRenderCache& render_cache, const std::vector<int>& sector_ids) {
    for (const int sector_id : sector_ids) {
        if (sector_id < 0 || sector_id >= static_cast<int>(render_cache.sector_ranges.size())) {
            continue;
        }

        const RuntimeRenderRange range = render_cache.sector_ranges[static_cast<std::size_t>(sector_id)];
        if (range.vertex_count <= 0) {
            continue;
        }
        glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
    }
}

std::vector<int>& point_shadow_sectors_for_light(
    DeferredRenderer& renderer,
    const RuntimeWorld& world,
    const PointLight& light
) {
    renderer.shadow_sector_ids.clear();
    const float radius = std::max(light.radius, 1.0F);
    const RuntimeBounds2 bounds{
        light.position.x - radius,
        light.position.z - radius,
        light.position.x + radius,
        light.position.z + radius,
    };

    const std::vector<int> candidates = sectors_in_bounds(world, bounds);
    renderer.shadow_sector_ids.reserve(candidates.size());
    const float min_y = light.position.y - radius;
    const float max_y = light.position.y + radius;
    for (const int sector_id : candidates) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            continue;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (sector.max_ceiling_height < min_y || sector.min_floor_height > max_y) {
            continue;
        }
        renderer.shadow_sector_ids.push_back(sector_id);
    }
    return renderer.shadow_sector_ids;
}

bool sector_intersects_sun_cascade(const RuntimeSector& sector, const SunShadowCascade& cascade) {
    constexpr float padding = 0.035F;
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    float max_y = -std::numeric_limits<float>::max();
    float max_z = -std::numeric_limits<float>::max();

    for (const float x : {sector.bounds.min_x, sector.bounds.max_x}) {
        for (const float y : {sector.min_floor_height, sector.max_ceiling_height}) {
            for (const float z : {sector.bounds.min_y, sector.bounds.max_y}) {
                const std::array<float, 4> clip = transform_point(cascade.matrix, Vec3{x, y, z});
                if (std::abs(clip[3]) <= 0.00001F) {
                    return true;
                }
                const float inv_w = 1.0F / clip[3];
                const float ndc_x = clip[0] * inv_w;
                const float ndc_y = clip[1] * inv_w;
                const float ndc_z = clip[2] * inv_w;
                min_x = std::min(min_x, ndc_x);
                min_y = std::min(min_y, ndc_y);
                min_z = std::min(min_z, ndc_z);
                max_x = std::max(max_x, ndc_x);
                max_y = std::max(max_y, ndc_y);
                max_z = std::max(max_z, ndc_z);
            }
        }
    }

    return max_x >= -1.0F - padding &&
        min_x <= 1.0F + padding &&
        max_y >= -1.0F - padding &&
        min_y <= 1.0F + padding &&
        max_z >= -1.0F - padding &&
        min_z <= 1.0F + padding;
}

std::vector<int>& sun_shadow_sectors_for_cascade(
    DeferredRenderer& renderer,
    const RuntimeWorld& world,
    const SunShadowCascade& cascade
) {
    renderer.shadow_sector_ids.clear();
    renderer.shadow_sector_ids.reserve(world.sectors.size());
    for (int sector_id = 0; sector_id < static_cast<int>(world.sectors.size()); ++sector_id) {
        if (sector_intersects_sun_cascade(world.sectors[static_cast<std::size_t>(sector_id)], cascade)) {
            renderer.shadow_sector_ids.push_back(sector_id);
        }
    }
    return renderer.shadow_sector_ids;
}

} // namespace

void set_game_projection(
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    core_set_mvp(game_view_projection_matrix(width, height, camera, config).m);
}

void draw_player_spawn_3d(
    const PlayerSpawn& spawn,
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    if (!spawn.set || width <= 0 || height <= 0) {
        return;
    }

    set_game_projection(width, height, camera, config);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_color4f(1.0F, 0.82F, 0.25F, 0.95F);
    core_set_line_width(2.0F);
    const float feet = spawn.position.y - config.player_eye_height;
    const float head = feet + config.player_height;
    const float x = spawn.position.x;
    const float z = spawn.position.z;
    core_begin(GL_LINES);
    core_vertex3f(x, feet, z);
    core_vertex3f(x, head, z);
    core_vertex3f(x - config.player_radius, feet, z);
    core_vertex3f(x + config.player_radius, feet, z);
    core_vertex3f(x, feet, z - config.player_radius);
    core_vertex3f(x, feet, z + config.player_radius);
    core_vertex3f(x, spawn.position.y, z);
    core_vertex3f(x - std::sin(spawn.yaw) * 24.0F, spawn.position.y, z - std::cos(spawn.yaw) * 24.0F);
    core_end();
    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_point_lights_3d(
    const std::vector<PointLight>& point_lights,
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    if (point_lights.empty() || width <= 0 || height <= 0) {
        return;
    }

    set_game_projection(width, height, camera, config);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_color4f(1.0F, 0.86F, 0.35F, 0.92F);
    core_set_line_width(2.0F);
    core_begin(GL_LINES);
    for (const PointLight& light : point_lights) {
        const float x = light.position.x;
        const float y = light.position.y;
        const float z = light.position.z;
        constexpr float s = 8.0F;
        core_vertex3f(x - s, y, z);
        core_vertex3f(x + s, y, z);
        core_vertex3f(x, y - s, z);
        core_vertex3f(x, y + s, z);
        core_vertex3f(x, y, z - s);
        core_vertex3f(x, y, z + s);
        core_vertex3f(x - s, y - s, z);
        core_vertex3f(x + s, y + s, z);
        core_vertex3f(x - s, y + s, z);
        core_vertex3f(x + s, y - s, z);
    }
    core_end();
    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

bool render_vsm_shadow_maps(
    DeferredRenderer& renderer,
    const RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const std::vector<PointLight>& lights,
    const PackedPointShadowAtlas& atlas,
    const WorldLighting& world_lighting,
    const GameCamera& camera,
    const GameRenderConfig& config,
    SunShadowCascades& sun_cascades
) {
    renderer.last_shadow_ms = 0.0;
    renderer.last_point_shadow_lights_rendered = 0;
    renderer.last_point_shadow_faces_rendered = 0;
    renderer.last_sun_shadow_cascades_rendered = 0;
    renderer.last_shadow_cache_hits = 0;
    renderer.last_shadow_cache_misses = 0;
    const bool render_point_shadows = config.vsm_shadows_enabled && atlas.shadowed_lights > 0;
    const bool render_sun_shadow =
        config.vsm_shadows_enabled &&
        config.csm_shadows_enabled &&
        world_lighting.sun_enabled &&
        world_lighting.sun_intensity > 0.0F;
    if (renderer.shadows_disabled ||
        renderer.shadow_framebuffer == 0 ||
        renderer.point_shadow_texture == 0 ||
        renderer.sun_shadow_texture == 0 ||
        renderer.point_shadow_program == 0 ||
        renderer.sun_shadow_program == 0 ||
        render_cache.vertex_buffer == 0 ||
        render_cache.total_vertices <= 0 ||
        (!render_point_shadows && !render_sun_shadow)) {
        return false;
    }

    const std::uint64_t shadow_revision = std::max<std::uint64_t>(render_cache.shadow_revision, 1);
    bool point_layout_changed = false;
    bool point_shadow_dirty = false;
    int active_point_shadows = 0;
    int point_cache_hits = 0;
    int point_cache_misses = 0;
    if (render_point_shadows) {
        for (const PointShadowAtlasEntry& entry : atlas.entries) {
            if (!entry.shadowed || entry.light_index < 0 || entry.light_index >= static_cast<int>(lights.size())) {
                continue;
            }
            ++active_point_shadows;
            const PointLight& light = lights[static_cast<std::size_t>(entry.light_index)];
            const std::uint64_t key = point_shadow_cache_key(light, entry.light_index);
            const auto it = renderer.point_shadow_cache.find(key);
            const bool matches = it != renderer.point_shadow_cache.end() &&
                point_shadow_cache_matches(it->second, light, entry.light_index, entry, shadow_revision);
            if (it != renderer.point_shadow_cache.end() &&
                it->second.valid &&
                !shadow_atlas_faces_equal(it->second.faces, entry.faces)) {
                point_layout_changed = true;
            }
            if (matches) {
                ++point_cache_hits;
            } else {
                ++point_cache_misses;
                point_shadow_dirty = true;
            }
        }
        if (point_layout_changed) {
            point_cache_hits = 0;
            point_cache_misses = active_point_shadows;
        }
        renderer.last_shadow_cache_hits += point_cache_hits;
        renderer.last_shadow_cache_misses += point_cache_misses;
    }

    bool sun_shadow_dirty = false;
    if (render_sun_shadow) {
        const std::array<std::array<float, 16>, kSunShadowCascadeCount> matrices = sun_cascade_matrices(sun_cascades);
        const Vec3 sun_direction = normalize3(world_lighting.sun_direction, WorldLighting{}.sun_direction);
        if (sun_shadow_cache_matches(
                renderer.sun_shadow_cache,
                shadow_revision,
                sun_direction,
                renderer.width,
                renderer.height,
                config.fov_y_degrees,
                config.near_plane,
                config.far_plane,
                camera.yaw,
                camera.pitch,
                matrices)) {
            restore_cached_sun_cascades(renderer, sun_cascades);
            ++renderer.last_shadow_cache_hits;
        } else {
            sun_shadow_dirty = true;
            ++renderer.last_shadow_cache_misses;
        }
    }

    if (!point_shadow_dirty && !sun_shadow_dirty) {
        return true;
    }

    const auto shadow_start = std::chrono::steady_clock::now();

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.shadow_framebuffer);
    glBindVertexArray(renderer.shadow_vao);
    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
    );
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (render_point_shadows && point_shadow_dirty) {
        glViewport(0, 0, kPointShadowAtlasSize, kPointShadowAtlasSize);
        glBindRenderbuffer(GL_RENDERBUFFER, renderer.point_shadow_depth_renderbuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer.point_shadow_depth_renderbuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer.point_shadow_texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            renderer.shadows_disabled = true;
            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glUseProgram(0);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        glClearColor(1.0F, 1.0F, 0.0F, 0.0F);
        if (point_layout_changed) {
            glDisable(GL_SCISSOR_TEST);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderer.point_shadow_cache.clear();
        }
        glUseProgram(renderer.point_shadow_program);
        glEnable(GL_SCISSOR_TEST);
        for (const PointShadowAtlasEntry& entry : atlas.entries) {
            if (!entry.shadowed || entry.light_index < 0 || entry.light_index >= static_cast<int>(lights.size())) {
                continue;
            }
            const PointLight& light = lights[static_cast<std::size_t>(entry.light_index)];
            const std::uint64_t key = point_shadow_cache_key(light, entry.light_index);
            const auto it = renderer.point_shadow_cache.find(key);
            const bool cache_hit = !point_layout_changed &&
                it != renderer.point_shadow_cache.end() &&
                point_shadow_cache_matches(it->second, light, entry.light_index, entry, shadow_revision);
            if (cache_hit) {
                continue;
            }
            ++renderer.last_point_shadow_lights_rendered;
            const std::array<Mat4, 6> matrices = point_shadow_matrices(light);
            glUniform3f(
                renderer.point_shadow_uniforms.light_position,
                light.position.x,
                light.position.y,
                light.position.z
            );
            glUniform1f(renderer.point_shadow_uniforms.light_radius, std::max(light.radius, 1.0F));
            const std::vector<int>& shadow_sector_ids = point_shadow_sectors_for_light(renderer, world, light);
            for (int face = 0; face < 6; ++face) {
                const ShadowAtlasRect rect = entry.faces[static_cast<std::size_t>(face)];
                glViewport(rect.x, rect.y, rect.size, rect.size);
                glScissor(rect.x, rect.y, rect.size, rect.size);
                if (!point_layout_changed) {
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
                glUniformMatrix4fv(
                    renderer.point_shadow_uniforms.light_matrix,
                    1,
                    GL_FALSE,
                    matrices[static_cast<std::size_t>(face)].m.data()
                );
                draw_shadow_sector_ranges(render_cache, shadow_sector_ids);
                ++renderer.last_point_shadow_faces_rendered;
            }
            renderer.point_shadow_cache[key] =
                make_point_shadow_cache_entry(light, entry.light_index, entry, shadow_revision);
        }
        glDisable(GL_SCISSOR_TEST);
    }

    if (render_sun_shadow && sun_shadow_dirty) {
        glViewport(0, 0, kSunShadowAtlasSize, kSunShadowAtlasSize);
        glBindRenderbuffer(GL_RENDERBUFFER, renderer.sun_shadow_depth_renderbuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer.sun_shadow_depth_renderbuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer.sun_shadow_texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            renderer.shadows_disabled = true;
            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glUseProgram(0);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
        glClearColor(1.0F, 1.0F, 0.0F, 0.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(renderer.sun_shadow_program);
        glEnable(GL_SCISSOR_TEST);
        for (int cascade_index = 0; cascade_index < kSunShadowCascadeCount; ++cascade_index) {
            const int tile_x = (cascade_index % kSunShadowCascadeGrid) * kSunShadowResolution;
            const int tile_y = (cascade_index / kSunShadowCascadeGrid) * kSunShadowResolution;
            glViewport(tile_x, tile_y, kSunShadowResolution, kSunShadowResolution);
            glScissor(tile_x, tile_y, kSunShadowResolution, kSunShadowResolution);
            glUniformMatrix4fv(
                renderer.sun_shadow_uniforms.light_matrix,
                1,
                GL_FALSE,
                sun_cascades[static_cast<std::size_t>(cascade_index)].matrix.m.data()
            );
            draw_shadow_sector_ranges(
                render_cache,
                sun_shadow_sectors_for_cascade(
                    renderer,
                    world,
                    sun_cascades[static_cast<std::size_t>(cascade_index)]
                )
            );
            ++renderer.last_sun_shadow_cascades_rendered;
        }
        glDisable(GL_SCISSOR_TEST);
        cache_sun_cascades(renderer, sun_cascades);
        renderer.sun_shadow_cache = make_sun_shadow_cache_entry(
            shadow_revision,
            normalize3(world_lighting.sun_direction, WorldLighting{}.sun_direction),
            renderer.width,
            renderer.height,
            config.fov_y_degrees,
            config.near_plane,
            config.far_plane,
            camera.yaw,
            camera.pitch,
            sun_cascade_matrices(sun_cascades)
        );
    }

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const auto shadow_end = std::chrono::steady_clock::now();
    renderer.last_shadow_ms =
        std::chrono::duration<double, std::milli>(shadow_end - shadow_start).count();
    return true;
}

bool render_screen_space_sun_shadow(
    DeferredRenderer& renderer,
    const int width,
    const int height,
    const WorldLighting& world_lighting,
    const GameCamera& camera,
    const GameRenderConfig& config,
    const Mat4& view_projection
) {
    renderer.last_screen_shadow_ms = 0.0;
    if (!config.screen_space_shadows_enabled ||
        !world_lighting.sun_enabled ||
        world_lighting.sun_intensity <= 0.0F ||
        renderer.screen_shadows_disabled ||
        renderer.screen_shadow_framebuffer == 0 ||
        renderer.screen_shadow_texture == 0 ||
        renderer.screen_shadow_program == 0) {
        return false;
    }

    const auto screen_shadow_start = std::chrono::steady_clock::now();
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.screen_shadow_framebuffer);
    glViewport(0, 0, width, height);
    const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &draw_buffer);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(1.0F, 1.0F, 1.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer.screen_shadow_program);
    const DeferredScreenShadowUniforms& uniforms = renderer.screen_shadow_uniforms;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.position_texture);
    glUniform1i(uniforms.position, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.normal_texture);
    glUniform1i(uniforms.normal, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.albedo_texture);
    glUniform1i(uniforms.albedo, 2);
    glUniform2f(
        uniforms.inv_viewport,
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height)
    );
    glUniform3f(uniforms.camera_position, camera.x, camera.y, camera.z);
    const Vec3 forward = camera_forward(camera);
    glUniform3f(uniforms.camera_forward, forward.x, forward.y, forward.z);
    glUniform3f(
        uniforms.sun_direction,
        world_lighting.sun_direction.x,
        world_lighting.sun_direction.y,
        world_lighting.sun_direction.z
    );
    glUniformMatrix4fv(
        uniforms.view_projection_matrix,
        1,
        GL_FALSE,
        view_projection.m.data()
    );

    glBindVertexArray(renderer.fullscreen_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer.last_screen_shadow_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - screen_shadow_start).count();
    return true;
}

int draw_runtime_world(
    const RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const int width,
    const int height,
    const GameCamera& camera,
    const bool draw_wire_overlay,
    const bool filter_connected_visibility,
    const GameRenderConfig& config,
    const GLuint material_texture_array
) {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    set_game_projection(width, height, camera, config);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    core_set_line_width(1.0F);

    const int camera_sector = sector_at_point(world, Vec3{camera.x, camera.y, camera.z});
    const bool filter_visible_sectors = filter_connected_visibility && camera_sector >= 0;
    std::vector<int> visible_sectors = filter_visible_sectors
        ? visible_sectors_from_camera(world, camera, config, width, height)
        : std::vector<int>{};
    if (filter_visible_sectors && visible_sectors.empty()) {
        visible_sectors.push_back(camera_sector);
    }
    const auto is_visible = [&visible_sectors, filter_visible_sectors](const int sector_id) {
        return !filter_visible_sectors ||
            std::find(visible_sectors.begin(), visible_sectors.end(), sector_id) != visible_sectors.end();
    };

    int visible_triangle_count = 0;
    const Mat4 view_projection = game_view_projection_matrix(width, height, camera, config);

    if (render_cache.vertex_buffer != 0 && render_cache.total_vertices > 0) {
        if (!filter_visible_sectors) {
            draw_textured_runtime_arrays(
                render_cache,
                view_projection,
                material_texture_array,
                0,
                render_cache.total_vertices
            );
            visible_triangle_count = render_cache.total_vertices / 3;
        } else {
            for (const int sector_id : visible_sectors) {
                if (sector_id < 0 || sector_id >= static_cast<int>(render_cache.sector_ranges.size())) {
                    continue;
                }
                const RuntimeRenderRange range = render_cache.sector_ranges[static_cast<std::size_t>(sector_id)];
                if (range.vertex_count <= 0) {
                    continue;
                }
                draw_textured_runtime_arrays(
                    render_cache,
                    view_projection,
                    material_texture_array,
                    range.first_vertex,
                    range.vertex_count
                );
                visible_triangle_count += range.vertex_count / 3;
            }
        }
    }

    if (draw_wire_overlay) {
        core_begin(GL_LINES);
        core_color4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (!is_visible(tagged_triangle.sector_id)) {
                continue;
            }

            const RuntimeTriangle& triangle = tagged_triangle.triangle;
            core_vertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            core_vertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            core_vertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            core_vertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            core_vertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            core_vertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        core_end();
    }

    return visible_triangle_count;
}

int draw_deferred_runtime_world(
    DeferredRenderer& renderer,
    const RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const std::vector<PointLight>& point_lights,
    const WorldLighting& world_lighting,
    const int width,
    const int height,
    const GameCamera& camera,
    const bool draw_wire_overlay,
    const GameRenderConfig& config,
    const GLuint material_texture_array
) {
    renderer.last_gbuffer_ms = 0.0;
    renderer.last_shadow_pack_upload_ms = 0.0;
    renderer.last_shadow_ms = 0.0;
    renderer.last_screen_shadow_ms = 0.0;
    renderer.last_lighting_ms = 0.0;
    renderer.last_wire_overlay_ms = 0.0;
    if (material_texture_array == 0 ||
        !ensure_deferred_renderer(renderer, width, height) ||
        render_cache.vertex_buffer == 0 || render_cache.total_vertices <= 0) {
        return draw_runtime_world(
            world,
            render_cache,
            width,
            height,
            camera,
            draw_wire_overlay,
            true,
            config,
            material_texture_array
        );
    }

    auto elapsed_ms = [](const std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    const auto pack_start = std::chrono::steady_clock::now();
    renderer.scratch_lights.clear();
    const Vec3 camera_position{camera.x, camera.y, camera.z};
    const int light_count = static_cast<int>(std::min<std::size_t>(point_lights.size(), kMaxDeferredPointLights));
    renderer.scratch_lights.reserve(static_cast<std::size_t>(std::max(light_count, 1)));
    if (point_lights.size() <= static_cast<std::size_t>(kMaxDeferredPointLights)) {
        renderer.scratch_lights.insert(renderer.scratch_lights.end(), point_lights.begin(), point_lights.end());
    } else {
        renderer.ranked_light_indices.clear();
        renderer.ranked_light_indices.reserve(point_lights.size());
        for (int i = 0; i < static_cast<int>(point_lights.size()); ++i) {
            renderer.ranked_light_indices.push_back(i);
        }
        std::sort(renderer.ranked_light_indices.begin(), renderer.ranked_light_indices.end(), [&point_lights, camera_position](const int a, const int b) {
            const float score_a = point_light_priority(point_lights[static_cast<std::size_t>(a)], camera_position);
            const float score_b = point_light_priority(point_lights[static_cast<std::size_t>(b)], camera_position);
            if (score_a != score_b) {
                return score_a > score_b;
            }
            return a < b;
        });
        for (int i = 0; i < kMaxDeferredPointLights; ++i) {
            renderer.scratch_lights.push_back(
                point_lights[static_cast<std::size_t>(renderer.ranked_light_indices[static_cast<std::size_t>(i)])]
            );
        }
    }
    const bool using_fallback_light = point_lights.empty();
    if (using_fallback_light) {
        PointLight fallback_light;
        fallback_light.position = Vec3{camera.x, camera.y + 48.0F, camera.z};
        fallback_light.color = Vec3{1.0F, 0.93F, 0.80F};
        fallback_light.radius = 640.0F;
        fallback_light.intensity = 1.8F;
        renderer.scratch_lights.push_back(fallback_light);
    }
    const std::vector<PointLight>& lights = renderer.scratch_lights;
    const int submitted_light_count = static_cast<int>(lights.size());
    const PackedPointShadowAtlas shadow_atlas =
        pack_point_shadow_atlas(
            lights,
            camera_position,
            kMaxDeferredPointLights,
            (!config.vsm_shadows_enabled || using_fallback_light) ? 0 : kMaxPointShadowedLights,
            kPointShadowAtlasSize
        );
    if (!using_fallback_light &&
        shadow_atlas.shadowed_lights < shadow_atlas.submitted_lights &&
        (renderer.last_shadow_submitted_lights != shadow_atlas.submitted_lights ||
            renderer.last_shadow_packed_lights != shadow_atlas.shadowed_lights)) {
        std::cout << "Shadow atlas packed " << shadow_atlas.shadowed_lights
                  << '/' << shadow_atlas.submitted_lights << " point lights\n";
        renderer.last_shadow_submitted_lights = shadow_atlas.submitted_lights;
        renderer.last_shadow_packed_lights = shadow_atlas.shadowed_lights;
    }
    SunShadowCascades cascades = sun_shadow_cascades(world_lighting, camera, config, width, height);
    const bool shadows_ready =
        render_vsm_shadow_maps(
            renderer,
            world,
            render_cache,
            lights,
            shadow_atlas,
            world_lighting,
            camera,
            config,
            cascades
        );
    const Mat4 view_projection = game_view_projection_matrix(width, height, camera, config);
    renderer.last_shadow_pack_upload_ms = elapsed_ms(pack_start) - renderer.last_shadow_ms;

    const int camera_sector = sector_at_point(world, Vec3{camera.x, camera.y, camera.z});
    const bool filter_visible_sectors = camera_sector >= 0;
    std::vector<int> visible_sectors = filter_visible_sectors
        ? visible_sectors_from_camera(world, camera, config, width, height)
        : std::vector<int>{};
    if (filter_visible_sectors && visible_sectors.empty()) {
        visible_sectors.push_back(camera_sector);
    }

    int visible_triangle_count = 0;

    const auto gbuffer_start = std::chrono::steady_clock::now();
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.framebuffer);
    glViewport(0, 0, width, height);
    const GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
    };
    glDrawBuffers(4, draw_buffers);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    set_game_projection(width, height, camera, config);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(renderer.geometry_program);
    glUniformMatrix4fv(renderer.geometry_uniforms.view_projection, 1, GL_FALSE, view_projection.m.data());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, material_texture_array);
    glUniform1i(renderer.geometry_uniforms.material_albedo, 7);
    glBindVertexArray(renderer.geometry_vao);
    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
    );
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, r))
    );
    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, nx))
    );
    glVertexAttribPointer(
        3,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, roughness))
    );
    glVertexAttribPointer(
        4,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, u))
    );
    glVertexAttribPointer(
        5,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, material_slot))
    );

    if (!filter_visible_sectors) {
        glDrawArrays(GL_TRIANGLES, 0, render_cache.total_vertices);
        visible_triangle_count = render_cache.total_vertices / 3;
    } else {
        for (const int sector_id : visible_sectors) {
            if (sector_id < 0 || sector_id >= static_cast<int>(render_cache.sector_ranges.size())) {
                continue;
            }
            const RuntimeRenderRange range = render_cache.sector_ranges[static_cast<std::size_t>(sector_id)];
            if (range.vertex_count <= 0) {
                continue;
            }
            glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
            visible_triangle_count += range.vertex_count / 3;
        }
    }

    glDisableVertexAttribArray(5);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer.last_gbuffer_ms = elapsed_ms(gbuffer_start);

    const bool screen_shadows_ready = render_screen_space_sun_shadow(
        renderer,
        width,
        height,
        world_lighting,
        camera,
        config,
        view_projection
    );

    const auto lighting_start = std::chrono::steady_clock::now();
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(renderer.lighting_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.position_texture);
    const DeferredLightingUniforms& uniforms = renderer.lighting_uniforms;
    glUniform1i(uniforms.position, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.normal_texture);
    glUniform1i(uniforms.normal, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.albedo_texture);
    glUniform1i(uniforms.albedo, 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer.material_texture);
    glUniform1i(uniforms.material, 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, screen_shadows_ready ? renderer.screen_shadow_texture : 0);
    glUniform1i(uniforms.screen_shadow_mask, 4);
    glUniform2f(
        uniforms.inv_viewport,
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height)
    );
    glUniform3f(uniforms.camera_position, camera.x, camera.y, camera.z);
    glUniform3f(uniforms.ambient_color, 0.14F, 0.17F, 0.18F);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, shadows_ready ? renderer.point_shadow_texture : 0);
    glUniform1i(uniforms.point_shadow_atlas, 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, shadows_ready ? renderer.sun_shadow_texture : 0);
    glUniform1i(uniforms.sun_shadow_moments, 6);
    glUniform1i(
        uniforms.point_shadows_enabled,
        config.vsm_shadows_enabled && shadows_ready && shadow_atlas.shadowed_lights > 0 ? 1 : 0
    );
    glUniform1i(uniforms.sun_enabled, world_lighting.sun_enabled ? 1 : 0);
    glUniform1i(
        uniforms.sun_shadow_enabled,
        config.vsm_shadows_enabled &&
                config.csm_shadows_enabled &&
                shadows_ready &&
                world_lighting.sun_enabled &&
                world_lighting.sun_intensity > 0.0F
            ? 1
            : 0
    );
    glUniform3f(
        uniforms.sun_direction,
        world_lighting.sun_direction.x,
        world_lighting.sun_direction.y,
        world_lighting.sun_direction.z
    );
    glUniform3f(
        uniforms.sun_color,
        world_lighting.sun_color.x,
        world_lighting.sun_color.y,
        world_lighting.sun_color.z
    );
    glUniform1f(uniforms.sun_intensity, world_lighting.sun_intensity);
    std::array<float, kSunShadowCascadeCount * 16> sun_matrix_values{};
    std::array<float, kSunShadowCascadeCount * 4> sun_rect_values{};
    for (int cascade_index = 0; cascade_index < kSunShadowCascadeCount; ++cascade_index) {
        const SunShadowCascade& cascade = cascades[static_cast<std::size_t>(cascade_index)];
        std::copy(
            cascade.matrix.m.begin(),
            cascade.matrix.m.end(),
            sun_matrix_values.begin() + (cascade_index * 16)
        );
        std::copy(
            cascade.rect.begin(),
            cascade.rect.end(),
            sun_rect_values.begin() + (cascade_index * 4)
        );
    }
    glUniformMatrix4fv(uniforms.sun_shadow_matrices, kSunShadowCascadeCount, GL_FALSE, sun_matrix_values.data());
    glUniform4fv(uniforms.sun_shadow_rects, kSunShadowCascadeCount, sun_rect_values.data());
    glUniform4f(
        uniforms.sun_cascade_splits,
        cascades[0].split_distance,
        cascades[1].split_distance,
        cascades[2].split_distance,
        cascades[3].split_distance
    );
    const Vec3 forward = camera_forward(camera);
    glUniform3f(uniforms.camera_forward, forward.x, forward.y, forward.z);
    glUniform1i(uniforms.screen_space_shadows_enabled, screen_shadows_ready ? 1 : 0);
    glUniform1i(uniforms.fog_enabled, config.fog_enabled ? 1 : 0);
    glUniform2f(uniforms.fog_start_end, config.fog_start, config.fog_end);
    glUniform3f(uniforms.fog_color, config.fog_color.x, config.fog_color.y, config.fog_color.z);
    glUniform1i(uniforms.light_count, submitted_light_count);
    const auto upload_start = std::chrono::steady_clock::now();
    renderer.point_light_records.assign(static_cast<std::size_t>(submitted_light_count), GpuPointLight{});
    const int shadow_face_count = (shadows_ready && shadow_atlas.shadowed_lights > 0)
        ? submitted_light_count * kPointShadowFaceCount
        : 0;
    renderer.point_shadow_faces.assign(static_cast<std::size_t>(shadow_face_count), GpuPointShadowFace{});
    for (int i = 0; i < submitted_light_count; ++i) {
        const PointLight& light = lights[static_cast<std::size_t>(i)];
        GpuPointLight& record = renderer.point_light_records[static_cast<std::size_t>(i)];
        record.position_radius = {light.position.x, light.position.y, light.position.z, light.radius};
        record.color_intensity = {light.color.x, light.color.y, light.color.z, light.intensity};
        record.shadow_flags[1] = std::max(light.shadow_bias, 0.0F);
    }
    if (shadow_face_count > 0) {
        const float inv_atlas = 1.0F / static_cast<float>(std::max(shadow_atlas.atlas_size, 1));
        for (const PointShadowAtlasEntry& entry : shadow_atlas.entries) {
            if (!entry.shadowed || entry.light_index < 0 || entry.light_index >= submitted_light_count) {
                continue;
            }
            renderer.point_light_records[static_cast<std::size_t>(entry.light_index)].shadow_flags[0] = 1.0F;
            const PointLight& light = lights[static_cast<std::size_t>(entry.light_index)];
            const std::array<Mat4, kPointShadowFaceCount> matrices = point_shadow_matrices(light);
            for (int face = 0; face < kPointShadowFaceCount; ++face) {
                const int shadow_index = (entry.light_index * kPointShadowFaceCount) + face;
                const ShadowAtlasRect rect = entry.faces[static_cast<std::size_t>(face)];
                GpuPointShadowFace& shadow_face = renderer.point_shadow_faces[static_cast<std::size_t>(shadow_index)];
                shadow_face.rect = {
                    static_cast<float>(rect.x) * inv_atlas,
                    static_cast<float>(rect.y) * inv_atlas,
                    static_cast<float>(rect.size) * inv_atlas,
                    static_cast<float>(rect.size) * inv_atlas,
                };
                shadow_face.matrix = matrices[static_cast<std::size_t>(face)].m;
            }
        }
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderer.point_light_buffer);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(renderer.point_light_records.size() * sizeof(GpuPointLight)),
        renderer.point_light_records.empty() ? nullptr : renderer.point_light_records.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, renderer.point_light_buffer);
    if (shadow_face_count > 0) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderer.point_shadow_face_buffer);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<GLsizeiptr>(renderer.point_shadow_faces.size() * sizeof(GpuPointShadowFace)),
            renderer.point_shadow_faces.data(),
            GL_DYNAMIC_DRAW
        );
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, renderer.point_shadow_face_buffer);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    renderer.last_shadow_pack_upload_ms += elapsed_ms(upload_start);

    glBindVertexArray(renderer.fullscreen_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    renderer.last_lighting_ms = elapsed_ms(lighting_start);

    if (draw_wire_overlay) {
        const auto wire_start = std::chrono::steady_clock::now();
        set_game_projection(width, height, camera, config);
        glDisable(GL_DEPTH_TEST);
        core_begin(GL_LINES);
        core_color4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (filter_visible_sectors &&
                std::find(visible_sectors.begin(), visible_sectors.end(), tagged_triangle.sector_id) == visible_sectors.end()) {
                continue;
            }

            const RuntimeTriangle& triangle = tagged_triangle.triangle;
            core_vertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            core_vertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            core_vertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            core_vertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            core_vertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            core_vertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        core_end();
        renderer.last_wire_overlay_ms = elapsed_ms(wire_start);
    }

    return visible_triangle_count;
}

} // namespace undecedent
