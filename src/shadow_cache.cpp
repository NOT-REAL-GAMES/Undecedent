#include "undecedent/shadow_cache.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace undecedent {
namespace {

constexpr float kPointPositionEpsilon = 0.001F;
constexpr float kPointRadiusEpsilon = 0.001F;
constexpr float kSunDirectionEpsilon = 0.0005F;
constexpr float kSunMatrixEpsilon = 0.0001F;
constexpr float kSunAngleThresholdRadians = 0.25F * 3.14159265F / 180.0F;

bool nearly_equal(const float a, const float b, const float epsilon) {
    return std::abs(a - b) <= epsilon;
}

bool vec3_nearly_equal(const Vec3 a, const Vec3 b, const float epsilon) {
    return nearly_equal(a.x, b.x, epsilon) &&
        nearly_equal(a.y, b.y, epsilon) &&
        nearly_equal(a.z, b.z, epsilon);
}

float wrapped_angle_delta(float a, float b) {
    constexpr float two_pi = 6.28318530F;
    float delta = std::fmod(a - b, two_pi);
    if (delta > 3.14159265F) {
        delta -= two_pi;
    } else if (delta < -3.14159265F) {
        delta += two_pi;
    }
    return std::abs(delta);
}

bool matrices_nearly_equal(
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& a,
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& b
) {
    for (std::size_t cascade = 0; cascade < a.size(); ++cascade) {
        for (std::size_t element = 0; element < a[cascade].size(); ++element) {
            if (!nearly_equal(a[cascade][element], b[cascade][element], kSunMatrixEpsilon)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::uint64_t point_shadow_cache_key(const PointLight& light, const int fallback_index) {
    if (light.id != 0) {
        return light.id;
    }
    return (std::uint64_t{1} << 63) | static_cast<std::uint64_t>(std::max(fallback_index, 0));
}

bool shadow_atlas_faces_equal(
    const std::array<ShadowAtlasRect, kPointShadowFaceCount>& a,
    const std::array<ShadowAtlasRect, kPointShadowFaceCount>& b
) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x ||
            a[i].y != b[i].y ||
            a[i].size != b[i].size) {
            return false;
        }
    }
    return true;
}

bool point_shadow_cache_matches(
    const PointShadowCacheEntry& cache,
    const PointLight& light,
    const int fallback_index,
    const PointShadowAtlasEntry& atlas_entry,
    const std::uint64_t shadow_revision
) {
    return cache.valid &&
        atlas_entry.shadowed &&
        cache.key == point_shadow_cache_key(light, fallback_index) &&
        cache.shadow_revision == shadow_revision &&
        vec3_nearly_equal(cache.position, light.position, kPointPositionEpsilon) &&
        nearly_equal(cache.radius, light.radius, kPointRadiusEpsilon) &&
        shadow_atlas_faces_equal(cache.faces, atlas_entry.faces);
}

PointShadowCacheEntry make_point_shadow_cache_entry(
    const PointLight& light,
    const int fallback_index,
    const PointShadowAtlasEntry& atlas_entry,
    const std::uint64_t shadow_revision
) {
    PointShadowCacheEntry cache;
    cache.key = point_shadow_cache_key(light, fallback_index);
    cache.shadow_revision = shadow_revision;
    cache.position = light.position;
    cache.radius = light.radius;
    cache.faces = atlas_entry.faces;
    cache.valid = atlas_entry.shadowed;
    return cache;
}

bool sun_shadow_cache_matches(
    const SunShadowCacheEntry& cache,
    const std::uint64_t shadow_revision,
    const Vec3 sun_direction,
    const int width,
    const int height,
    const float fov_y_degrees,
    const float near_plane,
    const float far_plane,
    const float yaw,
    const float pitch,
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& matrices
) {
    return cache.valid &&
        cache.shadow_revision == shadow_revision &&
        cache.width == width &&
        cache.height == height &&
        nearly_equal(cache.fov_y_degrees, fov_y_degrees, 0.001F) &&
        nearly_equal(cache.near_plane, near_plane, 0.001F) &&
        nearly_equal(cache.far_plane, far_plane, 0.001F) &&
        vec3_nearly_equal(cache.sun_direction, sun_direction, kSunDirectionEpsilon) &&
        wrapped_angle_delta(cache.yaw, yaw) <= kSunAngleThresholdRadians &&
        wrapped_angle_delta(cache.pitch, pitch) <= kSunAngleThresholdRadians &&
        matrices_nearly_equal(cache.matrices, matrices);
}

SunShadowCacheEntry make_sun_shadow_cache_entry(
    const std::uint64_t shadow_revision,
    const Vec3 sun_direction,
    const int width,
    const int height,
    const float fov_y_degrees,
    const float near_plane,
    const float far_plane,
    const float yaw,
    const float pitch,
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& matrices
) {
    SunShadowCacheEntry cache;
    cache.shadow_revision = shadow_revision;
    cache.sun_direction = sun_direction;
    cache.width = width;
    cache.height = height;
    cache.fov_y_degrees = fov_y_degrees;
    cache.near_plane = near_plane;
    cache.far_plane = far_plane;
    cache.yaw = yaw;
    cache.pitch = pitch;
    cache.matrices = matrices;
    cache.valid = true;
    return cache;
}

} // namespace undecedent
