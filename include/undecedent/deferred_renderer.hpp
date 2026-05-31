#pragma once

#include "undecedent/geometry.hpp"

#include <glad/glad.h>

#include <array>
#include <vector>

namespace undecedent {

struct GpuPointLight {
    std::array<float, 4> position_radius{};
    std::array<float, 4> color_intensity{};
    std::array<float, 4> shadow_flags{};
};

struct GpuPointShadowFace {
    std::array<float, 4> rect{};
    std::array<float, 16> matrix{};
};

struct DeferredLightingUniforms {
    GLint position = -1;
    GLint normal = -1;
    GLint albedo = -1;
    GLint material = -1;
    GLint screen_shadow_mask = -1;
    GLint point_shadow_atlas = -1;
    GLint sun_shadow_moments = -1;
    GLint inv_viewport = -1;
    GLint camera_position = -1;
    GLint ambient_color = -1;
    GLint light_count = -1;
    GLint point_shadows_enabled = -1;
    GLint sun_enabled = -1;
    GLint sun_shadow_enabled = -1;
    GLint sun_direction = -1;
    GLint sun_color = -1;
    GLint sun_intensity = -1;
    GLint sun_shadow_matrix = -1;
    GLint screen_space_shadows_enabled = -1;
};

struct DeferredPointShadowUniforms {
    GLint light_position = -1;
    GLint light_radius = -1;
    GLint light_matrix = -1;
};

struct DeferredSunShadowUniforms {
    GLint light_matrix = -1;
};

struct DeferredScreenShadowUniforms {
    GLint position = -1;
    GLint normal = -1;
    GLint albedo = -1;
    GLint inv_viewport = -1;
    GLint camera_position = -1;
    GLint camera_forward = -1;
    GLint sun_direction = -1;
    GLint view_projection_matrix = -1;
};

struct DeferredRenderer {
    GLuint framebuffer = 0;
    GLuint screen_shadow_framebuffer = 0;
    GLuint position_texture = 0;
    GLuint normal_texture = 0;
    GLuint albedo_texture = 0;
    GLuint material_texture = 0;
    GLuint screen_shadow_texture = 0;
    GLuint depth_renderbuffer = 0;
    GLuint shadow_framebuffer = 0;
    GLuint point_shadow_depth_renderbuffer = 0;
    GLuint sun_shadow_depth_renderbuffer = 0;
    GLuint point_shadow_texture = 0;
    GLuint sun_shadow_texture = 0;
    GLuint point_light_buffer = 0;
    GLuint point_shadow_face_buffer = 0;
    GLuint geometry_program = 0;
    GLuint lighting_program = 0;
    GLuint screen_shadow_program = 0;
    GLuint point_shadow_program = 0;
    GLuint sun_shadow_program = 0;
    DeferredLightingUniforms lighting_uniforms;
    DeferredPointShadowUniforms point_shadow_uniforms;
    DeferredSunShadowUniforms sun_shadow_uniforms;
    DeferredScreenShadowUniforms screen_shadow_uniforms;
    std::vector<PointLight> scratch_lights;
    std::vector<int> ranked_light_indices;
    std::vector<GpuPointLight> point_light_records;
    std::vector<GpuPointShadowFace> point_shadow_faces;
    int width = 0;
    int height = 0;
    int point_shadow_atlas_size = 0;
    int sun_shadow_resolution = 0;
    bool initialized = false;
    bool disabled = false;
    bool shadows_disabled = false;
    bool screen_shadows_disabled = false;
    double last_gbuffer_ms = 0.0;
    double last_shadow_pack_upload_ms = 0.0;
    double last_shadow_ms = 0.0;
    double last_screen_shadow_ms = 0.0;
    double last_lighting_ms = 0.0;
    double last_wire_overlay_ms = 0.0;
    int last_shadow_submitted_lights = -1;
    int last_shadow_packed_lights = -1;
};

void destroy_deferred_renderer(DeferredRenderer& renderer);
bool ensure_deferred_renderer(DeferredRenderer& renderer, int width, int height);

} // namespace undecedent
