#include "undecedent/deferred_renderer.hpp"

#include "undecedent/shadow_atlas.hpp"

#include <algorithm>
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
        glBindAttribLocation(program, 4, "aTexCoord");
        glBindAttribLocation(program, 5, "aMaterialSlot");
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

void cache_deferred_uniforms(DeferredRenderer& renderer) {
    const GLuint lighting = renderer.lighting_program;
    renderer.lighting_uniforms.position = glGetUniformLocation(lighting, "uPosition");
    renderer.lighting_uniforms.normal = glGetUniformLocation(lighting, "uNormal");
    renderer.lighting_uniforms.albedo = glGetUniformLocation(lighting, "uAlbedo");
    renderer.lighting_uniforms.material = glGetUniformLocation(lighting, "uMaterial");
    renderer.lighting_uniforms.screen_shadow_mask = glGetUniformLocation(lighting, "uScreenShadowMask");
    renderer.lighting_uniforms.point_shadow_atlas = glGetUniformLocation(lighting, "uPointShadowAtlas");
    renderer.lighting_uniforms.sun_shadow_moments = glGetUniformLocation(lighting, "uSunShadowMoments");
    renderer.lighting_uniforms.inv_viewport = glGetUniformLocation(lighting, "uInvViewport");
    renderer.lighting_uniforms.camera_position = glGetUniformLocation(lighting, "uCameraPosition");
    renderer.lighting_uniforms.ambient_color = glGetUniformLocation(lighting, "uAmbientColor");
    renderer.lighting_uniforms.light_count = glGetUniformLocation(lighting, "uLightCount");
    renderer.lighting_uniforms.point_shadows_enabled = glGetUniformLocation(lighting, "uPointShadowsEnabled");
    renderer.lighting_uniforms.sun_enabled = glGetUniformLocation(lighting, "uSunEnabled");
    renderer.lighting_uniforms.sun_shadow_enabled = glGetUniformLocation(lighting, "uSunShadowEnabled");
    renderer.lighting_uniforms.sun_direction = glGetUniformLocation(lighting, "uSunDirection");
    renderer.lighting_uniforms.sun_color = glGetUniformLocation(lighting, "uSunColor");
    renderer.lighting_uniforms.sun_intensity = glGetUniformLocation(lighting, "uSunIntensity");
    renderer.lighting_uniforms.camera_forward = glGetUniformLocation(lighting, "uCameraForward");
    renderer.lighting_uniforms.sun_shadow_matrices = glGetUniformLocation(lighting, "uSunShadowMatrices[0]");
    renderer.lighting_uniforms.sun_shadow_rects = glGetUniformLocation(lighting, "uSunShadowRects[0]");
    renderer.lighting_uniforms.sun_cascade_splits = glGetUniformLocation(lighting, "uSunCascadeSplits");
    renderer.lighting_uniforms.screen_space_shadows_enabled =
        glGetUniformLocation(lighting, "uScreenSpaceShadowsEnabled");
    renderer.lighting_uniforms.fog_enabled = glGetUniformLocation(lighting, "uFogEnabled");
    renderer.lighting_uniforms.fog_start_end = glGetUniformLocation(lighting, "uFogStartEnd");
    renderer.lighting_uniforms.fog_color = glGetUniformLocation(lighting, "uFogColor");

    if (renderer.screen_shadow_program != 0) {
        renderer.screen_shadow_uniforms.position =
            glGetUniformLocation(renderer.screen_shadow_program, "uPosition");
        renderer.screen_shadow_uniforms.normal =
            glGetUniformLocation(renderer.screen_shadow_program, "uNormal");
        renderer.screen_shadow_uniforms.albedo =
            glGetUniformLocation(renderer.screen_shadow_program, "uAlbedo");
        renderer.screen_shadow_uniforms.inv_viewport =
            glGetUniformLocation(renderer.screen_shadow_program, "uInvViewport");
        renderer.screen_shadow_uniforms.camera_position =
            glGetUniformLocation(renderer.screen_shadow_program, "uCameraPosition");
        renderer.screen_shadow_uniforms.camera_forward =
            glGetUniformLocation(renderer.screen_shadow_program, "uCameraForward");
        renderer.screen_shadow_uniforms.sun_direction =
            glGetUniformLocation(renderer.screen_shadow_program, "uSunDirection");
        renderer.screen_shadow_uniforms.view_projection_matrix =
            glGetUniformLocation(renderer.screen_shadow_program, "uViewProjectionMatrix");
    }

    if (renderer.point_shadow_program != 0) {
        renderer.point_shadow_uniforms.light_position =
            glGetUniformLocation(renderer.point_shadow_program, "uLightPosition");
        renderer.point_shadow_uniforms.light_radius =
            glGetUniformLocation(renderer.point_shadow_program, "uLightRadius");
        renderer.point_shadow_uniforms.light_matrix =
            glGetUniformLocation(renderer.point_shadow_program, "uLightMatrix");
    }
    if (renderer.sun_shadow_program != 0) {
        renderer.sun_shadow_uniforms.light_matrix =
            glGetUniformLocation(renderer.sun_shadow_program, "uLightMatrix");
    }
}

bool create_deferred_programs(DeferredRenderer& renderer) {
    static constexpr const char* geometry_vertex = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aMaterial;
layout(location = 4) in vec2 aTexCoord;
layout(location = 5) in float aMaterialSlot;
uniform mat4 uViewProjection;
out vec3 vWorldPosition;
out vec3 vNormal;
out vec3 vAlbedo;
out vec3 vMaterial;
out vec2 vTexCoord;
flat out int vMaterialSlot;
void main() {
    vWorldPosition = aPosition;
    vNormal = normalize(aNormal);
    vAlbedo = aColor;
    vMaterial = aMaterial;
    vTexCoord = aTexCoord;
    vMaterialSlot = int(clamp(floor(aMaterialSlot + 0.5), 0.0, 7.0));
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

    static constexpr const char* geometry_fragment = R"(
#version 430 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vAlbedo;
in vec3 vMaterial;
in vec2 vTexCoord;
flat in int vMaterialSlot;
uniform sampler2DArray uMaterialAlbedo;
layout(location = 0) out vec4 oPosition;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oAlbedo;
layout(location = 3) out vec4 oMaterial;
void main() {
    oPosition = vec4(vWorldPosition, 1.0);
    oNormal = vec4((normalize(vNormal) * 0.5) + 0.5, 1.0);
    vec3 texel = texture(uMaterialAlbedo, vec3(vTexCoord, float(vMaterialSlot))).rgb;
    oAlbedo = vec4(vAlbedo * texel, 1.0);
    oMaterial = vec4(vMaterial, 1.0);
}
)";

    static constexpr const char* lighting_vertex = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
void main() {
    gl_Position = vec4(aPosition, 1.0);
}
)";

    static constexpr const char* lighting_fragment = R"(
#version 430 core
uniform sampler2D uPosition;
uniform sampler2D uNormal;
uniform sampler2D uAlbedo;
uniform sampler2D uMaterial;
uniform sampler2D uScreenShadowMask;
uniform sampler2D uPointShadowAtlas;
uniform sampler2D uSunShadowMoments;
uniform vec2 uInvViewport;
uniform vec3 uCameraPosition;
uniform vec3 uAmbientColor;
uniform int uLightCount;
uniform bool uPointShadowsEnabled;
uniform bool uSunEnabled;
uniform bool uSunShadowEnabled;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uCameraForward;
uniform mat4 uSunShadowMatrices[4];
uniform vec4 uSunShadowRects[4];
uniform vec4 uSunCascadeSplits;
uniform bool uScreenSpaceShadowsEnabled;
uniform bool uFogEnabled;
uniform vec2 uFogStartEnd;
uniform vec3 uFogColor;
layout(location = 0) out vec4 oColor;
const int MAX_POINT_LIGHTS = 4096;
const int POINT_SHADOW_FACE_COUNT = 6;
struct PointLightRecord {
    vec4 position_radius;
    vec4 color_intensity;
    vec4 shadow_flags;
};
struct PointShadowFaceRecord {
    vec4 rect;
    mat4 matrix;
};
layout(std430, binding = 0) readonly buffer PointLightBuffer {
    PointLightRecord uPointLights[];
};
layout(std430, binding = 1) readonly buffer PointShadowFaceBuffer {
    PointShadowFaceRecord uPointShadowFaces[];
};
const float PI = 3.14159265358979323846;
float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}
const float EVSM_EXPONENT = 8.0;
const float EVSM_HARD_OCCLUDER_START = 0.0001;
const float EVSM_HARD_OCCLUDER_END = 0.0012;
const float EVSM_MIN_VARIANCE = 0.000002;
const float EVSM_BLEED_REDUCTION = 0.68;
const float POINT_SHADOW_ATLAS_TEXEL = 0.0001220703125;
const float SUN_SHADOW_ATLAS_TEXEL = 0.000244140625;
const float SUN_SHADOW_DEPTH_BIAS = 0.004;
float evsm_depth(float depth) {
    return exp(EVSM_EXPONENT * (clamp(depth, 0.0, 1.0) - 1.0));
}
float reduce_light_bleeding(float p_max) {
    return clamp((p_max - EVSM_BLEED_REDUCTION) / (1.0 - EVSM_BLEED_REDUCTION), 0.0, 1.0);
}
float evsm_visibility(vec2 moments, float receiver_depth, float depth_bias) {
    const float biased_receiver_depth = clamp(receiver_depth - depth_bias, 0.0, 1.0);
    const float receiver = evsm_depth(biased_receiver_depth);
    if (receiver <= moments.x) {
        return 1.0;
    }
    float mean_depth = 1.0 + (log(max(moments.x, 0.000001)) / EVSM_EXPONENT);
    float hard_visibility = 1.0 - smoothstep(
        EVSM_HARD_OCCLUDER_START,
        EVSM_HARD_OCCLUDER_END,
        biased_receiver_depth - mean_depth
    );
    float variance = max(moments.y - (moments.x * moments.x), EVSM_MIN_VARIANCE);
    float d = receiver - moments.x;
    float p_max = variance / (variance + (d * d));
    return min(reduce_light_bleeding(p_max), hard_visibility);
}
float point_shadow_visibility(int light_index, vec3 light_vector, float light_distance, float range) {
    if (!uPointShadowsEnabled || uPointLights[light_index].shadow_flags.x < 0.5 || range <= 1.0) {
        return 1.0;
    }
    vec3 light_to_fragment = -light_vector;
    vec3 abs_vector = abs(light_to_fragment);
    int face = 0;
    if (abs_vector.x >= abs_vector.y && abs_vector.x >= abs_vector.z) {
        face = light_to_fragment.x >= 0.0 ? 0 : 1;
    } else if (abs_vector.y >= abs_vector.x && abs_vector.y >= abs_vector.z) {
        face = light_to_fragment.y >= 0.0 ? 2 : 3;
    } else {
        face = light_to_fragment.z >= 0.0 ? 4 : 5;
    }
    int shadow_index = (light_index * POINT_SHADOW_FACE_COUNT) + face;
    vec3 light_position = uPointLights[light_index].position_radius.xyz;
    vec4 clip = uPointShadowFaces[shadow_index].matrix * vec4(light_position + light_to_fragment, 1.0);
    vec3 projected = (clip.xyz / clip.w) * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }
    float receiver_depth = clamp(light_distance / range, 0.0, 1.0);
    float depth_bias = max(uPointLights[light_index].shadow_flags.y, 0.0) / range;
    vec4 rect = uPointShadowFaces[shadow_index].rect;
    vec2 atlas_span = max(rect.zw - vec2(POINT_SHADOW_ATLAS_TEXEL * 2.0), vec2(0.0));
    vec2 atlas_uv = rect.xy + vec2(POINT_SHADOW_ATLAS_TEXEL) + (projected.xy * atlas_span);
    vec2 moments = texture(uPointShadowAtlas, atlas_uv).rg;
    return evsm_visibility(moments, receiver_depth, depth_bias);
}
float sun_shadow_cascade_visibility(int cascade_index, vec3 world_position) {
    vec4 clip = uSunShadowMatrices[cascade_index] * vec4(world_position, 1.0);
    vec3 projected = (clip.xyz / clip.w) * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }
    vec4 rect = uSunShadowRects[cascade_index];
    vec2 atlas_span = max(rect.zw - vec2(SUN_SHADOW_ATLAS_TEXEL * 2.0), vec2(0.0));
    vec2 atlas_uv = rect.xy + vec2(SUN_SHADOW_ATLAS_TEXEL) + (projected.xy * atlas_span);
    vec2 moments = texture(uSunShadowMoments, atlas_uv).rg;
    return evsm_visibility(moments, projected.z, SUN_SHADOW_DEPTH_BIAS);
}
float sun_shadow_visibility(vec3 world_position, float camera_forward_distance) {
    if (!uSunEnabled || !uSunShadowEnabled) {
        return 1.0;
    }
    float distance = max(camera_forward_distance, 0.0);
    if (distance > uSunCascadeSplits.w) {
        return 1.0;
    }
    int cascade_index = 0;
    float cascade_start = 0.0;
    float cascade_end = uSunCascadeSplits.x;
    if (distance > uSunCascadeSplits.z) {
        cascade_index = 3;
        cascade_start = uSunCascadeSplits.z;
        cascade_end = uSunCascadeSplits.w;
    } else if (distance > uSunCascadeSplits.y) {
        cascade_index = 2;
        cascade_start = uSunCascadeSplits.y;
        cascade_end = uSunCascadeSplits.z;
    } else if (distance > uSunCascadeSplits.x) {
        cascade_index = 1;
        cascade_start = uSunCascadeSplits.x;
        cascade_end = uSunCascadeSplits.y;
    }
    float visibility = sun_shadow_cascade_visibility(cascade_index, world_position);
    float fade_width = max((cascade_end - cascade_start) * 0.08, 128.0);
    float fade = smoothstep(cascade_end - fade_width, cascade_end, distance);
    if (cascade_index < 3 && fade > 0.0) {
        visibility = mix(visibility, sun_shadow_cascade_visibility(cascade_index + 1, world_position), fade);
    } else if (cascade_index == 3) {
        visibility = mix(visibility, 1.0, fade);
    }
    return visibility;
}
float diffuse_shadow_visibility(float shadow) {
    return smoothstep(0.65, 1.0, shadow);
}
float specular_shadow_visibility(float shadow) {
    return smoothstep(0.82, 1.0, shadow);
}
float diffuse_light_lobe(float ndotl) {
    return ndotl * smoothstep(0.12, 0.45, ndotl);
}
float specular_light_lobe(float ndotl) {
    return ndotl * smoothstep(0.35, 0.85, ndotl);
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
    vec3 world_position = texture(uPosition, uv).xyz;
    vec3 encoded_normal = texture(uNormal, uv).xyz;
    vec3 albedo = texture(uAlbedo, uv).xyz;
    vec3 material = texture(uMaterial, uv).xyz;
    if (dot(albedo, albedo) <= 0.000001) {
        oColor = vec4(0.02, 0.025, 0.03, 1.0);
        return;
    }

    float roughness = clamp(material.r, 0.04, 1.0);
    float metallic = clamp(material.g, 0.0, 1.0);
    float specular = clamp(material.b, 0.0, 1.0);
    vec3 view_dir = normalize(uCameraPosition - world_position);
    vec3 normal = normalize((encoded_normal * 2.0) - 1.0);
    if (dot(normal, view_dir) < 0.0) {
        normal = -normal;
    }
    float ndotv = max(dot(normal, view_dir), 0.0001);
    vec3 f0 = mix(vec3(specular * 0.08), albedo, metallic);
    vec3 diffuse_color = albedo * (1.0 - metallic);
    vec3 color = diffuse_color * uAmbientColor;
    float camera_forward_distance = dot(world_position - uCameraPosition, normalize(uCameraForward));
    if (uSunEnabled && uSunIntensity > 0.0) {
        vec3 sun_dir = normalize(-uSunDirection);
        vec3 sun_half = normalize(sun_dir + view_dir);
        float sun_ndotl = max(dot(normal, sun_dir), 0.0);
        float sun_ndoth = max(dot(normal, sun_half), 0.0);
        float sun_hdotl = max(dot(sun_half, sun_dir), 0.0);
        float distribution = distribution_ggx(sun_ndoth, roughness);
        float visibility = visibility_smith_ggx(ndotv, sun_ndotl, roughness);
        vec3 fresnel = fresnel_schlick(sun_hdotl, f0);
        vec3 specular_lobe = distribution * visibility * fresnel;
        vec3 diffuse_lobe = diffuse_color / PI;
        float shadow = sun_shadow_visibility(world_position, camera_forward_distance);
        if (uScreenSpaceShadowsEnabled) {
            shadow = min(shadow, texture(uScreenShadowMask, uv).r);
        }
        float diffuse_shadow = diffuse_shadow_visibility(shadow);
        float specular_shadow = specular_shadow_visibility(shadow);
        float diffuse_ndotl = diffuse_light_lobe(sun_ndotl * diffuse_shadow);
        float specular_ndotl = specular_light_lobe(sun_ndotl * specular_shadow);
        color += ((diffuse_lobe * diffuse_ndotl) +
            (specular_lobe * specular_ndotl)) * uSunColor * uSunIntensity;
    }
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= uLightCount) {
            break;
        }
        PointLightRecord light = uPointLights[i];
        vec3 light_position = light.position_radius.xyz;
        vec3 light_vector = light_position - world_position;
        float light_distance = length(light_vector);
        vec3 light_dir = light_distance > 0.0001 ? light_vector / light_distance : vec3(0.0, 1.0, 0.0);
        vec3 half_dir = normalize(light_dir + view_dir);
        float ndotl = max(dot(normal, light_dir), 0.0);
        float ndoth = max(dot(normal, half_dir), 0.0);
        float hdotl = max(dot(half_dir, light_dir), 0.0);
        float range = max(light.position_radius.w, 1.0);
        float attenuation = clamp(1.0 - (light_distance / range), 0.0, 1.0);
        attenuation *= attenuation;
        vec3 radiance = light.color_intensity.rgb * attenuation * light.color_intensity.a;
        float distribution = distribution_ggx(ndoth, roughness);
        float visibility = visibility_smith_ggx(ndotv, ndotl, roughness);
        vec3 fresnel = fresnel_schlick(hdotl, f0);
        vec3 specular_lobe = distribution * visibility * fresnel;
        vec3 diffuse_lobe = diffuse_color / PI;
        float shadow = point_shadow_visibility(i, light_vector, light_distance, range);
        float diffuse_shadow = diffuse_shadow_visibility(shadow);
        float specular_shadow = specular_shadow_visibility(shadow);
        float diffuse_ndotl = diffuse_light_lobe(ndotl * diffuse_shadow);
        float specular_ndotl = specular_light_lobe(ndotl * specular_shadow);
        color += ((diffuse_lobe * diffuse_ndotl) +
            (specular_lobe * specular_ndotl)) * radiance;
    }
    if (uFogEnabled) {
        float fog_distance = length(world_position - uCameraPosition);
        float fog = smoothstep(uFogStartEnd.x, uFogStartEnd.y, fog_distance);
        color = mix(color, uFogColor, fog);
    }
    oColor = vec4(color, 1.0);
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
    if (renderer.lighting_program == 0) {
        return false;
    }

    static constexpr const char* screen_shadow_fragment = R"(
#version 430 core
uniform sampler2D uPosition;
uniform sampler2D uNormal;
uniform sampler2D uAlbedo;
uniform vec2 uInvViewport;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uSunDirection;
uniform mat4 uViewProjectionMatrix;
layout(location = 0) out vec4 oColor;
const int SCREEN_SHADOW_MIN_STEPS = 8;
const int SCREEN_SHADOW_MAX_STEPS = 24;
const float SCREEN_SHADOW_DISTANCE = 112.0;
const float SCREEN_SHADOW_NORMAL_BIAS = 0.75;
const float SCREEN_SHADOW_DEPTH_BIAS = 0.75;
const float SCREEN_SHADOW_MAX_THICKNESS = 48.0;
const float SCREEN_SHADOW_MIN_VIEW_DEPTH = 0.25;
const float SCREEN_SHADOW_STRENGTH = 0.88;
const float SCREEN_SHADOW_EDGE_FADE_START = -0.015;
const float SCREEN_SHADOW_EDGE_FADE_END = 0.035;
const float SCREEN_SHADOW_OVERSCAN = 0.35;
float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}
float hash12(vec2 value) {
    vec3 p3 = fract(vec3(value.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
bool project_to_screen(vec3 position, out vec2 screen_uv) {
    vec4 clip = uViewProjectionMatrix * vec4(position, 1.0);
    if (clip.w <= 0.0001) {
        return false;
    }
    vec3 projected = (clip.xyz / clip.w) * 0.5 + 0.5;
    if (projected.x < -SCREEN_SHADOW_OVERSCAN || projected.x > 1.0 + SCREEN_SHADOW_OVERSCAN ||
        projected.y < -SCREEN_SHADOW_OVERSCAN || projected.y > 1.0 + SCREEN_SHADOW_OVERSCAN ||
        projected.z < -0.15 || projected.z > 1.15) {
        return false;
    }
    screen_uv = clamp(projected.xy, vec2(0.0), vec2(1.0));
    return true;
}
float screen_edge_fade(vec2 uv) {
    float edge_distance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    return smoothstep(SCREEN_SHADOW_EDGE_FADE_START, SCREEN_SHADOW_EDGE_FADE_END, edge_distance);
}
void main() {
    vec2 uv = gl_FragCoord.xy * uInvViewport;
    vec3 albedo = texture(uAlbedo, uv).xyz;
    if (dot(albedo, albedo) <= 0.000001) {
        oColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 world_position = texture(uPosition, uv).xyz;
    vec3 view_dir = normalize(uCameraPosition - world_position);
    vec3 normal = normalize((texture(uNormal, uv).xyz * 2.0) - 1.0);
    if (dot(normal, view_dir) < 0.0) {
        normal = -normal;
    }
    vec3 sun_dir = normalize(-uSunDirection);
    float receiver_depth = dot(world_position - uCameraPosition, uCameraForward);
    if (receiver_depth <= SCREEN_SHADOW_MIN_VIEW_DEPTH) {
        oColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float normal_bias = clamp(receiver_depth * 0.01, 0.08, SCREEN_SHADOW_NORMAL_BIAS);
    float depth_bias = clamp(receiver_depth * 0.004, 0.12, SCREEN_SHADOW_DEPTH_BIAS);
    vec3 ray_origin = world_position + (normal * normal_bias);
    vec3 ray_end = ray_origin + (sun_dir * SCREEN_SHADOW_DISTANCE);
    vec2 end_uv = uv;
    bool has_end = project_to_screen(ray_end, end_uv);
    float pixel_span = has_end ? length((end_uv - uv) / max(uInvViewport, vec2(0.000001))) : 256.0;
    int steps = int(clamp(floor(pixel_span * 0.075) + float(SCREEN_SHADOW_MIN_STEPS),
        float(SCREEN_SHADOW_MIN_STEPS),
        float(SCREEN_SHADOW_MAX_STEPS)));
    float jitter = (hash12(gl_FragCoord.xy) - 0.5) / float(steps);
    float occlusion = 0.0;
    for (int step = 1; step <= SCREEN_SHADOW_MAX_STEPS; ++step) {
        if (step > steps) {
            break;
        }
        float fraction = saturate((float(step) / float(steps)) + jitter);
        vec3 ray_position = ray_origin + (sun_dir * SCREEN_SHADOW_DISTANCE * fraction);
        vec2 sample_uv;
        if (!project_to_screen(ray_position, sample_uv)) {
            break;
        }
        float sample_edge_fade = screen_edge_fade(sample_uv);
        if (sample_edge_fade <= 0.0) {
            break;
        }
        vec3 sample_albedo = texture(uAlbedo, sample_uv).xyz;
        if (dot(sample_albedo, sample_albedo) <= 0.000001) {
            continue;
        }
        vec3 sample_position = texture(uPosition, sample_uv).xyz;
        float ray_depth = dot(ray_position - uCameraPosition, uCameraForward);
        float sample_depth = dot(sample_position - uCameraPosition, uCameraForward);
        if (ray_depth <= SCREEN_SHADOW_MIN_VIEW_DEPTH || sample_depth <= SCREEN_SHADOW_MIN_VIEW_DEPTH) {
            continue;
        }
        float depth_delta = ray_depth - sample_depth;
        if (depth_delta <= depth_bias || depth_delta > SCREEN_SHADOW_MAX_THICKNESS) {
            continue;
        }
        float hit = smoothstep(depth_bias, depth_bias * 4.0, depth_delta);
        float thickness_fade = 1.0 - smoothstep(
            SCREEN_SHADOW_MAX_THICKNESS * 0.55,
            SCREEN_SHADOW_MAX_THICKNESS,
            depth_delta
        );
        float distance_fade = 1.0 - smoothstep(0.72, 1.0, fraction);
        occlusion = max(occlusion, hit * thickness_fade * distance_fade * sample_edge_fade);
    }

    float visibility = 1.0 - (occlusion * SCREEN_SHADOW_STRENGTH);
    oColor = vec4(saturate(visibility), 1.0, 1.0, 1.0);
}
)";

    renderer.screen_shadow_program = create_shader_program(
        lighting_vertex,
        screen_shadow_fragment,
        "screen-space sun shadow",
        false
    );
    if (renderer.screen_shadow_program == 0) {
        renderer.screen_shadows_disabled = true;
        std::cerr << "Screen-space sun shadows disabled; falling back to EVSM-only sun shadows.\n";
    }

    static constexpr const char* shadow_vertex = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightMatrix;
out vec3 vWorldPosition;
void main() {
    vWorldPosition = aPosition;
    gl_Position = uLightMatrix * vec4(aPosition, 1.0);
}
)";

    static constexpr const char* point_shadow_fragment = R"(
#version 430 core
in vec3 vWorldPosition;
uniform vec3 uLightPosition;
uniform float uLightRadius;
layout(location = 0) out vec2 oMoments;
const float EVSM_EXPONENT = 8.0;
float evsm_depth(float depth) {
    return exp(EVSM_EXPONENT * (clamp(depth, 0.0, 1.0) - 1.0));
}
void main() {
    float depth = clamp(length(vWorldPosition - uLightPosition) / max(uLightRadius, 1.0), 0.0, 1.0);
    float warped = evsm_depth(depth);
    oMoments = vec2(warped, warped * warped);
}
)";

    static constexpr const char* sun_shadow_fragment = R"(
#version 430 core
layout(location = 0) out vec2 oMoments;
const float EVSM_EXPONENT = 8.0;
float evsm_depth(float depth) {
    return exp(EVSM_EXPONENT * (clamp(depth, 0.0, 1.0) - 1.0));
}
void main() {
    float depth = gl_FragCoord.z;
    float warped = evsm_depth(depth);
    oMoments = vec2(warped, warped * warped);
}
)";

    renderer.point_shadow_program = create_shader_program(
        shadow_vertex,
        point_shadow_fragment,
        "point VSM shadow",
        false
    );
    if (renderer.point_shadow_program == 0) {
        renderer.shadows_disabled = true;
        return true;
    }
    renderer.sun_shadow_program = create_shader_program(
        shadow_vertex,
        sun_shadow_fragment,
        "sun VSM shadow",
        false
    );
    if (renderer.sun_shadow_program == 0) {
        glDeleteProgram(renderer.point_shadow_program);
        renderer.point_shadow_program = 0;
        renderer.shadows_disabled = true;
    }
    renderer.geometry_uniforms.view_projection = glGetUniformLocation(renderer.geometry_program, "uViewProjection");
    renderer.geometry_uniforms.material_albedo = glGetUniformLocation(renderer.geometry_program, "uMaterialAlbedo");
    renderer.geometry_view_projection = renderer.geometry_uniforms.view_projection;
    cache_deferred_uniforms(renderer);
    return true;
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
    if (renderer.screen_shadow_texture != 0) {
        glDeleteTextures(1, &renderer.screen_shadow_texture);
    }
    if (renderer.point_shadow_texture != 0) {
        glDeleteTextures(1, &renderer.point_shadow_texture);
    }
    if (renderer.sun_shadow_texture != 0) {
        glDeleteTextures(1, &renderer.sun_shadow_texture);
    }
    if (renderer.point_light_buffer != 0) {
        glDeleteBuffers(1, &renderer.point_light_buffer);
    }
    if (renderer.point_shadow_face_buffer != 0) {
        glDeleteBuffers(1, &renderer.point_shadow_face_buffer);
    }
    if (renderer.fullscreen_vbo != 0) {
        glDeleteBuffers(1, &renderer.fullscreen_vbo);
    }
    if (renderer.geometry_vao != 0) {
        glDeleteVertexArrays(1, &renderer.geometry_vao);
    }
    if (renderer.shadow_vao != 0) {
        glDeleteVertexArrays(1, &renderer.shadow_vao);
    }
    if (renderer.fullscreen_vao != 0) {
        glDeleteVertexArrays(1, &renderer.fullscreen_vao);
    }
    if (renderer.depth_renderbuffer != 0) {
        glDeleteRenderbuffers(1, &renderer.depth_renderbuffer);
    }
    if (renderer.point_shadow_depth_renderbuffer != 0) {
        glDeleteRenderbuffers(1, &renderer.point_shadow_depth_renderbuffer);
    }
    if (renderer.sun_shadow_depth_renderbuffer != 0) {
        glDeleteRenderbuffers(1, &renderer.sun_shadow_depth_renderbuffer);
    }
    if (renderer.framebuffer != 0) {
        glDeleteFramebuffers(1, &renderer.framebuffer);
    }
    if (renderer.shadow_framebuffer != 0) {
        glDeleteFramebuffers(1, &renderer.shadow_framebuffer);
    }
    if (renderer.screen_shadow_framebuffer != 0) {
        glDeleteFramebuffers(1, &renderer.screen_shadow_framebuffer);
    }
    if (renderer.geometry_program != 0) {
        glDeleteProgram(renderer.geometry_program);
    }
    if (renderer.lighting_program != 0) {
        glDeleteProgram(renderer.lighting_program);
    }
    if (renderer.screen_shadow_program != 0) {
        glDeleteProgram(renderer.screen_shadow_program);
    }
    if (renderer.point_shadow_program != 0) {
        glDeleteProgram(renderer.point_shadow_program);
    }
    if (renderer.sun_shadow_program != 0) {
        glDeleteProgram(renderer.sun_shadow_program);
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
        glGenFramebuffers(1, &renderer.screen_shadow_framebuffer);
        glGenFramebuffers(1, &renderer.shadow_framebuffer);
        glGenRenderbuffers(1, &renderer.depth_renderbuffer);
        glGenRenderbuffers(1, &renderer.point_shadow_depth_renderbuffer);
        glGenRenderbuffers(1, &renderer.sun_shadow_depth_renderbuffer);
        glGenBuffers(1, &renderer.point_light_buffer);
        glGenBuffers(1, &renderer.point_shadow_face_buffer);
        glGenVertexArrays(1, &renderer.geometry_vao);
        glGenVertexArrays(1, &renderer.shadow_vao);
        glGenVertexArrays(1, &renderer.fullscreen_vao);
        glGenBuffers(1, &renderer.fullscreen_vbo);
        if (renderer.fullscreen_vao != 0 && renderer.fullscreen_vbo != 0) {
            constexpr std::array<float, 12> fullscreen_vertices{
                -1.0F, -1.0F, 0.0F,
                1.0F, -1.0F, 0.0F,
                1.0F, 1.0F, 0.0F,
                -1.0F, 1.0F, 0.0F,
            };
            glBindVertexArray(renderer.fullscreen_vao);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.fullscreen_vbo);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(fullscreen_vertices.size() * sizeof(float)),
                fullscreen_vertices.data(),
                GL_STATIC_DRAW
            );
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }
        renderer.initialized = true;
    }

    const bool shadow_resources_ready =
        renderer.shadows_disabled ||
        (renderer.point_shadow_texture != 0 &&
            renderer.sun_shadow_texture != 0 &&
            renderer.point_shadow_depth_size == kPointShadowAtlasSize &&
            renderer.sun_shadow_depth_size == kSunShadowAtlasSize &&
            renderer.point_shadow_atlas_size == kPointShadowAtlasSize &&
            renderer.sun_shadow_resolution == kSunShadowAtlasSize);

    if (renderer.width == width && renderer.height == height &&
        renderer.position_texture != 0 && renderer.normal_texture != 0 &&
        renderer.albedo_texture != 0 && renderer.material_texture != 0 &&
        (renderer.screen_shadows_disabled || renderer.screen_shadow_texture != 0) &&
        shadow_resources_ready) {
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
    if (renderer.screen_shadow_texture != 0) {
        glDeleteTextures(1, &renderer.screen_shadow_texture);
        renderer.screen_shadow_texture = 0;
    }

    renderer.position_texture = create_gbuffer_texture(width, height, GL_RGB16F, GL_RGB, GL_FLOAT);
    renderer.normal_texture = create_gbuffer_texture(width, height, GL_RGB16F, GL_RGB, GL_FLOAT);
    renderer.albedo_texture = create_gbuffer_texture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    renderer.material_texture = create_gbuffer_texture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

    if (!renderer.screen_shadows_disabled) {
        renderer.screen_shadow_texture = create_gbuffer_texture(width, height, GL_R8, GL_RED, GL_UNSIGNED_BYTE);
        if (renderer.screen_shadow_texture == 0 || glGetError() != GL_NO_ERROR) {
            std::cerr << "Screen-space shadow mask allocation failed; falling back to EVSM-only sun shadows.\n";
            renderer.screen_shadows_disabled = true;
            if (renderer.screen_shadow_texture != 0) {
                glDeleteTextures(1, &renderer.screen_shadow_texture);
            }
            renderer.screen_shadow_texture = 0;
        }
    }

    if (!renderer.shadows_disabled &&
        (renderer.point_shadow_texture == 0 ||
            renderer.sun_shadow_texture == 0 ||
            renderer.point_shadow_atlas_size != kPointShadowAtlasSize ||
            renderer.sun_shadow_resolution != kSunShadowAtlasSize)) {
        GLint max_texture_size = 0;
        GLint max_renderbuffer_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_renderbuffer_size);
        const int required_shadow_size = std::max(kPointShadowAtlasSize, kSunShadowAtlasSize);
        if (max_texture_size < required_shadow_size || max_renderbuffer_size < required_shadow_size) {
            std::cerr << "Deferred shadow atlas disabled: requested " << required_shadow_size
                      << " but GL_MAX_TEXTURE_SIZE=" << max_texture_size
                      << " GL_MAX_RENDERBUFFER_SIZE=" << max_renderbuffer_size << ".\n";
            renderer.shadows_disabled = true;
        }
        if (!renderer.shadows_disabled) {
            if (renderer.point_shadow_texture != 0) {
                glDeleteTextures(1, &renderer.point_shadow_texture);
            }
            if (renderer.sun_shadow_texture != 0) {
                glDeleteTextures(1, &renderer.sun_shadow_texture);
            }

            renderer.point_shadow_atlas_size = kPointShadowAtlasSize;
            renderer.sun_shadow_resolution = kSunShadowAtlasSize;
            renderer.point_shadow_cache.clear();
            renderer.sun_shadow_cache.valid = false;

            glGenTextures(1, &renderer.point_shadow_texture);
            glBindTexture(GL_TEXTURE_2D, renderer.point_shadow_texture);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RG32F,
                kPointShadowAtlasSize,
                kPointShadowAtlasSize,
                0,
                GL_RG,
                GL_FLOAT,
                nullptr
            );
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const GLenum point_error = glGetError();
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenTextures(1, &renderer.sun_shadow_texture);
            glBindTexture(GL_TEXTURE_2D, renderer.sun_shadow_texture);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RG32F,
                kSunShadowAtlasSize,
                kSunShadowAtlasSize,
                0,
                GL_RG,
                GL_FLOAT,
                nullptr
            );
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const GLenum sun_error = glGetError();
            glBindTexture(GL_TEXTURE_2D, 0);

            if (renderer.point_shadow_texture == 0 || renderer.sun_shadow_texture == 0 ||
                point_error != GL_NO_ERROR || sun_error != GL_NO_ERROR) {
                std::cerr << "Deferred shadow texture allocation failed"
                          << " point_error=0x" << std::hex << point_error
                          << " sun_error=0x" << sun_error << std::dec << ".\n";
                renderer.shadows_disabled = true;
            }
        }
    }

    if (!renderer.shadows_disabled && renderer.point_shadow_depth_size != kPointShadowAtlasSize) {
        glBindRenderbuffer(GL_RENDERBUFFER, renderer.point_shadow_depth_renderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            kPointShadowAtlasSize,
            kPointShadowAtlasSize
        );
        renderer.point_shadow_depth_size = kPointShadowAtlasSize;
        renderer.point_shadow_cache.clear();
    }

    if (!renderer.shadows_disabled && renderer.sun_shadow_depth_size != kSunShadowAtlasSize) {
        glBindRenderbuffer(GL_RENDERBUFFER, renderer.sun_shadow_depth_renderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            kSunShadowAtlasSize,
            kSunShadowAtlasSize
        );
        renderer.sun_shadow_depth_size = kSunShadowAtlasSize;
        renderer.sun_shadow_cache.valid = false;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

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

    if (!renderer.screen_shadows_disabled && renderer.screen_shadow_texture != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, renderer.screen_shadow_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer.screen_shadow_texture, 0);
        const GLenum screen_draw_buffer = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &screen_draw_buffer);
        const GLenum screen_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (screen_status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Screen-space shadow framebuffer incomplete: 0x"
                      << std::hex << screen_status << std::dec
                      << "; falling back to EVSM-only sun shadows.\n";
            renderer.screen_shadows_disabled = true;
            glDeleteTextures(1, &renderer.screen_shadow_texture);
            renderer.screen_shadow_texture = 0;
        }
    }

    return true;
}

} // namespace undecedent
