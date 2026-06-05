#pragma once

#include "undecedent/editor.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace undecedent {

enum class ScriptEditorMove {
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
};

ScriptTargetRef default_script_editor_target(EditorWorld& editor_world);
bool script_editor_target_available(EditorWorld& editor_world, ScriptTargetRef target);
std::uint64_t selected_entity_script_id(EditorWorld& editor_world);
std::uint64_t selected_sector_script_id(EditorWorld& editor_world);
std::string script_editor_target_label(ScriptTargetRef target);

void open_script_editor(EditorWorld& editor_world, ScriptTargetRef target);
void open_script_editor_default(EditorWorld& editor_world);
void toggle_script_editor(EditorWorld& editor_world);

ScriptEditorDraft* active_script_editor_draft(EditorWorld& editor_world);
const ScriptEditorDraft* active_script_editor_draft(const EditorWorld& editor_world);
ScriptEditorDraft* script_editor_draft_for(EditorWorld& editor_world, ScriptTargetRef target);

bool script_editor_insert_text(EditorWorld& editor_world, std::string_view text);
bool script_editor_newline(EditorWorld& editor_world);
bool script_editor_insert_tab(EditorWorld& editor_world);
bool script_editor_backspace(EditorWorld& editor_world);
bool script_editor_delete(EditorWorld& editor_world);
bool script_editor_move_caret(EditorWorld& editor_world, ScriptEditorMove move, bool selecting);
bool script_editor_select_all(EditorWorld& editor_world);
std::string script_editor_selected_text(const EditorWorld& editor_world);
bool script_editor_replace_selection(EditorWorld& editor_world, std::string_view text);
bool script_editor_cut_selection(EditorWorld& editor_world, std::string& cut_text);
bool script_editor_local_undo(EditorWorld& editor_world);
bool script_editor_local_redo(EditorWorld& editor_world);
void script_editor_set_caret_line_column(EditorWorld& editor_world, int line, int column);
void script_editor_scroll(EditorWorld& editor_world, int line_delta);

bool script_editor_apply_current(EditorWorld& editor_world);
bool script_editor_apply_dirty_before_save(EditorWorld& editor_world);
void script_editor_clear_clean_drafts(EditorWorld& editor_world);
bool script_editor_has_dirty_drafts(const EditorWorld& editor_world);
void script_editor_start_editable_template(EditorWorld& editor_world);

} // namespace undecedent
