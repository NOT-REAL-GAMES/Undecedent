#include "undecedent/deferred_renderer.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace undecedent {
namespace {

std::string shader_info_log(const GLuint object, const bool program) {
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
        std::cerr << "Deferred shader compile failed (" << label << "): "
                  << shader_info_log(shader, false) << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint create_shader_program(
    const char* vertex_source,
    const char* fragment_source,
    const char* label,
    const bool bind_runtime_attributes
) {
    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, label);
    if (vertex_shader == 0) {
        return 0;
    }
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, label);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    if (bind_runtime_attributes) {
        glBindAttribLocation(program, 0, "aPosition");
        glBindAttribLocation(program, 1, "aColor");
        glBindAttribLocation(program, 2, "aNormal");
        glBindAttribLocation(program, 3, "aMaterial");
    }
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::cerr << "Deferred shader link failed (" << label << "): "
                  << shader_info_log(program, true) << '\n';
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLuint create_gbuffer_texture(
    const int width,
    const int height,
    const GLint internal_format,
    const GLenum format,
    const GLenum type
) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

bool create_deferred_programs(DeferredRenderer& renderer) {
    static constexpr const char* geometry_vertex = R"(
#version 120
attribute vec3 aPosition;
attribute vec3 aColor;
attribute vec3 aNormal;
attribute vec3 aMaterial;
varying vec3 vWorldPosition;
varying vec3 vNormal;
varying vec3 vAlbedo;
varying vec3 vMaterial;
void main() {
    vWorldPosition = aPosition;
    vNormal = normalize(aNormal);
    vAlbedo = aColor;
    vMaterial = aMaterial;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPosition, 1.0);
}
)";

    static constexpr const char* geometry_fragment = R"(
#version 120
varying vec3 vWorldPosition;
varying vec3 vNormal;
varying vec3 vAlbedo;
varying vec3 vMaterial;
void main() {
    gl_FragData[0] = vec4(vWorldPosition, 1.0);
    gl_FragData[1] = vec4((normalize(vNormal) * 0.5) + 0.5, 1.0);
    gl_FragData[2] = vec4(vAlbedo, 1.0);
    gl_FragData[3] = vec4(vMaterial, 1.0);
}
)";

    static constexpr const char* lighting_vertex = R"(
#version 120
void main() {
    gl_Position = gl_Vertex;
}
)";

    static constexpr const char* lighting_fragment = R"(
#version 120
uniform sampler2D uPosition;
uniform sampler2D uNormal;
uniform sampler2D uAlbedo;
uniform sampler2D uMaterial;
uniform vec2 uInvViewport;
uniform vec3 uCameraPosition;
uniform vec3 uAmbientColor;
uniform int uLightCount;
uniform vec3 uLightPositions[32];
uniform vec3 uLightColors[32];
uniform float uLightRadii[32];
uniform float uLightIntensities[32];
const float PI = 3.14159265358979323846;
float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}
float distribution_ggx(float ndoth, float roughness) {
    float alpha = max(roughness * roughness, 0.001);
    float alpha2 = alpha * alpha;
    float denom = (ndoth * ndoth) * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, 0.0001);
}
float visibility_smith_ggx(float ndotv, float ndotl, float roughness) {
    float alpha = max(roughness * roughness, 0.001);
    float alpha2 = alpha * alpha;
    float gl = ndotv * sqrt(max((ndotl - (ndotl * alpha2)) * ndotl + alpha2, 0.0001));
    float gv = ndotl * sqrt(max((ndotv - (ndotv * alpha2)) * ndotv + alpha2, 0.0001));
    return 0.5 / max(gl + gv, 0.0001);
}
vec3 fresnel_schlick(float hdotl, vec3 f0) {
    float fresnel = pow(1.0 - saturate(hdotl), 5.0);
    return f0 + ((vec3(1.0) - f0) * fresnel);
}
void main() {
    vec2 uv = gl_FragCoord.xy * uInvViewport;
    vec3 world_position = texture2D(uPosition, uv).xyz;
    vec3 encoded_normal = texture2D(uNormal, uv).xyz;
    vec3 albedo = texture2D(uAlbedo, uv).xyz;
    vec3 material = texture2D(uMaterial, uv).xyz;
    if (dot(albedo, albedo) <= 0.000001) {
        gl_FragColor = vec4(0.02, 0.025, 0.03, 1.0);
        return;
    }

    float roughness = clamp(material.r, 0.04, 1.0);
    float metallic = clamp(material.g, 0.0, 1.0);
    float specular = clamp(material.b, 0.0, 1.0);
    vec3 normal = normalize((encoded_normal * 2.0) - 1.0);
    vec3 view_dir = normalize(uCameraPosition - world_position);
    float ndotv = max(dot(normal, view_dir), 0.0001);
    vec3 f0 = mix(vec3(specular * 0.08), albedo, metallic);
    vec3 diffuse_color = albedo * (1.0 - metallic);
    vec3 color = diffuse_color * uAmbientColor;
    for (int i = 0; i < 32; ++i) {
        if (i >= uLightCount) {
            break;
        }
        vec3 light_vector = uLightPositions[i] - world_position;
        float light_distance = length(light_vector);
        vec3 light_dir = light_distance > 0.0001 ? light_vector / light_distance : vec3(0.0, 1.0, 0.0);
        vec3 half_dir = normalize(light_dir + view_dir);
        float ndotl = max(dot(normal, light_dir), 0.0);
        float ndoth = max(dot(normal, half_dir), 0.0);
        float hdotl = max(dot(half_dir, light_dir), 0.0);
        float range = max(uLightRadii[i], 1.0);
        float attenuation = clamp(1.0 - (light_distance / range), 0.0, 1.0);
        attenuation *= attenuation;
        vec3 radiance = uLightColors[i] * attenuation * uLightIntensities[i];
        float distribution = distribution_ggx(ndoth, roughness);
        float visibility = visibility_smith_ggx(ndotv, ndotl, roughness);
        vec3 fresnel = fresnel_schlick(hdotl, f0);
        vec3 specular_lobe = distribution * visibility * fresnel;
        vec3 diffuse_lobe = diffuse_color / PI;
        color += (diffuse_lobe + specular_lobe) * radiance * ndotl;
    }
    gl_FragColor = vec4(color, 1.0);
}
)";

    renderer.geometry_program = create_shader_program(
        geometry_vertex,
        geometry_fragment,
        "deferred geometry",
        true
    );
    if (renderer.geometry_program == 0) {
        return false;
    }
    renderer.lighting_program = create_shader_program(
        lighting_vertex,
        lighting_fragment,
        "deferred lighting",
        false
    );
    return renderer.lighting_program != 0;
}

} // namespace

void destroy_deferred_renderer(DeferredRenderer& renderer) {
    if (renderer.position_texture != 0) {
        glDeleteTextures(1, &renderer.position_texture);
    }
    if (renderer.normal_texture != 0) {
        glDeleteTextures(1, &renderer.normal_texture);
    }
    if (renderer.albedo_texture != 0) {
        glDeleteTextures(1, &renderer.albedo_texture);
    }
    if (renderer.material_texture != 0) {
        glDeleteTextures(1, &renderer.material_texture);
    }
    if (renderer.depth_renderbuffer != 0) {
        glDeleteRenderbuffers(1, &renderer.depth_renderbuffer);
    }
    if (renderer.framebuffer != 0) {
        glDeleteFramebuffers(1, &renderer.framebuffer);
    }
    if (renderer.geometry_program != 0) {
        glDeleteProgram(renderer.geometry_program);
    }
    if (renderer.lighting_program != 0) {
        glDeleteProgram(renderer.lighting_program);
    }
    renderer = {};
}

bool ensure_deferred_renderer(DeferredRenderer& renderer, const int width, const int height) {
    if (renderer.disabled || width <= 0 || height <= 0) {
        return false;
    }

    if (!renderer.initialized) {
        if (!create_deferred_programs(renderer)) {
            destroy_deferred_renderer(renderer);
            renderer.disabled = true;
            return false;
        }
        glGenFramebuffers(1, &renderer.framebuffer);
        glGenRenderbuffers(1, &renderer.depth_renderbuffer);
        renderer.initialized = true;
    }

    if (renderer.width == width && renderer.height == height &&
        renderer.position_texture != 0 && renderer.normal_texture != 0 &&
        renderer.albedo_texture != 0 && renderer.material_texture != 0) {
        return true;
    }

    renderer.width = width;
    renderer.height = height;
    if (renderer.position_texture != 0) {
        glDeleteTextures(1, &renderer.position_texture);
    }
    if (renderer.normal_texture != 0) {
        glDeleteTextures(1, &renderer.normal_texture);
    }
    if (renderer.albedo_texture != 0) {
        glDeleteTextures(1, &renderer.albedo_texture);
    }
    if (renderer.material_texture != 0) {
        glDeleteTextures(1, &renderer.material_texture);
    }

    renderer.position_texture = create_gbuffer_texture(width, height, GL_RGB16F, GL_RGB, GL_FLOAT);
    renderer.normal_texture = create_gbuffer_texture(width, height, GL_RGB16F, GL_RGB, GL_FLOAT);
    renderer.albedo_texture = create_gbuffer_texture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    renderer.material_texture = create_gbuffer_texture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

    glBindRenderbuffer(GL_RENDERBUFFER, renderer.depth_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer.position_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, renderer.normal_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, renderer.albedo_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, renderer.material_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer.depth_renderbuffer);
    const GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
    };
    glDrawBuffers(4, draw_buffers);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Deferred framebuffer incomplete: 0x" << std::hex << status << std::dec << '\n';
        renderer.disabled = true;
        return false;
    }

    return true;
}

} // namespace undecedent
