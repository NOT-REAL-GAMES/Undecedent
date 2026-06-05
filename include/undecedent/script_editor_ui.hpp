#pragma once

#include "undecedent/editor.hpp"

namespace undecedent {

void draw_script_editor_workspace(EditorWorld& editor_world, int width, int height);
bool handle_script_editor_mouse_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);
bool handle_script_editor_mouse_wheel(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y, float scroll_y);

void draw_script_quick_buttons(EditorWorld& editor_world, int width, int height);
bool handle_script_quick_button_click(EditorWorld& editor_world, int width, int height, float mouse_x, float mouse_y);

} // namespace undecedent
