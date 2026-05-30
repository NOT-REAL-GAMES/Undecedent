#include "undecedent/editor_ui.hpp"

#include "undecedent/debug_draw.hpp"
#include "undecedent/materials.hpp"
#include "undecedent/screen_draw.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <iostream>
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

bool sculpt_button_rect(const int width, const int height, float& x, float& y, float& w, float& h) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    x = 14.0F;
    y = 52.0F;
    w = 112.0F;
    h = 30.0F;
    return true;
}

bool subdivision_controls_rect(const int width, const int height, float& x, float& y, float& w, float& h) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    x = 14.0F;
    y = 14.0F;
    w = 176.0F;
    h = 30.0F;
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

bool handle_sculpt_button_click(
    EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (selected_sector_subdivision(editor_world) <= 0 ||
        !sculpt_button_rect(width, height, x, y, w, h) ||
        !point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        return false;
    }
    editor_world.displacement_sculpt_enabled = !editor_world.displacement_sculpt_enabled;
    std::cout << "Displacement sculpt "
              << (editor_world.displacement_sculpt_enabled ? "enabled" : "disabled") << '\n';
    return true;
}

bool handle_subdivision_controls_click(
    EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (!subdivision_controls_rect(width, height, x, y, w, h) ||
        !point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        return false;
    }

    if (mouse_x <= x + 34.0F) {
        adjust_selected_sector_subdivision(editor_world, -1);
    } else if (mouse_x >= x + w - 34.0F) {
        adjust_selected_sector_subdivision(editor_world, 1);
    }
    return true;
}

void draw_sculpt_button(
    const EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (selected_sector_subdivision(editor_world) <= 0 ||
        !sculpt_button_rect(width, height, x, y, w, h)) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    if (editor_world.displacement_sculpt_enabled) {
        glColor4f(0.18F, 0.42F, 0.38F, 0.82F);
    } else {
        glColor4f(0.0F, 0.0F, 0.0F, 0.46F);
    }
    draw_screen_quad(x, y, w, h, width, height);
    glEnd();

    glLineWidth(1.5F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
    draw_screen_line(x + 12.0F, y + 20.0F, x + 22.0F, y + 10.0F, width, height);
    draw_screen_line(x + 22.0F, y + 10.0F, x + 32.0F, y + 20.0F, width, height);
    draw_screen_line(x + 32.0F, y + 20.0F, x + 42.0F, y + 10.0F, width, height);
    draw_stroke_text("SCULPT", x + 52.0F, y + 9.0F, 5.5F, width, height);
    glEnd();
    glLineWidth(1.0F);

    if (point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        const std::string radius = "R " + format_world_units(editor_world.displacement_brush_radius) + "U";
        const float tooltip_x = x;
        const float tooltip_y = y + h + 8.0F;
        const float tooltip_w = 178.0F;
        const float tooltip_h = 44.0F;
        glBegin(GL_QUADS);
        glColor4f(0.0F, 0.0F, 0.0F, 0.72F);
        draw_screen_quad(tooltip_x, tooltip_y, tooltip_w, tooltip_h, width, height);
        glEnd();

        glLineWidth(1.25F);
        glBegin(GL_LINES);
        glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
        draw_screen_line(tooltip_x, tooltip_y, tooltip_x + tooltip_w, tooltip_y, width, height);
        draw_screen_line(tooltip_x + tooltip_w, tooltip_y, tooltip_x + tooltip_w, tooltip_y + tooltip_h, width, height);
        draw_screen_line(tooltip_x + tooltip_w, tooltip_y + tooltip_h, tooltip_x, tooltip_y + tooltip_h, width, height);
        draw_screen_line(tooltip_x, tooltip_y + tooltip_h, tooltip_x, tooltip_y, width, height);
        draw_stroke_text("SCULPT DISPLACEMENT", tooltip_x + 8.0F, tooltip_y + 9.0F, 5.0F, width, height);
        draw_stroke_text(radius, tooltip_x + 8.0F, tooltip_y + 26.0F, 5.0F, width, height);
        glEnd();
        glLineWidth(1.0F);
    }
    glDisable(GL_BLEND);
}

void draw_subdivision_controls(const EditorWorld& editor_world, const int width, const int height) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (!subdivision_controls_rect(width, height, x, y, w, h)) {
        return;
    }

    const int subdivision = selected_sector_subdivision(editor_world);
    const bool has_selection =
        editor_world.selected_sector >= 0 &&
        editor_world.selected_sector < static_cast<int>(editor_world.sectors.size());

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, has_selection ? 0.50F : 0.28F);
    draw_screen_quad(x, y, w, h, width, height);
    glEnd();

    glLineWidth(1.5F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, has_selection ? 0.92F : 0.38F);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
    draw_screen_line(x + 34.0F, y, x + 34.0F, y + h, width, height);
    draw_screen_line(x + w - 34.0F, y, x + w - 34.0F, y + h, width, height);

    draw_screen_line(x + 11.0F, y + 15.0F, x + 23.0F, y + 15.0F, width, height);
    draw_screen_line(x + w - 23.0F, y + 15.0F, x + w - 11.0F, y + 15.0F, width, height);
    draw_screen_line(x + w - 17.0F, y + 9.0F, x + w - 17.0F, y + 21.0F, width, height);
    glEnd();

    const std::string label = has_selection
        ? "SUBDIV " + std::to_string(subdivision)
        : "SUBDIV -";
    draw_stroke_text(label, x + 46.0F, y + 9.0F, 5.5F, width, height);
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
