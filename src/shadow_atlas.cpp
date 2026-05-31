#include "undecedent/shadow_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace undecedent {
namespace {

struct RankedLight {
    int index = -1;
    float score = 0.0F;
};

struct OccupancyAtlas {
    int atlas_size = 0;
    int cell_size = 0;
    int cells_per_side = 0;
    std::vector<unsigned char> occupied;
};

float distance_to_camera(const PointLight& light, const Vec3 camera_position) {
    const float dx = light.position.x - camera_position.x;
    const float dy = light.position.y - camera_position.y;
    const float dz = light.position.z - camera_position.z;
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

float shadow_priority(const PointLight& light, const Vec3 camera_position) {
    const float distance = std::max(distance_to_camera(light, camera_position), 1.0F);
    return std::max(light.intensity, 0.0F) * std::max(light.radius, 0.0F) / distance;
}

int light_capacity_for_tile(const int tile_size, const int atlas_size) {
    if (tile_size <= 0 || atlas_size < tile_size) {
        return 0;
    }
    const int tiles_per_side = atlas_size / tile_size;
    return (tiles_per_side * tiles_per_side) / kPointShadowFaceCount;
}

int first_tier_for_budget(const int shadow_budget, const int atlas_size) {
    for (int tier = 0; tier < kPointShadowTileTierCount; ++tier) {
        if (shadow_budget <= light_capacity_for_tile(kPointShadowTileTiers[static_cast<std::size_t>(tier)], atlas_size)) {
            return tier;
        }
    }
    return kPointShadowTileTierCount - 1;
}

OccupancyAtlas make_occupancy_atlas(const int atlas_size) {
    OccupancyAtlas atlas;
    atlas.atlas_size = atlas_size;
    atlas.cell_size = kPointShadowMinTileSize;
    atlas.cells_per_side = atlas.cell_size > 0 ? atlas_size / atlas.cell_size : 0;
    atlas.occupied.assign(static_cast<std::size_t>(atlas.cells_per_side * atlas.cells_per_side), 0);
    return atlas;
}

bool cells_are_free(const OccupancyAtlas& atlas, const int cell_x, const int cell_y, const int cell_span) {
    if (cell_span <= 0 ||
        cell_x < 0 ||
        cell_y < 0 ||
        cell_x + cell_span > atlas.cells_per_side ||
        cell_y + cell_span > atlas.cells_per_side) {
        return false;
    }
    for (int y = 0; y < cell_span; ++y) {
        for (int x = 0; x < cell_span; ++x) {
            const std::size_t index =
                static_cast<std::size_t>((cell_y + y) * atlas.cells_per_side + (cell_x + x));
            if (atlas.occupied[index] != 0) {
                return false;
            }
        }
    }
    return true;
}

void mark_cells(OccupancyAtlas& atlas, const int cell_x, const int cell_y, const int cell_span) {
    for (int y = 0; y < cell_span; ++y) {
        for (int x = 0; x < cell_span; ++x) {
            const std::size_t index =
                static_cast<std::size_t>((cell_y + y) * atlas.cells_per_side + (cell_x + x));
            atlas.occupied[index] = 1;
        }
    }
}

bool allocate_rect(ShadowAtlasRect& out, OccupancyAtlas& atlas, const int tile_size) {
    if (tile_size <= 0 || atlas.cell_size <= 0 || tile_size % atlas.cell_size != 0) {
        return false;
    }
    const int cell_span = tile_size / atlas.cell_size;
    for (int cell_y = 0; cell_y + cell_span <= atlas.cells_per_side; cell_y += cell_span) {
        for (int cell_x = 0; cell_x + cell_span <= atlas.cells_per_side; cell_x += cell_span) {
            if (!cells_are_free(atlas, cell_x, cell_y, cell_span)) {
                continue;
            }
            mark_cells(atlas, cell_x, cell_y, cell_span);
            out = ShadowAtlasRect{cell_x * atlas.cell_size, cell_y * atlas.cell_size, tile_size};
            return true;
        }
    }
    return false;
}

bool allocate_light_faces(
    PointShadowAtlasEntry& entry,
    OccupancyAtlas& atlas,
    const int tile_size
) {
    const std::vector<unsigned char> rollback = atlas.occupied;
    entry.tile_size = tile_size;
    for (ShadowAtlasRect& face : entry.faces) {
        if (!allocate_rect(face, atlas, tile_size)) {
            atlas.occupied = rollback;
            entry.tile_size = 0;
            entry.faces = {};
            return false;
        }
    }
    entry.shadowed = true;
    return true;
}

} // namespace

bool shadow_atlas_rects_overlap(const ShadowAtlasRect a, const ShadowAtlasRect b) {
    return a.x < b.x + b.size &&
        a.x + a.size > b.x &&
        a.y < b.y + b.size &&
        a.y + a.size > b.y;
}

PackedPointShadowAtlas pack_point_shadow_atlas(
    const std::vector<PointLight>& lights,
    const Vec3 camera_position,
    const int max_lights,
    const int max_shadowed_lights,
    const int atlas_size
) {
    const int submitted = std::min<int>(
        std::max(max_lights, 0),
        static_cast<int>(std::min<std::size_t>(lights.size(), kMaxDeferredPointLights))
    );
    const int shadow_budget = std::min(
        std::min(submitted, std::max(max_shadowed_lights, 0)),
        kMaxPointShadowedLights
    );

    PackedPointShadowAtlas packed;
    packed.atlas_size = atlas_size;
    packed.submitted_lights = submitted;
    packed.entries.resize(static_cast<std::size_t>(submitted));

    std::vector<RankedLight> ranked;
    ranked.reserve(static_cast<std::size_t>(submitted));
    for (int i = 0; i < submitted; ++i) {
        packed.entries[static_cast<std::size_t>(i)].light_index = i;
        ranked.push_back(RankedLight{i, shadow_priority(lights[static_cast<std::size_t>(i)], camera_position)});
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedLight a, const RankedLight b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.index < b.index;
    });

    OccupancyAtlas atlas = make_occupancy_atlas(atlas_size);
    const int first_tier = first_tier_for_budget(shadow_budget, atlas_size);
    for (const RankedLight light : ranked) {
        if (packed.shadowed_lights >= shadow_budget) {
            break;
        }
        PointShadowAtlasEntry& entry = packed.entries[static_cast<std::size_t>(light.index)];
        for (int tier = first_tier; tier < kPointShadowTileTierCount; ++tier) {
            const int tile_size = kPointShadowTileTiers[static_cast<std::size_t>(tier)];
            if (tile_size <= 0 || tile_size > atlas_size) {
                continue;
            }
            if (allocate_light_faces(entry, atlas, tile_size)) {
                ++packed.shadowed_lights;
                break;
            }
        }
    }

    return packed;
}

} // namespace undecedent
