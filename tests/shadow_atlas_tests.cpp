#include "undecedent/shadow_atlas.hpp"

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

    return EXIT_SUCCESS;
}
