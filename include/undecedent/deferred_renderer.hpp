#pragma once

#include <glad/glad.h>

namespace undecedent {

struct DeferredRenderer {
    GLuint framebuffer = 0;
    GLuint position_texture = 0;
    GLuint normal_texture = 0;
    GLuint albedo_texture = 0;
    GLuint depth_renderbuffer = 0;
    GLuint geometry_program = 0;
    GLuint lighting_program = 0;
    int width = 0;
    int height = 0;
    bool initialized = false;
    bool disabled = false;
};

void destroy_deferred_renderer(DeferredRenderer& renderer);
bool ensure_deferred_renderer(DeferredRenderer& renderer, int width, int height);

} // namespace undecedent
