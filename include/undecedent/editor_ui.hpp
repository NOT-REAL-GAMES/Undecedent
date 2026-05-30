#pragma once

#include "undecedent/editor.hpp"

namespace undecedent {

void draw_material_selector(int active_material, int width, int height);
bool handle_entity_dropdown_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
void draw_entity_dropdown(const EditorWorld& editor_world, int width, int height);

} // namespace undecedent
