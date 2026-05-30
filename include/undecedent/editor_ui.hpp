#pragma once

#include "undecedent/editor.hpp"

namespace undecedent {

void draw_material_selector(int active_material, int width, int height);
bool handle_sculpt_button_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
bool handle_subdivision_controls_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
bool handle_entity_dropdown_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
void draw_sculpt_button(const EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
void draw_subdivision_controls(const EditorWorld& editor_world, int width, int height);
void draw_entity_dropdown(const EditorWorld& editor_world, int width, int height);

} // namespace undecedent
