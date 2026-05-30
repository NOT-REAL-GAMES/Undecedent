#include "undecedent/runtime_render.hpp"

#include "undecedent/runtime_visibility.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace undecedent {
namespace {

constexpr int kMaxDeferredPointLights = 32;

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
    const int width,
    const int height,
    const GameCamera& camera,
    const bool draw_wire_overlay,
    const GameRenderConfig& config
) {
    if (!ensure_deferred_renderer(renderer, width, height) ||
        render_cache.vertex_buffer == 0 || render_cache.total_vertices <= 0) {
        return draw_runtime_world(world, render_cache, width, height, camera, draw_wire_overlay, true, config);
    }

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
    const GLint light_count_location = glGetUniformLocation(renderer.lighting_program, "uLightCount");
    int light_count = static_cast<int>(std::min<std::size_t>(point_lights.size(), kMaxDeferredPointLights));
    std::array<PointLight, kMaxDeferredPointLights> fallback_lights{};
    const PointLight* lights = point_lights.empty() ? fallback_lights.data() : point_lights.data();
    if (point_lights.empty()) {
        fallback_lights[0].position = Vec3{camera.x, camera.y + 48.0F, camera.z};
        fallback_lights[0].color = Vec3{1.0F, 0.93F, 0.80F};
        fallback_lights[0].radius = 640.0F;
        fallback_lights[0].intensity = 1.8F;
        light_count = 1;
    }
    glUniform1i(light_count_location, light_count);
    for (int i = 0; i < light_count; ++i) {
        const PointLight& light = lights[i];
        const std::string index = std::to_string(i);
        glUniform3f(
            glGetUniformLocation(renderer.lighting_program, ("uLightPositions[" + index + "]").c_str()),
            light.position.x,
            light.position.y,
            light.position.z
        );
        glUniform3f(
            glGetUniformLocation(renderer.lighting_program, ("uLightColors[" + index + "]").c_str()),
            light.color.x,
            light.color.y,
            light.color.z
        );
        glUniform1f(
            glGetUniformLocation(renderer.lighting_program, ("uLightRadii[" + index + "]").c_str()),
            light.radius
        );
        glUniform1f(
            glGetUniformLocation(renderer.lighting_program, ("uLightIntensities[" + index + "]").c_str()),
            light.intensity
        );
    }

    glBegin(GL_QUADS);
    glVertex2f(-1.0F, -1.0F);
    glVertex2f(1.0F, -1.0F);
    glVertex2f(1.0F, 1.0F);
    glVertex2f(-1.0F, 1.0F);
    glEnd();
    glUseProgram(0);
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
