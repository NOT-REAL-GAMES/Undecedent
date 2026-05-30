#pragma once

#include "undecedent/editor.hpp"

namespace undecedent {

struct Editor2DRenderConfig {
    int major_grid_every = 4;
    float min_zoom = 1.0F / 65536.0F;
    float scale_indicator_target_pixels = 160.0F;
    float scale_indicator_min_pixels = 80.0F;
    float scale_indicator_max_pixels = 190.0F;
    float player_eye_height = 48.0F;
};

void draw_editor_2d_view(
    const EditorWorld& editor_world,
    int width,
    int height,
    const EditorCamera& camera,
    const Editor2DRenderConfig& config
);

} // namespace undecedent
