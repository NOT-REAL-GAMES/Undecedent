#include "undecedent/script_editor_ui.hpp"

#include "undecedent/core_draw.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/script_editor.hpp"
#include "undecedent/sdf_text.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace undecedent {
namespace {

struct WorkspaceLayout {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    float tab_y = 0.0F;
    float code_x = 0.0F;
    float code_y = 0.0F;
    float code_w = 0.0F;
    float code_h = 0.0F;
    float footer_y = 0.0F;
};

bool point_in_rect(const float px, const float py, const float x, const float y, const float w, const float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

WorkspaceLayout workspace_layout(const int width, const int height) {
    const float margin = 44.0F;
    const float x = std::min(margin, std::max(12.0F, static_cast<float>(width) * 0.04F));
    const float y = std::min(42.0F, std::max(10.0F, static_cast<float>(height) * 0.04F));
    WorkspaceLayout layout;
    layout.x = x;
    layout.y = y;
    layout.w = std::max(360.0F, static_cast<float>(width) - (x * 2.0F));
    layout.h = std::max(300.0F, static_cast<float>(height) - y - 46.0F);
    layout.tab_y = layout.y + 12.0F;
    layout.code_x = layout.x + 18.0F;
    layout.code_y = layout.y + 72.0F;
    layout.code_w = layout.w - 36.0F;
    layout.code_h = layout.h - 112.0F;
    layout.footer_y = layout.y + layout.h - 30.0F;
    return layout;
}

void draw_rect(const float x, const float y, const float w, const float h, const int width, const int height, const float alpha) {
    core_begin(kCoreQuads);
    core_color4f(0.0F, 0.0F, 0.0F, alpha);
    draw_screen_quad(x, y, w, h, width, height);
    core_end();
}

void draw_outline(const float x, const float y, const float w, const float h, const int width, const int height, const float alpha = 0.92F) {
    core_begin(GL_LINES);
    core_color4f(0.90F, 0.96F, 0.76F, alpha);
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
    core_end();
}

void draw_text(
    const std::string& text,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height,
    const float alpha = 0.96F,
    const SdfTextClip clip = {}
) {
    draw_sdf_text(text, x, y, size, width, height, SdfTextColor{1.0F, 1.0F, 1.0F, alpha}, clip);
}

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    std::string line;
    for (const char ch : source) {
        if (ch == '\n') {
            lines.push_back(std::move(line));
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

std::pair<int, int> selection_range(const ScriptEditorDraft& draft) {
    if (draft.selection_anchor < 0 || draft.selection_anchor == draft.caret) {
        return {draft.caret, draft.caret};
    }
    return {std::min(draft.selection_anchor, draft.caret), std::max(draft.selection_anchor, draft.caret)};
}

int offset_for_line(const std::vector<std::string>& lines, const int line_index) {
    int offset = 0;
    for (int i = 0; i < line_index && i < static_cast<int>(lines.size()); ++i) {
        offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
    }
    return offset;
}

float measured_prefix_width(const std::string& line, const int column, const float text_size) {
    if (column <= 0) {
        return 0.0F;
    }
    const int clamped = std::clamp(column, 0, static_cast<int>(line.size()));
    const SdfTextMetrics metrics = measure_sdf_text(line.substr(0, static_cast<std::size_t>(clamped)), text_size);
    if (metrics.width > 0.0F) {
        return metrics.width;
    }
    return static_cast<float>(clamped) * (text_size * 0.56F);
}

int column_from_screen_x(const std::string& line, const float local_x, const float text_size) {
    if (local_x <= 0.0F) {
        return 0;
    }
    for (int column = 0; column < static_cast<int>(line.size()); ++column) {
        const float left = measured_prefix_width(line, column, text_size);
        const float right = measured_prefix_width(line, column + 1, text_size);
        if (local_x < (left + right) * 0.5F) {
            return column;
        }
    }
    return static_cast<int>(line.size());
}

bool current_target_is(const EditorWorld& editor_world, const ScriptTargetKind kind) {
    return editor_world.script_editor.current_target.kind == kind;
}

bool draw_tab(
    EditorWorld& editor_world,
    const ScriptTargetRef target,
    const bool enabled,
    const float x,
    const float y,
    const int width,
    const int height
) {
    constexpr float tab_w = 106.0F;
    constexpr float tab_h = 28.0F;
    const bool active = editor_world.script_editor.current_target.kind == target.kind &&
        editor_world.script_editor.current_target.id == target.id;
    core_begin(kCoreQuads);
    core_color4f(active ? 0.18F : 0.0F, active ? 0.38F : 0.0F, active ? 0.32F : 0.0F, enabled ? 0.70F : 0.24F);
    draw_screen_quad(x, y, tab_w, tab_h, width, height);
    core_end();
    draw_outline(x, y, tab_w, tab_h, width, height, enabled ? 0.88F : 0.32F);
    const char* label = target.kind == ScriptTargetKind::Map ? "MAP" :
        target.kind == ScriptTargetKind::Entity ? "ENTITY" : "SECTOR";
    draw_text(label, x + 14.0F, y + 8.0F, 12.0F, width, height, enabled ? 0.96F : 0.40F);
    return active;
}

void draw_button(
    const std::string& label,
    const float x,
    const float y,
    const float w,
    const float h,
    const int width,
    const int height,
    const bool active = false
) {
    core_begin(kCoreQuads);
    core_color4f(active ? 0.18F : 0.0F, active ? 0.40F : 0.0F, active ? 0.34F : 0.0F, active ? 0.80F : 0.46F);
    draw_screen_quad(x, y, w, h, width, height);
    core_end();
    draw_outline(x, y, w, h, width, height);
    draw_text(label, x + 12.0F, y + 8.0F, 12.0F, width, height);
}

bool entity_script_button_rect(EditorWorld& editor_world, const int width, const int height, float& x, float& y, float& w, float& h) {
    if (width <= 0 || height <= 0 || selected_entity_script_id(editor_world) == 0) {
        return false;
    }
    w = 70.0F;
    h = 20.0F;
    x = static_cast<float>(width) - 246.0F - 14.0F + 246.0F - w - 10.0F;
    y = 58.0F;
    return true;
}

bool sector_script_button_rect(EditorWorld& editor_world, const int width, const int height, float& x, float& y, float& w, float& h) {
    if (width <= 0 || height <= 0 || selected_sector_script_id(editor_world) == 0) {
        return false;
    }
    x = 14.0F;
    y = 88.0F;
    w = 112.0F;
    h = 28.0F;
    return true;
}

bool target_has_script(const EditorWorld& editor_world, const ScriptTargetRef target) {
    return script_for_target(editor_world.scripts, target) != nullptr;
}

} // namespace

void draw_script_editor_workspace(EditorWorld& editor_world, const int width, const int height) {
    if (!editor_world.script_editor.open || width <= 0 || height <= 0) {
        return;
    }

    ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
    if (draft == nullptr) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const WorkspaceLayout layout = workspace_layout(width, height);
    draw_rect(layout.x, layout.y, layout.w, layout.h, width, height, 0.88F);
    draw_outline(layout.x, layout.y, layout.w, layout.h, width, height, 0.96F);
    draw_text("SCRIPT WORKSPACE", layout.x + 16.0F, layout.y + 16.0F, 13.0F, width, height);

    const ScriptTargetRef map_target{ScriptTargetKind::Map, 0};
    const std::uint64_t entity_id = selected_entity_script_id(editor_world);
    const std::uint64_t sector_id = selected_sector_script_id(editor_world);
    const ScriptTargetRef entity_target{ScriptTargetKind::Entity, entity_id};
    const ScriptTargetRef sector_target{ScriptTargetKind::Sector, sector_id};
    draw_tab(editor_world, map_target, true, layout.x + 16.0F, layout.tab_y + 28.0F, width, height);
    draw_tab(editor_world, entity_target, entity_id != 0, layout.x + 126.0F, layout.tab_y + 28.0F, width, height);
    draw_tab(editor_world, sector_target, sector_id != 0, layout.x + 236.0F, layout.tab_y + 28.0F, width, height);

    const float button_y = layout.tab_y + 28.0F;
    draw_button("APPLY", layout.x + layout.w - 256.0F, button_y, 78.0F, 28.0F, width, height, draft->dirty);
    draw_button(draft->read_only ? "REPLACE" : "TEMPLATE", layout.x + layout.w - 170.0F, button_y, 100.0F, 28.0F, width, height);
    draw_button("CLOSE", layout.x + layout.w - 62.0F, button_y, 48.0F, 28.0F, width, height);

    draw_rect(layout.code_x, layout.code_y, layout.code_w, layout.code_h, width, height, 0.58F);
    draw_outline(layout.code_x, layout.code_y, layout.code_w, layout.code_h, width, height, 0.72F);

    const std::vector<std::string> lines = split_lines(draft->source);
    constexpr float text_size = 13.0F;
    constexpr float line_h = 18.0F;
    constexpr float gutter_w = 54.0F;
    const int visible_lines = std::max(1, static_cast<int>(layout.code_h / line_h) - 1);
    const int first_line = std::clamp(draft->scroll_line, 0, std::max(0, static_cast<int>(lines.size()) - 1));
    const int last_line = std::min(static_cast<int>(lines.size()), first_line + visible_lines);
    const auto [selection_start, selection_end] = selection_range(*draft);
    const SdfTextClip clip{layout.code_x + gutter_w, layout.code_y, layout.code_w - gutter_w - 8.0F, layout.code_h, true};

    for (int line = first_line; line < last_line; ++line) {
        const float y = layout.code_y + 8.0F + (static_cast<float>(line - first_line) * line_h);
        const int line_start = offset_for_line(lines, line);
        const int line_end = line_start + static_cast<int>(lines[static_cast<std::size_t>(line)].size());
        draw_text(std::to_string(line + 1), layout.code_x + 10.0F, y, 11.0F, width, height, 0.54F);

        const int sel_a = std::max(selection_start, line_start);
        const int sel_b = std::min(selection_end, line_end);
        if (sel_a < sel_b) {
            const std::string& current_line = lines[static_cast<std::size_t>(line)];
            const float selection_x = measured_prefix_width(current_line, sel_a - line_start, text_size);
            const float selection_w =
                measured_prefix_width(current_line, sel_b - line_start, text_size) - selection_x;
            core_begin(kCoreQuads);
            core_color4f(0.22F, 0.44F, 0.80F, 0.42F);
            draw_screen_quad(
                layout.code_x + gutter_w + selection_x,
                y - 1.0F,
                std::max(2.0F, selection_w),
                line_h,
                width,
                height
            );
            core_end();
        }

        draw_text(
            lines[static_cast<std::size_t>(line)],
            layout.code_x + gutter_w,
            y,
            text_size,
            width,
            height,
            draft->read_only ? 0.72F : 0.98F,
            clip
        );

        if (!draft->read_only && draft->caret >= line_start && draft->caret <= line_end) {
            const float caret_x = layout.code_x + gutter_w +
                measured_prefix_width(lines[static_cast<std::size_t>(line)], draft->caret - line_start, text_size);
            core_begin(GL_LINES);
            core_color4f(1.0F, 1.0F, 1.0F, 0.96F);
            draw_screen_line(caret_x, y - 2.0F, caret_x, y + line_h - 1.0F, width, height);
            core_end();
        }
    }

    const std::string mode = draft->read_only ? "READ ONLY" : (draft->dirty ? "DIRTY" : "CLEAN");
    const std::string footer = script_editor_target_label(draft->target) + "  " + mode + "  " + draft->status;
    draw_text(footer, layout.x + 18.0F, layout.footer_y + 8.0F, 12.0F, width, height, draft->compile_error ? 1.0F : 0.86F);
    glDisable(GL_BLEND);
}

bool handle_script_editor_mouse_click(EditorWorld& editor_world, const int width, const int height, const float mouse_x, const float mouse_y) {
    if (!editor_world.script_editor.open || width <= 0 || height <= 0) {
        return false;
    }
    const WorkspaceLayout layout = workspace_layout(width, height);

    const std::uint64_t entity_id = selected_entity_script_id(editor_world);
    const std::uint64_t sector_id = selected_sector_script_id(editor_world);
    const float tab_y = layout.tab_y + 28.0F;
    if (point_in_rect(mouse_x, mouse_y, layout.x + 16.0F, tab_y, 106.0F, 28.0F)) {
        open_script_editor(editor_world, ScriptTargetRef{ScriptTargetKind::Map, 0});
        return true;
    }
    if (entity_id != 0 && point_in_rect(mouse_x, mouse_y, layout.x + 126.0F, tab_y, 106.0F, 28.0F)) {
        open_script_editor(editor_world, ScriptTargetRef{ScriptTargetKind::Entity, entity_id});
        return true;
    }
    if (sector_id != 0 && point_in_rect(mouse_x, mouse_y, layout.x + 236.0F, tab_y, 106.0F, 28.0F)) {
        open_script_editor(editor_world, ScriptTargetRef{ScriptTargetKind::Sector, sector_id});
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, layout.x + layout.w - 256.0F, tab_y, 78.0F, 28.0F)) {
        script_editor_apply_current(editor_world);
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, layout.x + layout.w - 170.0F, tab_y, 100.0F, 28.0F)) {
        script_editor_start_editable_template(editor_world);
        return true;
    }
    if (point_in_rect(mouse_x, mouse_y, layout.x + layout.w - 62.0F, tab_y, 48.0F, 28.0F)) {
        editor_world.script_editor.open = false;
        return true;
    }

    if (point_in_rect(mouse_x, mouse_y, layout.code_x, layout.code_y, layout.code_w, layout.code_h)) {
        constexpr float line_h = 18.0F;
        constexpr float gutter_w = 54.0F;
        constexpr float text_size = 13.0F;
        ScriptEditorDraft* draft = active_script_editor_draft(editor_world);
        const int line = draft == nullptr
            ? 0
            : std::max(0, draft->scroll_line +
                static_cast<int>((mouse_y - layout.code_y - 8.0F) / line_h));
        const std::vector<std::string> lines = draft == nullptr ? std::vector<std::string>{} : split_lines(draft->source);
        const std::string line_text = line >= 0 && line < static_cast<int>(lines.size())
            ? lines[static_cast<std::size_t>(line)]
            : std::string{};
        const int column = column_from_screen_x(line_text, mouse_x - layout.code_x - gutter_w, text_size);
        script_editor_set_caret_line_column(editor_world, line, column);
        return true;
    }

    return true;
}

bool handle_script_editor_mouse_wheel(
    EditorWorld& editor_world,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y,
    const float scroll_y
) {
    if (!editor_world.script_editor.open || width <= 0 || height <= 0) {
        return false;
    }
    const WorkspaceLayout layout = workspace_layout(width, height);
    if (!point_in_rect(mouse_x, mouse_y, layout.x, layout.y, layout.w, layout.h)) {
        return true;
    }
    script_editor_scroll(editor_world, static_cast<int>(std::round(-scroll_y * 3.0F)));
    return true;
}

void draw_script_quick_buttons(EditorWorld& editor_world, const int width, const int height) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (sector_script_button_rect(editor_world, width, height, x, y, w, h)) {
        const ScriptTargetRef target{ScriptTargetKind::Sector, selected_sector_script_id(editor_world)};
        draw_button(target_has_script(editor_world, target) ? "SCRIPT*" : "SCRIPT", x, y, w, h, width, height);
    }
    if (entity_script_button_rect(editor_world, width, height, x, y, w, h)) {
        const ScriptTargetRef target{ScriptTargetKind::Entity, selected_entity_script_id(editor_world)};
        draw_button(target_has_script(editor_world, target) ? "SCRIPT*" : "SCRIPT", x, y, w, h, width, height);
    }
    glDisable(GL_BLEND);
}

bool handle_script_quick_button_click(EditorWorld& editor_world, const int width, const int height, const float mouse_x, const float mouse_y) {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    if (sector_script_button_rect(editor_world, width, height, x, y, w, h) &&
        point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        open_script_editor(editor_world, ScriptTargetRef{ScriptTargetKind::Sector, selected_sector_script_id(editor_world)});
        return true;
    }
    if (entity_script_button_rect(editor_world, width, height, x, y, w, h) &&
        point_in_rect(mouse_x, mouse_y, x, y, w, h)) {
        open_script_editor(editor_world, ScriptTargetRef{ScriptTargetKind::Entity, selected_entity_script_id(editor_world)});
        return true;
    }
    return false;
}

} // namespace undecedent
