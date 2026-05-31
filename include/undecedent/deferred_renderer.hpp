#pragma once

#include <glad/glad.h>

namespace undecedent {

struct DeferredRenderer {
    GLuint framebuffer = 0;
    GLuint position_texture = 0;
    GLuint normal_texture = 0;
    GLuint albedo_texture = 0;
    GLuint material_texture = 0;
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
    GLuint point_shadow_program = 0;
    GLuint sun_shadow_program = 0;
    int width = 0;
    int height = 0;
    int point_shadow_atlas_size = 0;
    int sun_shadow_resolution = 0;
    bool initialized = false;
    bool disabled = false;
    bool shadows_disabled = false;
    double last_shadow_ms = 0.0;
    int last_shadow_submitted_lights = -1;
    int last_shadow_packed_lights = -1;
};

void destroy_deferred_renderer(DeferredRenderer& renderer);
bool ensure_deferred_renderer(DeferredRenderer& renderer, int width, int height);

} // namespace undecedent
