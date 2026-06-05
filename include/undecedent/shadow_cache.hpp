#pragma once

#include "undecedent/geometry.hpp"
#include "undecedent/shadow_atlas.hpp"

#include <array>
#include <cstdint>

namespace undecedent {

struct PointShadowCacheEntry {
    std::uint64_t key = 0;
    std::uint64_t shadow_revision = 0;
    Vec3 position{};
    float radius = 0.0F;
    std::array<ShadowAtlasRect, kPointShadowFaceCount> faces{};
    bool valid = false;
};

struct SunShadowCacheEntry {
    std::uint64_t shadow_revision = 0;
    Vec3 sun_direction{};
    int width = 0;
    int height = 0;
    float fov_y_degrees = 0.0F;
    float near_plane = 0.0F;
    float far_plane = 0.0F;
    float yaw = 0.0F;
    float pitch = 0.0F;
    std::array<std::array<float, 16>, kSunShadowCascadeCount> matrices{};
    bool valid = false;
};

std::uint64_t point_shadow_cache_key(const PointLight& light, int fallback_index);
bool shadow_atlas_faces_equal(
    const std::array<ShadowAtlasRect, kPointShadowFaceCount>& a,
    const std::array<ShadowAtlasRect, kPointShadowFaceCount>& b
);
bool point_shadow_cache_matches(
    const PointShadowCacheEntry& cache,
    const PointLight& light,
    int fallback_index,
    const PointShadowAtlasEntry& atlas_entry,
    std::uint64_t shadow_revision
);
PointShadowCacheEntry make_point_shadow_cache_entry(
    const PointLight& light,
    int fallback_index,
    const PointShadowAtlasEntry& atlas_entry,
    std::uint64_t shadow_revision
);

bool sun_shadow_cache_matches(
    const SunShadowCacheEntry& cache,
    std::uint64_t shadow_revision,
    Vec3 sun_direction,
    int width,
    int height,
    float fov_y_degrees,
    float near_plane,
    float far_plane,
    float yaw,
    float pitch,
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& matrices
);
SunShadowCacheEntry make_sun_shadow_cache_entry(
    std::uint64_t shadow_revision,
    Vec3 sun_direction,
    int width,
    int height,
    float fov_y_degrees,
    float near_plane,
    float far_plane,
    float yaw,
    float pitch,
    const std::array<std::array<float, 16>, kSunShadowCascadeCount>& matrices
);

} // namespace undecedent
