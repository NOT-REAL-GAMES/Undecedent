#include "undecedent/shadow_atlas.hpp"
#include "undecedent/shadow_cache.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

undecedent::PointLight light_at(const float x, const float intensity = 1.0F, const float radius = 256.0F) {
    undecedent::PointLight light;
    light.id = static_cast<std::uint64_t>(x + 1000.0F);
    light.position = undecedent::Vec3{x, 0.0F, 0.0F};
    light.intensity = intensity;
    light.radius = radius;
    return light;
}

bool entry_has_six_valid_faces(const undecedent::PointShadowAtlasEntry& entry) {
    if (!entry.shadowed || entry.tile_size <= 0) {
        return false;
    }
    for (const undecedent::ShadowAtlasRect rect : entry.faces) {
        if (rect.size != entry.tile_size || rect.x < 0 || rect.y < 0) {
            return false;
        }
    }
    return true;
}

std::array<std::array<float, 16>, undecedent::kSunShadowCascadeCount> identity_cascade_matrices() {
    std::array<std::array<float, 16>, undecedent::kSunShadowCascadeCount> matrices{};
    for (std::array<float, 16>& matrix : matrices) {
        matrix[0] = 1.0F;
        matrix[5] = 1.0F;
        matrix[10] = 1.0F;
        matrix[15] = 1.0F;
    }
    return matrices;
}

} // namespace

int main() {
    {
        const std::vector<undecedent::PointLight> lights{light_at(8.0F)};
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F});
        expect(packed.submitted_lights == 1, "one light should be submitted");
        expect(packed.shadowed_lights == 1, "one light should be shadowed");
        expect(entry_has_six_valid_faces(packed.entries.front()), "shadowed light should allocate six faces");
    }

    {
        std::vector<undecedent::PointLight> lights;
        lights.push_back(light_at(512.0F, 1.0F, 128.0F));
        lights.push_back(light_at(8.0F, 8.0F, 512.0F));
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F}, 2, 2, 2048);
        expect(packed.entries[1].shadowed, "higher priority light should be shadowed");
        expect(packed.entries[1].tile_size == 512, "highest priority light should receive largest tile tier");
    }

    {
        std::vector<undecedent::PointLight> lights;
        for (int i = 0; i < 12; ++i) {
            lights.push_back(light_at(static_cast<float>(i + 1), 1.0F, 256.0F));
        }
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F}, 12, 12, 512);
        expect(packed.shadowed_lights < packed.submitted_lights, "small atlas should drop lower-priority lights");
        for (const undecedent::PointShadowAtlasEntry& entry : packed.entries) {
            if (entry.shadowed) {
                expect(entry_has_six_valid_faces(entry), "shadowed lights should never be partial");
            } else {
                expect(entry.tile_size == 0, "unshadowed lights should have no tile size");
            }
        }
    }

    {
        std::vector<undecedent::PointLight> lights;
        for (int i = 0; i < 8; ++i) {
            lights.push_back(light_at(static_cast<float>(i + 1), 1.0F, 256.0F));
        }
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F}, 8, 8, 1024);
        std::vector<undecedent::ShadowAtlasRect> rects;
        for (const undecedent::PointShadowAtlasEntry& entry : packed.entries) {
            if (!entry.shadowed) {
                continue;
            }
            for (const undecedent::ShadowAtlasRect rect : entry.faces) {
                expect(rect.x + rect.size <= packed.atlas_size, "tile should stay inside atlas x bounds");
                expect(rect.y + rect.size <= packed.atlas_size, "tile should stay inside atlas y bounds");
                for (const undecedent::ShadowAtlasRect previous : rects) {
                    expect(!undecedent::shadow_atlas_rects_overlap(rect, previous), "atlas tiles should not overlap");
                }
                rects.push_back(rect);
            }
        }
    }

    {
        std::vector<undecedent::PointLight> lights;
        for (int i = 0; i < 600; ++i) {
            lights.push_back(light_at(static_cast<float>(i + 1), 1.0F, 256.0F));
        }
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(
                lights,
                undecedent::Vec3{0.0F, 0.0F, 0.0F},
                undecedent::kMaxDeferredPointLights,
                undecedent::kMaxPointShadowedLights,
                undecedent::kPointShadowAtlasSize
            );
        expect(packed.submitted_lights == 600, "all 600 lights should remain submitted to deferred lighting");
        expect(packed.shadowed_lights == undecedent::kMaxPointShadowedLights, "shadowed lights should stop at 512");
        for (const undecedent::PointShadowAtlasEntry& entry : packed.entries) {
            if (entry.shadowed) {
                expect(entry.tile_size == undecedent::kPointShadowMinTileSize, "crowded atlas should use 128px tiles");
            }
        }
    }

    {
        std::vector<undecedent::PointLight> lights{light_at(16.0F, 1.0F, 256.0F)};
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F});
        const undecedent::PointShadowCacheEntry cache =
            undecedent::make_point_shadow_cache_entry(lights.front(), 0, packed.entries.front(), 7);
        expect(
            undecedent::point_shadow_cache_matches(cache, lights.front(), 0, packed.entries.front(), 7),
            "unchanged point light should reuse shadow cache"
        );

        undecedent::PointLight color_changed = lights.front();
        color_changed.color.x = 0.1F;
        color_changed.intensity = 9.0F;
        color_changed.shadow_bias = 12.0F;
        expect(
            undecedent::point_shadow_cache_matches(cache, color_changed, 0, packed.entries.front(), 7),
            "color intensity and receiver bias should not redraw point shadow cache"
        );

        undecedent::PointLight moved = lights.front();
        moved.position.x += 2.0F;
        expect(
            !undecedent::point_shadow_cache_matches(cache, moved, 0, packed.entries.front(), 7),
            "moving a point light should invalidate shadow cache"
        );

        undecedent::PointLight resized = lights.front();
        resized.radius += 16.0F;
        expect(
            !undecedent::point_shadow_cache_matches(cache, resized, 0, packed.entries.front(), 7),
            "changing point light radius should invalidate shadow cache"
        );

        expect(
            !undecedent::point_shadow_cache_matches(cache, lights.front(), 0, packed.entries.front(), 8),
            "geometry shadow revision should invalidate point shadow cache"
        );
    }

    {
        std::vector<undecedent::PointLight> lights{light_at(16.0F, 1.0F, 256.0F)};
        const undecedent::PackedPointShadowAtlas packed =
            undecedent::pack_point_shadow_atlas(lights, undecedent::Vec3{0.0F, 0.0F, 0.0F});
        undecedent::PointShadowAtlasEntry moved_entry = packed.entries.front();
        moved_entry.faces[0].x += moved_entry.faces[0].size;
        const undecedent::PointShadowCacheEntry cache =
            undecedent::make_point_shadow_cache_entry(lights.front(), 0, packed.entries.front(), 3);
        expect(
            !undecedent::point_shadow_cache_matches(cache, lights.front(), 0, moved_entry, 3),
            "atlas tile movement should invalidate point shadow cache"
        );
    }

    {
        const std::array<std::array<float, 16>, undecedent::kSunShadowCascadeCount> matrices =
            identity_cascade_matrices();
        const undecedent::SunShadowCacheEntry cache =
            undecedent::make_sun_shadow_cache_entry(
                11,
                undecedent::Vec3{0.0F, -1.0F, 0.0F},
                1280,
                720,
                70.0F,
                1.0F,
                20000.0F,
                0.0F,
                0.0F,
                matrices
            );
        expect(
            undecedent::sun_shadow_cache_matches(
                cache,
                11,
                undecedent::Vec3{0.0F, -1.0F, 0.0F},
                1280,
                720,
                70.0F,
                1.0F,
                20000.0F,
                0.002F,
                0.0F,
                matrices
            ),
            "small camera angle movement should reuse snapped CSM cache"
        );
        expect(
            !undecedent::sun_shadow_cache_matches(
                cache,
                11,
                undecedent::Vec3{0.0F, -1.0F, 0.0F},
                1280,
                720,
                70.0F,
                1.0F,
                20000.0F,
                0.02F,
                0.0F,
                matrices
            ),
            "large camera angle movement should invalidate CSM cache"
        );
        expect(
            !undecedent::sun_shadow_cache_matches(
                cache,
                12,
                undecedent::Vec3{0.0F, -1.0F, 0.0F},
                1280,
                720,
                70.0F,
                1.0F,
                20000.0F,
                0.0F,
                0.0F,
                matrices
            ),
            "geometry shadow revision should invalidate CSM cache"
        );
        expect(
            !undecedent::sun_shadow_cache_matches(
                cache,
                11,
                undecedent::Vec3{0.1F, -0.99F, 0.0F},
                1280,
                720,
                70.0F,
                1.0F,
                20000.0F,
                0.0F,
                0.0F,
                matrices
            ),
            "sun direction change should invalidate CSM cache"
        );
    }

    return EXIT_SUCCESS;
}
