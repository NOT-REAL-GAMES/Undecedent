#include "undecedent/editor_ui.hpp"

#include "undecedent/debug_draw.hpp"
#include "undecedent/materials.hpp"
#include "undecedent/screen_draw.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <string>

namespace undecedent {
namespace {

bool entity_dropdown_rects(
    const int width,
    const int height,
    float& x,
    float& y,
    float& w,
    float& row_h
) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    w = 176.0F;
    row_h = 30.0F;
    x = static_cast<float>(width) - w - 14.0F;
    y = 14.0F;
    return true;
}

bool point_in_rect(const float px, const float py, const float x, const float y, const float w, const float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

} // namespace

void draw_material_selector(const int active_material, const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const float swatch = 24.0F;
    const float gap = 7.0F;
    const float x = 16.0F;
    const float y = static_cast<float>(height) - 46.0F;
    const float box_width = (swatch * static_cast<float>(kMaterialCount)) +
        (gap * static_cast<float>(kMaterialCount - 1)) + 18.0F;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.40F);
    draw_screen_quad(10.0F, y - 8.0F, box_width, swatch + 22.0F, width, height);
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialColor color = material_color(i);
        glColor4f(color.r, color.g, color.b, 0.94F);
        draw_screen_quad(x + (static_cast<float>(i) * (swatch + gap)), y, swatch, swatch, width, height);
    }
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    for (int i = 0; i < kMaterialCount; ++i) {
        const float sx = x + (static_cast<float>(i) * (swatch + gap));
        if (i == active_material) {
            glColor4f(0.95F, 0.98F, 0.72F, 0.98F);
        } else {
            glColor4f(0.04F, 0.05F, 0.06F, 0.82F);
        }
        draw_screen_line(sx, y, sx + swatch, y, width, height);
        draw_screen_line(sx + swatch, y, sx + swatch, y + swatch, width, height);
        draw_screen_line(sx + swatch, y + swatch, sx, y + swatch, width, height);
        draw_screen_line(sx, y + swatch, sx, y, width, height);
        draw_stroke_text(std::to_string(i + 1), sx + 8.0F, y + swatch + 5.0F, 5.0F, width, height);
    }
    glEnd();

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

bool handle_entity_dropdown_click(
    EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float row_h = 0.0F;
    if (!entity_dropdown_rects(width, height, x, y, w, row_h)) {
        return false;
    }

    if (point_in_rect(mouse_x, mouse_y, x, y, w, row_h)) {
        editor_world.entity_dropdown_open = !editor_world.entity_dropdown_open;
        return true;
    }

    if (editor_world.entity_dropdown_open) {
        if (point_in_rect(mouse_x, mouse_y, x, y + row_h, w, row_h)) {
            editor_world.entity_placement = EntityPlacementType::PlayerSpawn;
            editor_world.entity_dropdown_open = false;
            return true;
        }
        if (point_in_rect(mouse_x, mouse_y, x, y + (row_h * 2.0F), w, row_h)) {
            editor_world.entity_placement = EntityPlacementType::PointLight;
            editor_world.entity_dropdown_open = false;
            return true;
        }
        editor_world.entity_dropdown_open = false;
    }
    return false;
}

void draw_entity_dropdown(const EditorWorld& editor_world, const int width, const int height) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float row_h = 0.0F;
    if (!entity_dropdown_rects(width, height, x, y, w, row_h)) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.46F);
    draw_screen_quad(x, y, w, row_h, width, height);
    if (editor_world.entity_dropdown_open) {
        draw_screen_quad(x, y + row_h, w, row_h * 2.0F, width, height);
    }
    glEnd();

    glLineWidth(1.5F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + row_h, width, height);
    draw_screen_line(x + w, y + row_h, x, y + row_h, width, height);
    draw_screen_line(x, y + row_h, x, y, width, height);
    draw_stroke_text(entity_placement_label(editor_world.entity_placement), x + 10.0F, y + 8.0F, 5.5F, width, height);
    draw_screen_line(x + w - 22.0F, y + 11.0F, x + w - 14.0F, y + 19.0F, width, height);
    draw_screen_line(x + w - 14.0F, y + 19.0F, x + w - 6.0F, y + 11.0F, width, height);

    if (editor_world.entity_dropdown_open) {
        draw_screen_line(x, y + (row_h * 2.0F), x + w, y + (row_h * 2.0F), width, height);
        draw_stroke_text("PLAYER SPAWN", x + 10.0F, y + row_h + 8.0F, 5.5F, width, height);
        draw_stroke_text("POINT LIGHT", x + 10.0F, y + (row_h * 2.0F) + 8.0F, 5.5F, width, height);
    }
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

} // namespace undecedent
