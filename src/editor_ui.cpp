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

bool entity_inspector_rect(const int width, const int height, float& x, float& y, float& w, float& h) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    w = 246.0F;
    h = std::min(340.0F, std::max(170.0F, static_cast<float>(height) - 106.0F));
    x = static_cast<float>(width) - w - 14.0F;
    y = 52.0F;
    return true;
}

bool point_in_rect(const float px, const float py, const float x, const float y, const float w, const float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

std::string entity_selection_label(const EditorWorld& editor_world) {
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn:
        return "PLAYER SPAWN";
    case SelectedEntityKind::PointLight:
        return "POINT LIGHT";
    case SelectedEntityKind::SunLight:
        return "SUN LIGHT";
    case SelectedEntityKind::None:
        return "NO ENTITY";
    }
    return "NO ENTITY";
}

float property_step(const EntityProperty property, const bool fine) {
    switch (property) {
    case EntityProperty::Yaw: return fine ? 0.025F : 0.125F;
    case EntityProperty::ColorR:
    case EntityProperty::ColorG:
    case EntityProperty::ColorB:
    case EntityProperty::SunDirectionX:
    case EntityProperty::SunDirectionY:
    case EntityProperty::SunDirectionZ:
        return fine ? 0.01F : 0.05F;
    case EntityProperty::Radius:
        return fine ? 1.0F : 16.0F;
    case EntityProperty::Intensity:
        return fine ? 0.05F : 0.25F;
    case EntityProperty::PositionX:
    case EntityProperty::PositionY:
    case EntityProperty::PositionZ:
        return fine ? 1.0F : 8.0F;
    case EntityProperty::SunEnabled:
        return 0.0F;
    }
    return fine ? 1.0F : 8.0F;
}

std::string format_property_value(const float value) {
    return format_world_units(value);
}

void draw_button_rect(
    const float x,
    const float y,
    const float w,
    const float h,
    const int width,
    const int height,
    const bool active = false
) {
    glBegin(GL_QUADS);
    glColor4f(active ? 0.18F : 0.0F, active ? 0.40F : 0.0F, active ? 0.34F : 0.0F, active ? 0.80F : 0.42F);
    draw_screen_quad(x, y, w, h, width, height);
    glEnd();
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, active ? 0.96F : 0.62F);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
    glEnd();
}

void draw_ui_text(
    const std::string& label,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height,
    const float alpha = 0.92F
) {
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, alpha);
    draw_stroke_text(label, x, y, size, width, height);
    glEnd();
}

bool inspector_step_click(
    EditorWorld& editor_world,
    const EntityProperty property,
    const float row_x,
    const float row_y,
    const float row_w,
    const float row_h,
    const float mouse_x,
    const float mouse_y,
    const bool fine
) {
    const float button_w = 24.0F;
    if (point_in_rect(mouse_x, mouse_y, row_x + row_w - (button_w * 2.0F) - 10.0F, row_y, button_w, row_h)) {
        return adjust_selected_entity_property(editor_world, property, -property_step(property, fine));
    }
    if (point_in_rect(mouse_x, mouse_y, row_x + row_w - button_w - 6.0F, row_y, button_w, row_h)) {
        return adjust_selected_entity_property(editor_world, property, property_step(property, fine));
    }
    return false;
}

void draw_property_row(
    const std::string& label,
    const std::string& value,
    const float x,
    const float y,
    const float w,
    const float row_h,
    const int width,
    const int height
) {
    draw_ui_text(label, x + 8.0F, y + 7.0F, 4.8F, width, height);
    draw_ui_text(value, x + 92.0F, y + 7.0F, 4.8F, width, height);
    draw_button_rect(x + w - 58.0F, y + 2.0F, 22.0F, row_h - 4.0F, width, height);
    draw_button_rect(x + w - 30.0F, y + 2.0F, 22.0F, row_h - 4.0F, width, height);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
    draw_screen_line(x + w - 52.0F, y + (row_h * 0.5F), x + w - 42.0F, y + (row_h * 0.5F), width, height);
    draw_screen_line(x + w - 24.0F, y + (row_h * 0.5F), x + w - 14.0F, y + (row_h * 0.5F), width, height);
    draw_screen_line(x + w - 19.0F, y + (row_h * 0.5F) - 5.0F, x + w - 19.0F, y + (row_h * 0.5F) + 5.0F, width, height);
    glEnd();
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

bool handle_entity_inspector_click(
    EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y,
    const bool fine
) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (!entity_inspector_rect(width, height, x, y, w, h) || !point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        return false;
    }

    const float selector_y = y + 32.0F;
    const float selector_h = 24.0F;
    const float chip_w = (w - 28.0F) / 3.0F;
    if (point_in_rect(mouse_x, mouse_y, x + 8.0F, selector_y, chip_w, selector_h)) {
        if (editor_world.player_spawn.set) {
            select_entity(editor_world, SelectedEntityRef{SelectedEntityKind::PlayerSpawn, 0});
        }
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, x + 14.0F + chip_w, selector_y, chip_w, selector_h)) {
        ensure_editor_stable_ids(editor_world);
        if (!editor_world.point_lights.empty()) {
            select_entity(
                editor_world,
                SelectedEntityRef{SelectedEntityKind::PointLight, editor_world.point_lights.front().id}
            );
        }
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, x + 20.0F + (chip_w * 2.0F), selector_y, chip_w, selector_h)) {
        select_entity(editor_world, SelectedEntityRef{SelectedEntityKind::SunLight, 0});
        return true;
    }

    const float row_h = 23.0F;
    float row_y = y + 68.0F;
    const auto row_hit = [&](const int row_index) {
        return point_in_rect(mouse_x, mouse_y, x + 8.0F, row_y + (static_cast<float>(row_index) * row_h), w - 16.0F, row_h);
    };
    const auto step_row = [&](const int row_index, const EntityProperty property) {
        return inspector_step_click(
            editor_world,
            property,
            x + 8.0F,
            row_y + (static_cast<float>(row_index) * row_h),
            w - 16.0F,
            row_h,
            mouse_x,
            mouse_y,
            fine
        );
    };

    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn:
        if (step_row(0, EntityProperty::PositionX)) return true;
        if (step_row(1, EntityProperty::PositionY)) return true;
        if (step_row(2, EntityProperty::PositionZ)) return true;
        if (step_row(3, EntityProperty::Yaw)) return true;
        if (row_hit(5)) {
            delete_selected_entity(editor_world);
            return true;
        }
        break;
    case SelectedEntityKind::PointLight:
        if (step_row(0, EntityProperty::PositionX)) return true;
        if (step_row(1, EntityProperty::PositionY)) return true;
        if (step_row(2, EntityProperty::PositionZ)) return true;
        if (step_row(3, EntityProperty::ColorR)) return true;
        if (step_row(4, EntityProperty::ColorG)) return true;
        if (step_row(5, EntityProperty::ColorB)) return true;
        if (step_row(6, EntityProperty::Radius)) return true;
        if (step_row(7, EntityProperty::Intensity)) return true;
        if (row_hit(9)) {
            delete_selected_entity(editor_world);
            return true;
        }
        break;
    case SelectedEntityKind::SunLight:
        if (row_hit(0)) {
            adjust_selected_entity_property(editor_world, EntityProperty::SunEnabled, 0.0F);
            return true;
        }
        if (step_row(1, EntityProperty::SunDirectionX)) return true;
        if (step_row(2, EntityProperty::SunDirectionY)) return true;
        if (step_row(3, EntityProperty::SunDirectionZ)) return true;
        if (step_row(4, EntityProperty::ColorR)) return true;
        if (step_row(5, EntityProperty::ColorG)) return true;
        if (step_row(6, EntityProperty::ColorB)) return true;
        if (step_row(7, EntityProperty::Intensity)) return true;
        break;
    case SelectedEntityKind::None:
        break;
    }

    return true;
}

void draw_entity_inspector(const EditorWorld& editor_world, const int width, const int height) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (!entity_inspector_rect(width, height, x, y, w, h)) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.44F);
    draw_screen_quad(x, y, w, h, width, height);
    glEnd();

    glLineWidth(1.5F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.88F);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
    glEnd();

    draw_ui_text("INSPECTOR", x + 8.0F, y + 10.0F, 5.4F, width, height);
    const float selector_y = y + 32.0F;
    const float selector_h = 24.0F;
    const float chip_w = (w - 28.0F) / 3.0F;
    draw_button_rect(
        x + 8.0F,
        selector_y,
        chip_w,
        selector_h,
        width,
        height,
        editor_world.selected_entity.kind == SelectedEntityKind::PlayerSpawn
    );
    draw_button_rect(
        x + 14.0F + chip_w,
        selector_y,
        chip_w,
        selector_h,
        width,
        height,
        editor_world.selected_entity.kind == SelectedEntityKind::PointLight
    );
    draw_button_rect(
        x + 20.0F + (chip_w * 2.0F),
        selector_y,
        chip_w,
        selector_h,
        width,
        height,
        editor_world.selected_entity.kind == SelectedEntityKind::SunLight
    );
    draw_ui_text("SPAWN", x + 18.0F, selector_y + 8.0F, 4.5F, width, height);
    draw_ui_text("LIGHT", x + 24.0F + chip_w, selector_y + 8.0F, 4.5F, width, height);
    draw_ui_text("SUN", x + 34.0F + (chip_w * 2.0F), selector_y + 8.0F, 4.5F, width, height);

    draw_ui_text(entity_selection_label(editor_world), x + 8.0F, y + 61.0F, 4.7F, width, height);
    const float row_h = 23.0F;
    float row_y = y + 68.0F;
    int row = 0;
    const auto draw_row = [&](const std::string& label, const std::string& value) {
        draw_property_row(label, value, x + 8.0F, row_y + (static_cast<float>(row) * row_h), w - 16.0F, row_h, width, height);
        ++row;
    };
    const auto gap = [&]() {
        ++row;
    };

    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn:
        if (editor_world.player_spawn.set) {
            draw_row("POS X", format_property_value(editor_world.player_spawn.position.x));
            draw_row("POS Y", format_property_value(editor_world.player_spawn.position.y));
            draw_row("POS Z", format_property_value(editor_world.player_spawn.position.z));
            draw_row("YAW", format_property_value(editor_world.player_spawn.yaw));
            gap();
            draw_button_rect(x + 8.0F, row_y + (static_cast<float>(row) * row_h), w - 16.0F, row_h - 2.0F, width, height);
            draw_ui_text("UNSET PLAYER SPAWN", x + 20.0F, row_y + (static_cast<float>(row) * row_h) + 7.0F, 4.8F, width, height);
        }
        break;
    case SelectedEntityKind::PointLight:
        if (const PointLight* light = selected_point_light(editor_world)) {
            draw_row("POS X", format_property_value(light->position.x));
            draw_row("POS Y", format_property_value(light->position.y));
            draw_row("POS Z", format_property_value(light->position.z));
            draw_row("RED", format_property_value(light->color.x));
            draw_row("GREEN", format_property_value(light->color.y));
            draw_row("BLUE", format_property_value(light->color.z));
            draw_row("RADIUS", format_property_value(light->radius));
            draw_row("POWER", format_property_value(light->intensity));
            gap();
            draw_button_rect(x + 8.0F, row_y + (static_cast<float>(row) * row_h), w - 16.0F, row_h - 2.0F, width, height);
            draw_ui_text("DELETE POINT LIGHT", x + 22.0F, row_y + (static_cast<float>(row) * row_h) + 7.0F, 4.8F, width, height);
        }
        break;
    case SelectedEntityKind::SunLight:
        draw_button_rect(x + 8.0F, row_y, w - 16.0F, row_h - 2.0F, width, height, editor_world.world_lighting.sun_enabled);
        draw_ui_text(editor_world.world_lighting.sun_enabled ? "ENABLED" : "DISABLED", x + 20.0F, row_y + 7.0F, 4.8F, width, height);
        ++row;
        draw_row("DIR X", format_property_value(editor_world.world_lighting.sun_direction.x));
        draw_row("DIR Y", format_property_value(editor_world.world_lighting.sun_direction.y));
        draw_row("DIR Z", format_property_value(editor_world.world_lighting.sun_direction.z));
        draw_row("RED", format_property_value(editor_world.world_lighting.sun_color.x));
        draw_row("GREEN", format_property_value(editor_world.world_lighting.sun_color.y));
        draw_row("BLUE", format_property_value(editor_world.world_lighting.sun_color.z));
        draw_row("POWER", format_property_value(editor_world.world_lighting.sun_intensity));
        break;
    case SelectedEntityKind::None:
        draw_ui_text("PICK AN ENTITY OR SUN", x + 18.0F, row_y + 10.0F, 4.8F, width, height);
        break;
    }

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_translation_gizmo(
    const EditorWorld& editor_world,
    const int width,
    const int height,
    const GameCamera& camera,
    const GameRenderConfig& config
) {
    Vec3 origin{};
    if (!selected_entity_position(editor_world, origin) || width <= 0 || height <= 0) {
        return;
    }

    set_game_projection(width, height, camera, config);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glLineWidth(3.0F);
    glBegin(GL_LINES);
    glColor4f(0.95F, 0.24F, 0.22F, 0.98F);
    glVertex3f(origin.x, origin.y, origin.z);
    glVertex3f(origin.x + 72.0F, origin.y, origin.z);
    glColor4f(0.26F, 0.95F, 0.38F, 0.98F);
    glVertex3f(origin.x, origin.y, origin.z);
    glVertex3f(origin.x, origin.y + 72.0F, origin.z);
    glColor4f(0.30F, 0.50F, 1.0F, 0.98F);
    glVertex3f(origin.x, origin.y, origin.z);
    glVertex3f(origin.x, origin.y, origin.z + 72.0F);
    glEnd();
    glLineWidth(1.0F);
}

} // namespace undecedent
