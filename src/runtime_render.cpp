#include "undecedent/runtime_render.hpp"

#include "undecedent/runtime_visibility.hpp"
#include "undecedent/shadow_atlas.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace undecedent {
namespace {

struct Mat4 {
    std::array<float, 16> m{};
};

struct GpuPointLight {
    std::array<float, 4> position_radius{};
    std::array<float, 4> color_intensity{};
    std::array<float, 4> shadow_flags{};
};

struct GpuPointShadowFace {
    std::array<float, 4> rect{};
    std::array<float, 16> matrix{};
};

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

Mat4 sun_shadow_matrix(const WorldLighting& lighting, const GameCamera& camera) {
    const Vec3 sun_direction = normalize3(lighting.sun_direction, WorldLighting{}.sun_direction);
    const Vec3 center{camera.x, camera.y, camera.z};
    const float half_extent = 2048.0F;
    const float depth_extent = 4096.0F;
    const Vec3 eye = sub3(center, scale3(sun_direction, depth_extent * 0.5F));
    const Vec3 up = std::abs(sun_direction.y) > 0.92F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{0.0F, 1.0F, 0.0F};
    const Mat4 projection = orthographic_matrix(-half_extent, half_extent, -half_extent, half_extent, 1.0F, depth_extent);
    return multiply(projection, look_at_matrix(eye, center, up));
}

float point_light_priority(const PointLight& light, const Vec3 camera_position) {
    const float dx = light.position.x - camera_position.x;
    const float dy = light.position.y - camera_position.y;
    const float dz = light.position.z - camera_position.z;
    const float distance = std::max(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)), 1.0F);
    return std::max(light.intensity, 0.0F) * std::max(light.radius, 0.0F) / distance;
}

} // namespace

void set_game_projection(
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    const double aspect = height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;
    const double fov_y_radians = static_cast<double>(config.fov_y_degrees) * 3.14159265358979323846 / 180.0;
    const double top = std::tan(fov_y_radians * 0.5) * config.near_plane;
    const double right = top * aspect;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, config.near_plane, config.far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-camera.pitch * 180.0F / 3.14159265F, 1.0F, 0.0F, 0.0F);
    glRotatef(-camera.yaw * 180.0F / 3.14159265F, 0.0F, 1.0F, 0.0F);
    glTranslatef(-camera.x, -camera.y, -camera.z);
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
    glColor4f(1.0F, 0.82F, 0.25F, 0.95F);
    glLineWidth(2.0F);
    const float feet = spawn.position.y - config.player_eye_height;
    const float head = feet + config.player_height;
    const float x = spawn.position.x;
    const float z = spawn.position.z;
    glBegin(GL_LINES);
    glVertex3f(x, feet, z);
    glVertex3f(x, head, z);
    glVertex3f(x - config.player_radius, feet, z);
    glVertex3f(x + config.player_radius, feet, z);
    glVertex3f(x, feet, z - config.player_radius);
    glVertex3f(x, feet, z + config.player_radius);
    glVertex3f(x, spawn.position.y, z);
    glVertex3f(x - std::sin(spawn.yaw) * 24.0F, spawn.position.y, z - std::cos(spawn.yaw) * 24.0F);
    glEnd();
    glLineWidth(1.0F);
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
    glColor4f(1.0F, 0.86F, 0.35F, 0.92F);
    glLineWidth(2.0F);
    glBegin(GL_LINES);
    for (const PointLight& light : point_lights) {
        const float x = light.position.x;
        const float y = light.position.y;
        const float z = light.position.z;
        constexpr float s = 8.0F;
        glVertex3f(x - s, y, z);
        glVertex3f(x + s, y, z);
        glVertex3f(x, y - s, z);
        glVertex3f(x, y + s, z);
        glVertex3f(x, y, z - s);
        glVertex3f(x, y, z + s);
        glVertex3f(x - s, y - s, z);
        glVertex3f(x + s, y + s, z);
        glVertex3f(x - s, y + s, z);
        glVertex3f(x + s, y - s, z);
    }
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

bool render_vsm_shadow_maps(
    DeferredRenderer& renderer,
    const RuntimeRenderCache& render_cache,
    const std::vector<PointLight>& lights,
    const PackedPointShadowAtlas& atlas,
    const WorldLighting& world_lighting,
    const GameCamera& camera
) {
    renderer.last_shadow_ms = 0.0;
    if (renderer.shadows_disabled ||
        renderer.shadow_framebuffer == 0 ||
        renderer.point_shadow_texture == 0 ||
        renderer.sun_shadow_texture == 0 ||
        renderer.point_shadow_program == 0 ||
        renderer.sun_shadow_program == 0 ||
        render_cache.vertex_buffer == 0 ||
        render_cache.total_vertices <= 0) {
        return false;
    }

    const auto shadow_start = std::chrono::steady_clock::now();

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.shadow_framebuffer);
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

    glViewport(0, 0, kPointShadowAtlasSize, kPointShadowAtlasSize);
    glBindRenderbuffer(GL_RENDERBUFFER, renderer.point_shadow_depth_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kPointShadowAtlasSize, kPointShadowAtlasSize);
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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(renderer.point_shadow_program);
    glEnable(GL_SCISSOR_TEST);
    for (const PointShadowAtlasEntry& entry : atlas.entries) {
        if (!entry.shadowed || entry.light_index < 0 || entry.light_index >= static_cast<int>(lights.size())) {
            continue;
        }
        const PointLight& light = lights[static_cast<std::size_t>(entry.light_index)];
        const std::array<Mat4, 6> matrices = point_shadow_matrices(light);
        glUniform3f(
            glGetUniformLocation(renderer.point_shadow_program, "uLightPosition"),
            light.position.x,
            light.position.y,
            light.position.z
        );
        glUniform1f(glGetUniformLocation(renderer.point_shadow_program, "uLightRadius"), std::max(light.radius, 1.0F));
        for (int face = 0; face < 6; ++face) {
            const ShadowAtlasRect rect = entry.faces[static_cast<std::size_t>(face)];
            glViewport(rect.x, rect.y, rect.size, rect.size);
            glScissor(rect.x, rect.y, rect.size, rect.size);
            glUniformMatrix4fv(
                glGetUniformLocation(renderer.point_shadow_program, "uLightMatrix"),
                1,
                GL_FALSE,
                matrices[static_cast<std::size_t>(face)].m.data()
            );
            glDrawArrays(GL_TRIANGLES, 0, render_cache.total_vertices);
        }
    }
    glDisable(GL_SCISSOR_TEST);

    if (world_lighting.sun_enabled && world_lighting.sun_intensity > 0.0F) {
        const Mat4 matrix = sun_shadow_matrix(world_lighting, camera);
        glViewport(0, 0, kSunShadowResolution, kSunShadowResolution);
        glBindRenderbuffer(GL_RENDERBUFFER, renderer.sun_shadow_depth_renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSunShadowResolution, kSunShadowResolution);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer.sun_shadow_depth_renderbuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer.sun_shadow_texture, 0);
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
        glUniformMatrix4fv(
            glGetUniformLocation(renderer.sun_shadow_program, "uLightMatrix"),
            1,
            GL_FALSE,
            matrix.m.data()
        );
        glDrawArrays(GL_TRIANGLES, 0, render_cache.total_vertices);
    }

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const auto shadow_end = std::chrono::steady_clock::now();
    renderer.last_shadow_ms =
        std::chrono::duration<double, std::milli>(shadow_end - shadow_start).count();
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
    const GameRenderConfig& config
) {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    set_game_projection(width, height, camera, config);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glLineWidth(1.0F);

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

    if (render_cache.vertex_buffer != 0 && render_cache.total_vertices > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(
            3,
            GL_FLOAT,
            sizeof(RuntimeRenderVertex),
            reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
        );
        glColorPointer(
            3,
            GL_FLOAT,
            sizeof(RuntimeRenderVertex),
            reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, r))
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

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (draw_wire_overlay) {
        glBegin(GL_LINES);
        glColor4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (!is_visible(tagged_triangle.sector_id)) {
                continue;
            }

            const RuntimeTriangle& triangle = tagged_triangle.triangle;
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        glEnd();
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
    const GameRenderConfig& config
) {
    if (!ensure_deferred_renderer(renderer, width, height) ||
        render_cache.vertex_buffer == 0 || render_cache.total_vertices <= 0) {
        renderer.last_shadow_ms = 0.0;
        return draw_runtime_world(world, render_cache, width, height, camera, draw_wire_overlay, true, config);
    }

    std::vector<PointLight> lights;
    const Vec3 camera_position{camera.x, camera.y, camera.z};
    const int light_count = static_cast<int>(std::min<std::size_t>(point_lights.size(), kMaxDeferredPointLights));
    lights.reserve(static_cast<std::size_t>(std::max(light_count, 1)));
    if (point_lights.size() <= static_cast<std::size_t>(kMaxDeferredPointLights)) {
        lights.insert(lights.end(), point_lights.begin(), point_lights.end());
    } else {
        std::vector<int> ranked_indices;
        ranked_indices.reserve(point_lights.size());
        for (int i = 0; i < static_cast<int>(point_lights.size()); ++i) {
            ranked_indices.push_back(i);
        }
        std::sort(ranked_indices.begin(), ranked_indices.end(), [&point_lights, camera_position](const int a, const int b) {
            const float score_a = point_light_priority(point_lights[static_cast<std::size_t>(a)], camera_position);
            const float score_b = point_light_priority(point_lights[static_cast<std::size_t>(b)], camera_position);
            if (score_a != score_b) {
                return score_a > score_b;
            }
            return a < b;
        });
        for (int i = 0; i < kMaxDeferredPointLights; ++i) {
            lights.push_back(point_lights[static_cast<std::size_t>(ranked_indices[static_cast<std::size_t>(i)])]);
        }
    }
    if (point_lights.empty()) {
        PointLight fallback_light;
        fallback_light.position = Vec3{camera.x, camera.y + 48.0F, camera.z};
        fallback_light.color = Vec3{1.0F, 0.93F, 0.80F};
        fallback_light.radius = 640.0F;
        fallback_light.intensity = 1.8F;
        lights.push_back(fallback_light);
    }
    const int submitted_light_count = static_cast<int>(lights.size());
    const PackedPointShadowAtlas shadow_atlas =
        pack_point_shadow_atlas(
            lights,
            camera_position,
            kMaxDeferredPointLights,
            kMaxPointShadowedLights,
            kPointShadowAtlasSize
        );
    if (shadow_atlas.shadowed_lights < shadow_atlas.submitted_lights &&
        (renderer.last_shadow_submitted_lights != shadow_atlas.submitted_lights ||
            renderer.last_shadow_packed_lights != shadow_atlas.shadowed_lights)) {
        std::cout << "Shadow atlas packed " << shadow_atlas.shadowed_lights
                  << '/' << shadow_atlas.submitted_lights << " point lights\n";
        renderer.last_shadow_submitted_lights = shadow_atlas.submitted_lights;
        renderer.last_shadow_packed_lights = shadow_atlas.shadowed_lights;
    }
    const bool shadows_ready =
        render_vsm_shadow_maps(renderer, render_cache, lights, shadow_atlas, world_lighting, camera);
    const Mat4 sun_matrix = sun_shadow_matrix(world_lighting, camera);

    const int camera_sector = sector_at_point(world, Vec3{camera.x, camera.y, camera.z});
    const bool filter_visible_sectors = camera_sector >= 0;
    std::vector<int> visible_sectors = filter_visible_sectors
        ? visible_sectors_from_camera(world, camera, config, width, height)
        : std::vector<int>{};
    if (filter_visible_sectors && visible_sectors.empty()) {
        visible_sectors.push_back(camera_sector);
    }

    int visible_triangle_count = 0;

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

    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(renderer.lighting_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.position_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uPosition"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.normal_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uNormal"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.albedo_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uAlbedo"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer.material_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uMaterial"), 3);
    glUniform2f(
        glGetUniformLocation(renderer.lighting_program, "uInvViewport"),
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height)
    );
    glUniform3f(glGetUniformLocation(renderer.lighting_program, "uCameraPosition"), camera.x, camera.y, camera.z);
    glUniform3f(glGetUniformLocation(renderer.lighting_program, "uAmbientColor"), 0.14F, 0.17F, 0.18F);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, shadows_ready ? renderer.point_shadow_texture : 0);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uPointShadowAtlas"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, shadows_ready ? renderer.sun_shadow_texture : 0);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uSunShadowMoments"), 5);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uPointShadowsEnabled"), shadows_ready ? 1 : 0);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uSunEnabled"), world_lighting.sun_enabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uSunShadowEnabled"), shadows_ready ? 1 : 0);
    glUniform3f(
        glGetUniformLocation(renderer.lighting_program, "uSunDirection"),
        world_lighting.sun_direction.x,
        world_lighting.sun_direction.y,
        world_lighting.sun_direction.z
    );
    glUniform3f(
        glGetUniformLocation(renderer.lighting_program, "uSunColor"),
        world_lighting.sun_color.x,
        world_lighting.sun_color.y,
        world_lighting.sun_color.z
    );
    glUniform1f(glGetUniformLocation(renderer.lighting_program, "uSunIntensity"), world_lighting.sun_intensity);
    glUniformMatrix4fv(
        glGetUniformLocation(renderer.lighting_program, "uSunShadowMatrix"),
        1,
        GL_FALSE,
        sun_matrix.m.data()
    );
    const GLint light_count_location = glGetUniformLocation(renderer.lighting_program, "uLightCount");
    glUniform1i(light_count_location, submitted_light_count);
    std::vector<GpuPointLight> point_light_records(static_cast<std::size_t>(kMaxDeferredPointLights));
    std::vector<GpuPointShadowFace> point_shadow_faces(
        static_cast<std::size_t>(kMaxDeferredPointLights * kPointShadowFaceCount)
    );
    for (int i = 0; i < submitted_light_count; ++i) {
        const PointLight& light = lights[static_cast<std::size_t>(i)];
        GpuPointLight& record = point_light_records[static_cast<std::size_t>(i)];
        record.position_radius = {light.position.x, light.position.y, light.position.z, light.radius};
        record.color_intensity = {light.color.x, light.color.y, light.color.z, light.intensity};
    }
    if (shadows_ready) {
        const float inv_atlas = 1.0F / static_cast<float>(std::max(shadow_atlas.atlas_size, 1));
        for (const PointShadowAtlasEntry& entry : shadow_atlas.entries) {
            if (!entry.shadowed || entry.light_index < 0 || entry.light_index >= submitted_light_count) {
                continue;
            }
            point_light_records[static_cast<std::size_t>(entry.light_index)].shadow_flags[0] = 1.0F;
            const PointLight& light = lights[static_cast<std::size_t>(entry.light_index)];
            const std::array<Mat4, kPointShadowFaceCount> matrices = point_shadow_matrices(light);
            for (int face = 0; face < kPointShadowFaceCount; ++face) {
                const int shadow_index = (entry.light_index * kPointShadowFaceCount) + face;
                const ShadowAtlasRect rect = entry.faces[static_cast<std::size_t>(face)];
                GpuPointShadowFace& shadow_face = point_shadow_faces[static_cast<std::size_t>(shadow_index)];
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
        static_cast<GLsizeiptr>(point_light_records.size() * sizeof(GpuPointLight)),
        point_light_records.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, renderer.point_light_buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderer.point_shadow_face_buffer);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(point_shadow_faces.size() * sizeof(GpuPointShadowFace)),
        point_shadow_faces.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, renderer.point_shadow_face_buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBegin(GL_QUADS);
    glVertex2f(-1.0F, -1.0F);
    glVertex2f(1.0F, -1.0F);
    glVertex2f(1.0F, 1.0F);
    glVertex2f(-1.0F, 1.0F);
    glEnd();
    glUseProgram(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
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

    if (draw_wire_overlay) {
        set_game_projection(width, height, camera, config);
        glDisable(GL_DEPTH_TEST);
        glBegin(GL_LINES);
        glColor4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (filter_visible_sectors &&
                std::find(visible_sectors.begin(), visible_sectors.end(), tagged_triangle.sector_id) == visible_sectors.end()) {
                continue;
            }

            const RuntimeTriangle& triangle = tagged_triangle.triangle;
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        glEnd();
    }

    return visible_triangle_count;
}

} // namespace undecedent
