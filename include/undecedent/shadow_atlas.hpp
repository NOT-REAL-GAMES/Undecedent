#pragma once

#include "undecedent/geometry.hpp"

#include <array>
#include <vector>

namespace undecedent {

constexpr int kPointShadowFaceCount = 6;
constexpr int kPointShadowAtlasSize = 8192;
constexpr int kPointShadowTileTierCount = 3;
constexpr std::array<int, kPointShadowTileTierCount> kPointShadowTileTiers{512, 256, 128};
constexpr int kPointShadowMinTileSize = kPointShadowTileTiers[kPointShadowTileTierCount - 1];
constexpr int kPointShadowAtlasMinTileCount =
    (kPointShadowAtlasSize / kPointShadowMinTileSize) * (kPointShadowAtlasSize / kPointShadowMinTileSize);
constexpr int kMaxDeferredPointLights = 4096;
constexpr int kMaxPointShadowedLights = 512;
constexpr int kSunShadowResolution = 2048;
constexpr int kSunShadowCascadeCount = 4;
constexpr int kSunShadowCascadeGrid = 2;
constexpr int kSunShadowAtlasSize = kSunShadowResolution * kSunShadowCascadeGrid;

struct ShadowAtlasRect {
    int x = 0;
    int y = 0;
    int size = 0;
};

struct PointShadowAtlasEntry {
    bool shadowed = false;
    int light_index = -1;
    int tile_size = 0;
    std::array<ShadowAtlasRect, kPointShadowFaceCount> faces{};
};

struct PackedPointShadowAtlas {
    int atlas_size = kPointShadowAtlasSize;
    int submitted_lights = 0;
    int shadowed_lights = 0;
    std::vector<PointShadowAtlasEntry> entries;
};

PackedPointShadowAtlas pack_point_shadow_atlas(
    const std::vector<PointLight>& lights,
    Vec3 camera_position,
    int max_lights = kMaxDeferredPointLights,
    int max_shadowed_lights = kMaxPointShadowedLights,
    int atlas_size = kPointShadowAtlasSize
);

bool shadow_atlas_rects_overlap(ShadowAtlasRect a, ShadowAtlasRect b);

} // namespace undecedent
