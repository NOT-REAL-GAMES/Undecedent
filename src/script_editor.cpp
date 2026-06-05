#include "undecedent/script_editor.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace undecedent {
namespace {

constexpr std::size_t kScriptEditorLocalHistoryLimit = 96;

bool same_target(const ScriptTargetRef a, const ScriptTargetRef b) {
    return a.kind == b.kind && a.id == b.id;
}

bool source_is_blank(const std::string& source) {
    return std::all_of(source.begin(), source.end(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::string editable_template_for(const ScriptTargetRef target) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        return
            "// Map script\n"
            "function on_map_start() {\n"
            "    print(1);\n"
            "}\n\n"
            "function on_tick() {\n"
            "}\n";
    case ScriptTargetKind::Entity:
        return
            "// Entity script\n"
            "function on_tick() {\n"
            "    self.transform.x = self.transform.x;\n"
            "}\n";
    case ScriptTargetKind::Sector:
        return
            "// Sector script, stored for future sector events\n"
            "function on_sector_enter() {\n"
            "    print(1);\n"
            "}\n";
    }
    return {};
}

std::string disassembly_text_for(const ScriptProgram& program) {
    std::string text = "// Read-only bytecode disassembly. Press REPLACE to start editable source.\n";
    text += disassemble_script(program);
    return text;
}

void clamp_draft_cursor(ScriptEditorDraft& draft) {
    const int size = static_cast<int>(draft.source.size());
    draft.caret = std::clamp(draft.caret, 0, size);
    if (draft.selection_anchor >= 0) {
        draft.selection_anchor = std::clamp(draft.selection_anchor, 0, size);
    }
    draft.scroll_line = std::max(0, draft.scroll_line);
}

void push_local_undo(ScriptEditorDraft& draft) {
    draft.undo_stack.push_back(draft.source);
    if (draft.undo_stack.size() > kScriptEditorLocalHistoryLimit) {
        draft.undo_stack.erase(draft.undo_stack.begin());
    }
    draft.redo_stack.clear();
}

std::pair<int, int> selection_range(const ScriptEditorDraft& draft) {
    if (draft.selection_anchor < 0 || draft.selection_anchor == draft.caret) {
        return {draft.caret, draft.caret};
    }
    return {std::min(draft.selection_anchor, draft.caret), std::max(draft.selection_anchor, draft.caret)};
}

bool erase_selection(ScriptEditorDraft& draft) {
    const auto [start, end] = selection_range(draft);
    if (start == end) {
        return false;
    }
    draft.source.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    draft.caret = start;
    draft.selection_anchor = -1;
    return true;
}

bool mutate_source(EditorWorld& editor_world, const char* status, const auto& mutate) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr || draft->read_only) {
        return false;
    }
    push_local_undo(*draft);
    const bool changed = mutate(*draft);
    if (!changed) {
        draft->undo_stack.pop_back();
        return false;
    }
    draft->dirty = true;
    draft->compile_error = false;
    draft->status = status;
    clamp_draft_cursor(*draft);
    return true;
}

int line_for_offset(const std::string& source, const int offset) {
    int line = 0;
    const int clamped = std::clamp(offset, 0, static_cast<int>(source.size()));
    for (int i = 0; i < clamped; ++i) {
        if (source[static_cast<std::size_t>(i)] == '\n') {
            ++line;
        }
    }
    return line;
}

int line_start_offset(const std::string& source, const int target_line) {
    if (target_line <= 0) {
        return 0;
    }
    int line = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++line;
            if (line == target_line) {
                return static_cast<int>(i + 1U);
            }
        }
    }
    return static_cast<int>(source.size());
}

int line_end_offset(const std::string& source, const int start) {
    std::size_t i = static_cast<std::size_t>(std::clamp(start, 0, static_cast<int>(source.size())));
    while (i < source.size() && source[i] != '\n') {
        ++i;
    }
    return static_cast<int>(i);
}

int column_for_offset(const std::string& source, const int offset) {
    const int start = line_start_offset(source, line_for_offset(source, offset));
    return std::max(0, offset - start);
}

int line_count(const std::string& source) {
    return 1 + static_cast<int>(std::count(source.begin(), source.end(), '\n'));
}

int offset_for_line_column(const std::string& source, const int line, const int column) {
    const int start = line_start_offset(source, std::max(0, line));
    const int end = line_end_offset(source, start);
    return std::clamp(start + std::max(0, column), start, end);
}

void move_to_offset(ScriptEditorDraft& draft, const int offset, const bool selecting) {
    if (selecting) {
        if (draft.selection_anchor < 0) {
            draft.selection_anchor = draft.caret;
        }
    } else {
        draft.selection_anchor = -1;
    }
    draft.caret = std::clamp(offset, 0, static_cast<int>(draft.source.size()));
    const int line = line_for_offset(draft.source, draft.caret);
    if (line < draft.scroll_line) {
        draft.scroll_line = line;
    } else if (line > draft.scroll_line + 26) {
        draft.scroll_line = std::max(0, line - 26);
    }
}

void apply_program_for_target(EditorWorld& editor_world, const ScriptTargetRef target, ScriptProgram program) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        set_global_script(editor_world.scripts, std::move(program));
        break;
    case ScriptTargetKind::Entity:
        if (target.id != 0) {
            attach_entity_script(editor_world.scripts, editor_world.entities, target.id, std::move(program));
        }
        break;
    case ScriptTargetKind::Sector:
        if (target.id != 0) {
            set_sector_script(editor_world.scripts, target.id, std::move(program));
        }
        break;
    }
}

} // namespace

std::uint64_t selected_entity_script_id(EditorWorld& editor_world) {
    ensure_editor_stable_ids(editor_world);
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn: {
        const EntityHandle entity = player_spawn_entity(editor_world.entities);
        return entity_alive(editor_world.entities, entity) ? entity_stable_id(editor_world.entities, entity) : 0;
    }
    case SelectedEntityKind::PointLight:
        return point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id).index !=
                kInvalidEntityIndex
            ? editor_world.selected_entity.point_light_id
            : 0;
    case SelectedEntityKind::SunLight:
    case SelectedEntityKind::None:
        break;
    }
    return 0;
}

std::uint64_t selected_sector_script_id(EditorWorld& editor_world) {
    ensure_editor_stable_ids(editor_world);
    if (editor_world.selected_sector < 0 ||
        editor_world.selected_sector >= static_cast<int>(editor_world.sectors.size())) {
        return 0;
    }
    return editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)].id;
}

ScriptTargetRef default_script_editor_target(EditorWorld& editor_world) {
    if (const std::uint64_t entity_id = selected_entity_script_id(editor_world); entity_id != 0) {
        return ScriptTargetRef{ScriptTargetKind::Entity, entity_id};
    }
    if (const std::uint64_t sector_id = selected_sector_script_id(editor_world); sector_id != 0) {
        return ScriptTargetRef{ScriptTargetKind::Sector, sector_id};
    }
    return ScriptTargetRef{ScriptTargetKind::Map, 0};
}

bool script_editor_target_available(EditorWorld& editor_world, const ScriptTargetRef target) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        return true;
    case ScriptTargetKind::Entity:
        if (target.id == 0) {
            return false;
        }
        ensure_editor_stable_ids(editor_world);
        return entity_alive(editor_world.entities, find_entity_by_stable_id(editor_world.entities, target.id));
    case ScriptTargetKind::Sector:
        if (target.id == 0) {
            return false;
        }
        ensure_editor_stable_ids(editor_world);
        return std::any_of(editor_world.sectors.begin(), editor_world.sectors.end(), [target](const SectorPlane& sector) {
            return sector.id == target.id;
        });
    }
    return false;
}

std::string script_editor_target_label(const ScriptTargetRef target) {
    switch (target.kind) {
    case ScriptTargetKind::Map:
        return "MAP";
    case ScriptTargetKind::Entity:
        return "ENTITY " + std::to_string(target.id);
    case ScriptTargetKind::Sector:
        return "SECTOR " + std::to_string(target.id);
    }
    return "SCRIPT";
}

ScriptEditorDraft* script_editor_draft_for(EditorWorld& editor_world, const ScriptTargetRef target) {
    for (ScriptEditorDraft& draft : editor_world.script_editor.drafts) {
        if (same_target(draft.target, target)) {
            return &draft;
        }
    }

    ScriptEditorDraft draft;
    draft.target = target;
    if (const ScriptProgram* program = script_for_target(editor_world.scripts, target)) {
        draft.source = disassembly_text_for(*program);
        draft.read_only = true;
        draft.status = "READ-ONLY BYTECODE. PRESS REPLACE TO EDIT SOURCE.";
    } else {
        draft.source = editable_template_for(target);
        draft.status = "NEW SOURCE DRAFT.";
    }
    editor_world.script_editor.drafts.push_back(std::move(draft));
    return &editor_world.script_editor.drafts.back();
}

ScriptEditorDraft* active_script_editor_draft(EditorWorld& editor_world) {
    if (!editor_world.script_editor.open) {
        return nullptr;
    }
    return script_editor_draft_for(editor_world, editor_world.script_editor.current_target);
}

const ScriptEditorDraft* active_script_editor_draft(const EditorWorld& editor_world) {
    if (!editor_world.script_editor.open) {
        return nullptr;
    }
    for (const ScriptEditorDraft& draft : editor_world.script_editor.drafts) {
        if (same_target(draft.target, editor_world.script_editor.current_target)) {
            return &draft;
        }
    }
    return nullptr;
}

void open_script_editor(EditorWorld& editor_world, ScriptTargetRef target) {
    if (!script_editor_target_available(editor_world, target)) {
        target = ScriptTargetRef{ScriptTargetKind::Map, 0};
    }
    editor_world.script_editor.open = true;
    editor_world.script_editor.current_target = target;
    script_editor_draft_for(editor_world, target);
    std::cout << "Script editor: " << script_editor_target_label(target) << '\n';
}

void open_script_editor_default(EditorWorld& editor_world) {
    open_script_editor(editor_world, default_script_editor_target(editor_world));
}

void toggle_script_editor(EditorWorld& editor_world) {
    if (editor_world.script_editor.open) {
        editor_world.script_editor.open = false;
        std::cout << "Script editor closed.\n";
    } else {
        open_script_editor_default(editor_world);
    }
}

bool script_editor_insert_text(EditorWorld& editor_world, const std::string_view text) {
    if (text.empty()) {
        return false;
    }
    return mutate_source(editor_world, "EDITING.", [text](ScriptEditorDraft& draft) {
        erase_selection(draft);
        draft.source.insert(static_cast<std::size_t>(draft.caret), text);
        draft.caret += static_cast<int>(text.size());
        return true;
    });
}

bool script_editor_newline(EditorWorld& editor_world) {
    return script_editor_insert_text(editor_world, "\n");
}

bool script_editor_insert_tab(EditorWorld& editor_world) {
    return script_editor_insert_text(editor_world, "    ");
}

bool script_editor_backspace(EditorWorld& editor_world) {
    return mutate_source(editor_world, "EDITING.", [](ScriptEditorDraft& draft) {
        if (erase_selection(draft)) {
            return true;
        }
        if (draft.caret <= 0) {
            return false;
        }
        draft.source.erase(static_cast<std::size_t>(draft.caret - 1), 1U);
        --draft.caret;
        return true;
    });
}

bool script_editor_delete(EditorWorld& editor_world) {
    return mutate_source(editor_world, "EDITING.", [](ScriptEditorDraft& draft) {
        if (erase_selection(draft)) {
            return true;
        }
        if (draft.caret >= static_cast<int>(draft.source.size())) {
            return false;
        }
        draft.source.erase(static_cast<std::size_t>(draft.caret), 1U);
        return true;
    });
}

bool script_editor_move_caret(EditorWorld& editor_world, const ScriptEditorMove move, const bool selecting) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return false;
    }
    const int old_caret = draft->caret;
    const int current_line = line_for_offset(draft->source, draft->caret);
    const int current_column = column_for_offset(draft->source, draft->caret);
    int next = old_caret;
    switch (move) {
    case ScriptEditorMove::Left:
        next = std::max(0, old_caret - 1);
        break;
    case ScriptEditorMove::Right:
        next = std::min(static_cast<int>(draft->source.size()), old_caret + 1);
        break;
    case ScriptEditorMove::Up:
        next = offset_for_line_column(draft->source, current_line - 1, current_column);
        break;
    case ScriptEditorMove::Down:
        next = offset_for_line_column(draft->source, current_line + 1, current_column);
        break;
    case ScriptEditorMove::Home:
        next = line_start_offset(draft->source, current_line);
        break;
    case ScriptEditorMove::End:
        next = line_end_offset(draft->source, line_start_offset(draft->source, current_line));
        break;
    case ScriptEditorMove::PageUp:
        next = offset_for_line_column(draft->source, current_line - 18, current_column);
        draft->scroll_line = std::max(0, draft->scroll_line - 18);
        break;
    case ScriptEditorMove::PageDown:
        next = offset_for_line_column(draft->source, current_line + 18, current_column);
        draft->scroll_line = std::min(std::max(0, line_count(draft->source) - 1), draft->scroll_line + 18);
        break;
    }
    move_to_offset(*draft, next, selecting);
    return next != old_caret;
}

bool script_editor_select_all(EditorWorld& editor_world) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return false;
    }
    draft->selection_anchor = 0;
    draft->caret = static_cast<int>(draft->source.size());
    return true;
}

std::string script_editor_selected_text(const EditorWorld& editor_world) {
    const ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return {};
    }
    const auto [start, end] = selection_range(*draft);
    if (start == end) {
        return {};
    }
    return draft->source.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

bool script_editor_replace_selection(EditorWorld& editor_world, const std::string_view text) {
    return mutate_source(editor_world, "EDITING.", [text](ScriptEditorDraft& draft) {
        const bool had_selection = erase_selection(draft);
        draft.source.insert(static_cast<std::size_t>(draft.caret), text);
        draft.caret += static_cast<int>(text.size());
        return had_selection || !text.empty();
    });
}

bool script_editor_cut_selection(EditorWorld& editor_world, std::string& cut_text) {
    cut_text = script_editor_selected_text(editor_world);
    if (cut_text.empty()) {
        return false;
    }
    return mutate_source(editor_world, "EDITING.", [](ScriptEditorDraft& draft) {
        return erase_selection(draft);
    });
}

bool script_editor_local_undo(EditorWorld& editor_world) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr || draft->read_only || draft->undo_stack.empty()) {
        return false;
    }
    draft->redo_stack.push_back(draft->source);
    draft->source = std::move(draft->undo_stack.back());
    draft->undo_stack.pop_back();
    draft->caret = std::clamp(draft->caret, 0, static_cast<int>(draft->source.size()));
    draft->selection_anchor = -1;
    draft->dirty = true;
    draft->status = "LOCAL UNDO.";
    return true;
}

bool script_editor_local_redo(EditorWorld& editor_world) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr || draft->read_only || draft->redo_stack.empty()) {
        return false;
    }
    draft->undo_stack.push_back(draft->source);
    draft->source = std::move(draft->redo_stack.back());
    draft->redo_stack.pop_back();
    draft->caret = std::clamp(draft->caret, 0, static_cast<int>(draft->source.size()));
    draft->selection_anchor = -1;
    draft->dirty = true;
    draft->status = "LOCAL REDO.";
    return true;
}

void script_editor_set_caret_line_column(EditorWorld& editor_world, const int line, const int column) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return;
    }
    move_to_offset(*draft, offset_for_line_column(draft->source, line, column), false);
}

void script_editor_scroll(EditorWorld& editor_world, const int line_delta) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return;
    }
    draft->scroll_line = std::clamp(
        draft->scroll_line + line_delta,
        0,
        std::max(0, line_count(draft->source) - 1)
    );
}

bool script_editor_apply_current(EditorWorld& editor_world) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return false;
    }
    if (draft->read_only) {
        draft->status = "READ-ONLY. PRESS REPLACE TO START SOURCE.";
        draft->compile_error = true;
        return false;
    }

    if (!script_editor_target_available(editor_world, draft->target)) {
        draft->status = "TARGET NO LONGER EXISTS.";
        draft->compile_error = true;
        return false;
    }

    if (source_is_blank(draft->source)) {
        push_undo_snapshot(editor_world, "script edit");
        const bool changed = clear_script_for_target(editor_world.scripts, draft->target);
        if (draft->target.kind == ScriptTargetKind::Entity) {
            detach_entity_script(editor_world.scripts, editor_world.entities, draft->target.id);
        }
        if (changed) {
            editor_world.dirty_scripts = true;
        }
        draft->dirty = false;
        draft->compile_error = false;
        draft->status = "SCRIPT CLEARED.";
        return true;
    }

    const ScriptCompileResult compiled = compile_script(draft->source);
    if (!compiled.ok) {
        draft->compile_error = true;
        draft->status = compiled.message;
        std::cout << "Cannot compile script: " << compiled.message << '\n';
        return false;
    }

    push_undo_snapshot(editor_world, "script edit");
    apply_program_for_target(editor_world, draft->target, compiled.program);
    editor_world.dirty_scripts = true;
    draft->dirty = false;
    draft->read_only = false;
    draft->compile_error = false;
    draft->status = compiled.message;
    std::cout << "Compiled " << script_editor_target_label(draft->target) << " script.\n";
    return true;
}

bool script_editor_apply_dirty_before_save(EditorWorld& editor_world) {
    if (!script_editor_has_dirty_drafts(editor_world)) {
        return true;
    }
    const ScriptTargetRef original_target = editor_world.script_editor.current_target;
    const bool was_open = editor_world.script_editor.open;
    editor_world.script_editor.open = true;
    for (ScriptEditorDraft& draft : editor_world.script_editor.drafts) {
        if (!draft.dirty) {
            continue;
        }
        editor_world.script_editor.current_target = draft.target;
        if (!script_editor_apply_current(editor_world)) {
            editor_world.script_editor.open = was_open || true;
            return false;
        }
    }
    editor_world.script_editor.current_target = original_target;
    editor_world.script_editor.open = was_open;
    return true;
}

void script_editor_clear_clean_drafts(EditorWorld& editor_world) {
    auto& drafts = editor_world.script_editor.drafts;
    drafts.erase(
        std::remove_if(drafts.begin(), drafts.end(), [](const ScriptEditorDraft& draft) {
            return !draft.dirty && !draft.read_only;
        }),
        drafts.end()
    );
}

bool script_editor_has_dirty_drafts(const EditorWorld& editor_world) {
    return std::any_of(
        editor_world.script_editor.drafts.begin(),
        editor_world.script_editor.drafts.end(),
        [](const ScriptEditorDraft& draft) {
            return draft.dirty && !draft.read_only;
        }
    );
}

void script_editor_start_editable_template(EditorWorld& editor_world) {
    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return;
    }
    push_local_undo(*draft);
    draft->source = editable_template_for(draft->target);
    draft->caret = static_cast<int>(draft->source.size());
    draft->selection_anchor = -1;
    draft->scroll_line = 0;
    draft->read_only = false;
    draft->dirty = true;
    draft->compile_error = false;
    draft->status = "EDITABLE TEMPLATE STARTED.";
}

} // namespace undecedent
