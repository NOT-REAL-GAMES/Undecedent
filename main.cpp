#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "undecedent/core_draw.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "undecedent/debug_draw.hpp"
#include "undecedent/deferred_renderer.hpp"
#include "undecedent/editor.hpp"
#include "undecedent/editor_render.hpp"
#include "undecedent/editor_ui.hpp"
#include "undecedent/game_camera.hpp"
#include "undecedent/game_control.hpp"
#include "undecedent/map_io.hpp"
#include "undecedent/math3d.hpp"
#include "undecedent/material_texture.hpp"
#include "undecedent/materials.hpp"
#include "undecedent/physics.hpp"
#include "undecedent/runtime_render_cache.hpp"
#include "undecedent/runtime_render.hpp"
#include "undecedent/runtime_pick.hpp"
#include "undecedent/runtime_world.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/sdl_platform.hpp"
#include "undecedent/sdf_text.hpp"
#include "undecedent/script_editor.hpp"
#include "undecedent/script_editor_ui.hpp"
#include "undecedent/triangulator.hpp"

namespace {
using undecedent::core_begin;
using undecedent::core_color4f;
using undecedent::core_draw_begin_frame;
using undecedent::core_draw_flush;
using undecedent::core_end;
using undecedent::core_set_line_width;
using undecedent::core_set_identity_mvp;
using undecedent::draw_screen_line;
using undecedent::draw_screen_quad;
using undecedent::draw_editor_2d_view;
using undecedent::draw_deferred_runtime_world;
using undecedent::draw_point_lights_3d;
using undecedent::draw_player_spawn_3d;
using undecedent::draw_runtime_world;
using undecedent::draw_stroke_text;
using undecedent::draw_sdf_text;
using undecedent::draw_entity_dropdown;
using undecedent::draw_entity_inspector;
using undecedent::draw_material_selector;
using undecedent::draw_sculpt_button;
using undecedent::draw_script_editor_workspace;
using undecedent::draw_script_quick_buttons;
using undecedent::draw_subdivision_controls;
using undecedent::draw_translation_gizmo;
using undecedent::screen_to_ndc_x;
using undecedent::screen_to_ndc_y;
using undecedent::sdf_text_begin_frame;
using undecedent::sdf_text_flush;
using undecedent::sdf_text_shutdown;
using undecedent::configure_gl_attributes;
using undecedent::log_sdl_error;
using undecedent::toggle_exclusive_fullscreen;
using undecedent::add_vec3;
using undecedent::mul_vec3;
using undecedent::adjust_selected_sector_floor_heights;
using undecedent::adjust_selected_sector_heights;
using undecedent::adjust_displacement_brush_radius;
using undecedent::apply_editor_scroll_zoom;
using undecedent::apply_editor_slice_scroll;
using undecedent::apply_material_to_surface;
using undecedent::cancel_plane_tool;
using undecedent::clear_entity_selection;
using undecedent::clear_sector_selection;
using undecedent::commit_plane_tool;
using undecedent::committed_vertex_at_screen;
using undecedent::delete_selected_sectors;
using undecedent::destroy_runtime_render_cache;
using undecedent::draft_contains_point;
using undecedent::draft_contains_point_except;
using undecedent::draft_preview_result;
using undecedent::draft_vertex_at_screen;
using undecedent::EditorCamera;
using undecedent::Editor2DRenderConfig;
using undecedent::editor_grid_world_step;
using undecedent::EditorWorld;
using undecedent::EntityPlacementType;
using undecedent::finish_committed_vertex_drag;
using undecedent::format_world_units;
using undecedent::editor_scroll_zoom_delta;
using undecedent::GameCamera;
using undecedent::GameControlConfig;
using undecedent::GameRenderConfig;
using undecedent::MaterialTextureArray;
using undecedent::MaterialTextureChannel;
using undecedent::handle_entity_dropdown_click;
using undecedent::kCoreQuads;
using undecedent::handle_entity_inspector_click;
using undecedent::handle_sculpt_button_click;
using undecedent::handle_script_editor_mouse_click;
using undecedent::handle_script_editor_mouse_wheel;
using undecedent::handle_script_quick_button_click;
using undecedent::handle_subdivision_controls_click;
using undecedent::is_dragged_committed_ref;
using undecedent::is_sector_selected;
using undecedent::matching_committed_vertices;
using undecedent::merge_selected_sectors;
using undecedent::move_dragged_committed_vertices;
using undecedent::place_entity;
using undecedent::place_entity_at_origin;
using undecedent::PlaneToolMode;
using undecedent::pick_runtime_surface;
using undecedent::pick_editor_entity_2d;
using undecedent::pick_editor_entity_3d;
using undecedent::select_entity;
using undecedent::player_physics_config;
using undecedent::PlaytestPlayerState;
using undecedent::rebuild_runtime_geometry;
using undecedent::redo_editor_action;
using undecedent::refresh_draft;
using undecedent::RuntimeRenderCache;
using undecedent::screen_to_world_x;
using undecedent::screen_to_world_y;
using undecedent::sector_at_point;
using undecedent::sector_visible_in_slice;
using undecedent::select_single_sector;
using undecedent::clear_material_texture_path;
using undecedent::set_material_texture;
using undecedent::script_editor_apply_dirty_before_save;
using undecedent::script_editor_apply_current;
using undecedent::script_editor_backspace;
using undecedent::script_editor_clear_clean_drafts;
using undecedent::script_editor_cut_selection;
using undecedent::script_editor_delete;
using undecedent::script_editor_has_dirty_drafts;
using undecedent::script_editor_insert_tab;
using undecedent::script_editor_insert_text;
using undecedent::script_editor_local_redo;
using undecedent::script_editor_local_undo;
using undecedent::script_editor_move_caret;
using undecedent::script_editor_newline;
using undecedent::script_editor_replace_selection;
using undecedent::script_editor_select_all;
using undecedent::script_editor_selected_text;
using undecedent::ScriptEditorMove;
using undecedent::sculpt_displacement_at_pick;
using undecedent::start_hole_plane;
using undecedent::start_knife_tool;
using undecedent::start_outer_plane;
using undecedent::start_translation_gizmo_drag;
using undecedent::SurfacePick;
using undecedent::toggle_sector_selection;
using undecedent::undo_editor_action;
using undecedent::update_committed_vertex_hover;
using undecedent::update_editor_camera;
using undecedent::update_game_camera;
using undecedent::update_game_camera_mouse_look;
using undecedent::update_playtest_camera;
using undecedent::update_snapped_mouse;
using undecedent::update_translation_gizmo_drag;
using undecedent::finish_translation_gizmo_drag;
using undecedent::world_to_ndc_x;
using undecedent::world_to_ndc_y;
using undecedent::world_to_screen_x;
using undecedent::world_to_screen_y;

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kEditorMajorGridEvery = 4;
constexpr float kEditorMinZoom = 1.0F / 65536.0F;
constexpr float kGameMoveSpeed = 180.0F;
constexpr float kGameLookSpeed = 1.9F;
constexpr float kPlayerRadius = 8.0F;
constexpr float kPlayerHeight = 56.0F;
constexpr float kPlayerEyeHeight = 48.0F;
constexpr float kPlayerGravity = 900.0F;
constexpr float kPlayerJumpVelocity = 320.0F;
constexpr float kPlayerTerminalFallSpeed = 900.0F;
constexpr float kPlayerGravityDrag = 0.10F;
constexpr float kPlayerGroundProbe = 1.0F;
constexpr float kGameNearPlane = 1.0F;
constexpr float kGameFarPlane = 20000.0F;
constexpr float kSectorHeightStep = 8.0F;
constexpr float kScaleIndicatorTargetPixels = 160.0F;
constexpr float kScaleIndicatorMinPixels = 80.0F;
constexpr float kScaleIndicatorMaxPixels = 190.0F;
constexpr float kFpsCounterUpdateSeconds = 0.25F;

struct ProfilerDisplay {
    double frame_ms = 0.0;
    double events_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double gbuffer_ms = 0.0;
    double shadow_pack_upload_ms = 0.0;
    double shadow_ms = 0.0;
    double screen_shadow_ms = 0.0;
    double lighting_ms = 0.0;
    double wire_overlay_ms = 0.0;
    double overlay_ms = 0.0;
    double pace_ms = 0.0;
    double finish_ms = 0.0;
    double swap_ms = 0.0;
    int fps = 0;
    int effective_swap_interval = 0;
    int fps_cap = 0;
    int total_triangles = 0;
    int visible_triangles = 0;
    int sectors = 0;
    int walls = 0;
    int shadow_cache_hits = 0;
    int shadow_cache_misses = 0;
    int point_shadow_lights = 0;
    int point_shadow_faces = 0;
    int sun_shadow_cascades = 0;
};

struct ProfilerAccumulator {
    double frame_ms = 0.0;
    double events_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double gbuffer_ms = 0.0;
    double shadow_pack_upload_ms = 0.0;
    double shadow_ms = 0.0;
    double screen_shadow_ms = 0.0;
    double lighting_ms = 0.0;
    double wire_overlay_ms = 0.0;
    double overlay_ms = 0.0;
    double pace_ms = 0.0;
    double finish_ms = 0.0;
    double swap_ms = 0.0;
    double seconds = 0.0;
    int frames = 0;
};

struct BenchmarkState {
    bool enabled = false;
    bool skip_clear = false;
    bool skip_swap = false;
};

struct BenchmarkAccumulator {
    double frame_ms = 0.0;
    double events_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double gbuffer_ms = 0.0;
    double shadow_pack_upload_ms = 0.0;
    double shadow_ms = 0.0;
    double screen_shadow_ms = 0.0;
    double lighting_ms = 0.0;
    double wire_overlay_ms = 0.0;
    double overlay_ms = 0.0;
    double pace_ms = 0.0;
    double finish_ms = 0.0;
    double swap_ms = 0.0;
    double seconds = 0.0;
    int frames = 0;
};

struct EffectsMenuLayout {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
    float row_y = 0.0F;
    float row_h = 0.0F;
};

struct MaterialTextureControlsLayout {
    float x = 0.0F;
    float y = 0.0F;
    float channel_y = 0.0F;
    float channel_w = 42.0F;
    float button_w = 86.0F;
    float button_h = 22.0F;
    float gap = 8.0F;
};

struct PresentConfig {
    int swap_interval = 0;
    int effective_swap_interval = 0;
    int fps_cap = 0;
    bool yield_when_uncapped = false;
};

enum class RenderEffectToggle {
    None,
    Vsm,
    Csm,
    ScreenSpace,
    Fog,
};

enum class RenderMenuRow {
    None,
    Vsm,
    Csm,
    ScreenSpace,
    Fog,
    Present,
    Cap,
    Yield,
};

struct MapDialogRequests {
    std::mutex mutex;
    std::string pending_save_path;
    std::string pending_load_path;
    std::vector<std::string> messages;
};

struct MaterialDialogRequests {
    std::mutex mutex;
    std::string pending_texture_path;
    int pending_material_id = -1;
    MaterialTextureChannel pending_channel = MaterialTextureChannel::Albedo;
    int requested_material_id = -1;
    MaterialTextureChannel requested_channel = MaterialTextureChannel::Albedo;
    std::vector<std::string> messages;
};

enum class AppMode {
    Editor3D,
    Editor2D,
    Playtest,
};


void frame_game_camera_on_sectors(GameCamera& camera, const std::vector<undecedent::SectorPlane>& sectors);

double ticks_to_ms(const Uint64 start, const Uint64 end) {
    return static_cast<double>(end - start) / 1'000'000.0;
}

std::string format_ms(const double milliseconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << milliseconds;
    return stream.str();
}

std::string format_fps(const double fps) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(fps >= 1000.0 ? 0 : 1) << fps;
    return stream.str();
}

const char* swap_interval_label(const int interval) {
    switch (interval) {
    case -1:
        return "ADAPT";
    case 0:
        return "OFF";
    case 1:
        return "VSYNC";
    default:
        return "CUSTOM";
    }
}

std::string fps_cap_label(const int fps_cap) {
    return fps_cap <= 0 ? std::string("UNCAP") : std::to_string(fps_cap);
}

void apply_swap_interval(SDL_Window* window, PresentConfig& config) {
    (void)window;
    bool applied = SDL_GL_SetSwapInterval(config.swap_interval);
    if (!applied && config.swap_interval == -1) {
        std::cerr << "Adaptive swap interval unsupported: " << SDL_GetError()
                  << "; falling back to OFF.\n";
        config.swap_interval = 0;
        applied = SDL_GL_SetSwapInterval(config.swap_interval);
    }
    if (!applied) {
        std::cerr << "SDL_GL_SetSwapInterval(" << config.swap_interval
                  << ") failed: " << SDL_GetError() << '\n';
    }

    int effective = config.swap_interval;
    if (!SDL_GL_GetSwapInterval(&effective)) {
        std::cerr << "SDL_GL_GetSwapInterval failed: " << SDL_GetError() << '\n';
    }
    config.effective_swap_interval = effective;
    std::cout << "Present swap interval requested " << swap_interval_label(config.swap_interval)
              << " effective " << swap_interval_label(config.effective_swap_interval)
              << " (" << config.effective_swap_interval << ")\n";
}

void cycle_present_mode(SDL_Window* window, PresentConfig& config) {
    if (config.swap_interval == 0) {
        config.swap_interval = 1;
    } else if (config.swap_interval == 1) {
        config.swap_interval = -1;
    } else {
        config.swap_interval = 0;
    }
    apply_swap_interval(window, config);
}

void cycle_fps_cap(PresentConfig& config) {
    static constexpr std::array<int, 7> kFpsCaps{0, 1000, 500, 240, 144, 120, 60};
    const auto found = std::find(kFpsCaps.begin(), kFpsCaps.end(), config.fps_cap);
    if (found == kFpsCaps.end() || std::next(found) == kFpsCaps.end()) {
        config.fps_cap = kFpsCaps.front();
    } else {
        config.fps_cap = *std::next(found);
    }
    std::cout << "Frame cap " << fps_cap_label(config.fps_cap) << '\n';
}

double pace_frame_before_swap(const Uint64 frame_start_ticks, const PresentConfig& config) {
    const Uint64 pace_start_ticks = SDL_GetTicksNS();
    if (config.fps_cap > 0) {
        const Uint64 target_ns = static_cast<Uint64>(1'000'000'000ULL / static_cast<Uint64>(config.fps_cap));
        const Uint64 target_ticks = frame_start_ticks + target_ns;
        Uint64 now = SDL_GetTicksNS();
        if (now < target_ticks) {
            const Uint64 remaining_ns = target_ticks - now;
            if (remaining_ns > 2'000'000ULL) {
                SDL_Delay(static_cast<Uint32>((remaining_ns - 1'000'000ULL) / 1'000'000ULL));
            }
            while ((now = SDL_GetTicksNS()) < target_ticks) {
                if (target_ticks - now > 250'000ULL) {
                    SDL_Delay(0);
                }
            }
        }
    } else if (config.yield_when_uncapped) {
        SDL_Delay(0);
    }
    return ticks_to_ms(pace_start_ticks, SDL_GetTicksNS());
}

bool point_in_screen_rect(
    const float px,
    const float py,
    const float x,
    const float y,
    const float w,
    const float h
) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

MaterialTextureControlsLayout material_texture_controls_layout(const int height) {
    MaterialTextureControlsLayout layout;
    layout.x = 276.0F;
    layout.channel_y = static_cast<float>(height) - 70.0F;
    layout.y = static_cast<float>(height) - 43.0F;
    return layout;
}

enum class MaterialTextureControlAction {
    None,
    SelectAlbedo,
    SelectNormal,
    SelectSmoothness,
    SelectAo,
    SelectMetallic,
    Assign,
    Clear,
    CycleStorage,
};

const char* material_texture_storage_mode_label(const undecedent::MaterialTextureStorageMode mode) {
    switch (mode) {
    case undecedent::MaterialTextureStorageMode::JpegXlLossless:
        return "JXL-L";
    case undecedent::MaterialTextureStorageMode::JpegXlLossy:
        return "JXL-Q80";
    case undecedent::MaterialTextureStorageMode::SourceBytes:
    default:
        return "SRC";
    }
}

void cycle_material_texture_storage_mode(undecedent::MaterialTextureSource& source) {
    switch (source.storage_mode) {
    case undecedent::MaterialTextureStorageMode::SourceBytes:
        source.storage_mode = undecedent::MaterialTextureStorageMode::JpegXlLossless;
        break;
    case undecedent::MaterialTextureStorageMode::JpegXlLossless:
        source.storage_mode = undecedent::MaterialTextureStorageMode::JpegXlLossy;
        source.jxl_quality = 80;
        break;
    case undecedent::MaterialTextureStorageMode::JpegXlLossy:
    default:
        source.storage_mode = undecedent::MaterialTextureStorageMode::SourceBytes;
        source.jxl_quality = 80;
        break;
    }
}

MaterialTextureControlAction material_texture_control_at(
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    const MaterialTextureControlsLayout layout = material_texture_controls_layout(height);
    for (int channel_index = 0; channel_index < undecedent::kMaterialTextureChannelCount; ++channel_index) {
        const float x = layout.x + (static_cast<float>(channel_index) * (layout.channel_w + layout.gap));
        if (point_in_screen_rect(mouse_x, mouse_y, x, layout.channel_y, layout.channel_w, layout.button_h)) {
            switch (static_cast<undecedent::MaterialTextureChannel>(channel_index)) {
            case undecedent::MaterialTextureChannel::Normal: return MaterialTextureControlAction::SelectNormal;
            case undecedent::MaterialTextureChannel::Smoothness: return MaterialTextureControlAction::SelectSmoothness;
            case undecedent::MaterialTextureChannel::AmbientOcclusion: return MaterialTextureControlAction::SelectAo;
            case undecedent::MaterialTextureChannel::Metallic: return MaterialTextureControlAction::SelectMetallic;
            case undecedent::MaterialTextureChannel::Albedo:
            default:
                return MaterialTextureControlAction::SelectAlbedo;
            }
        }
    }
    if (point_in_screen_rect(mouse_x, mouse_y, layout.x, layout.y, layout.button_w, layout.button_h)) {
        return MaterialTextureControlAction::Assign;
    }
    if (point_in_screen_rect(
            mouse_x,
            mouse_y,
            layout.x + layout.button_w + layout.gap,
            layout.y,
            layout.button_w,
            layout.button_h
        )) {
        return MaterialTextureControlAction::Clear;
    }
    if (point_in_screen_rect(
            mouse_x,
            mouse_y,
            layout.x + ((layout.button_w + layout.gap) * 2.0F),
            layout.y,
            layout.button_w,
            layout.button_h
        )) {
        return MaterialTextureControlAction::CycleStorage;
    }
    return MaterialTextureControlAction::None;
}

void sync_script_text_input(SDL_Window* window, const EditorWorld& editor_world, bool& active) {
    const bool should_be_active = editor_world.script_editor.open;
    if (should_be_active == active) {
        return;
    }
    if (should_be_active) {
        if (!SDL_StartTextInput(window)) {
            std::cout << "Cannot start script text input: " << SDL_GetError() << '\n';
        }
    } else {
        SDL_StopTextInput(window);
    }
    active = should_be_active;
}

bool handle_script_editor_key_event(EditorWorld& editor_world, const SDL_KeyboardEvent& key_event) {
    if (!editor_world.script_editor.open) {
        return false;
    }

    const SDL_Keycode key = key_event.key;
    const SDL_Scancode scancode = key_event.scancode;
    const bool ctrl_down = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    const bool shift_down = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

    if (ctrl_down && (key == SDLK_RETURN || scancode == SDL_SCANCODE_RETURN ||
            key == SDLK_KP_ENTER || scancode == SDL_SCANCODE_KP_ENTER)) {
        script_editor_apply_current(editor_world);
        return true;
    }
    if (ctrl_down && (key == SDLK_A || scancode == SDL_SCANCODE_A)) {
        script_editor_select_all(editor_world);
        return true;
    }
    if (ctrl_down && (key == SDLK_C || scancode == SDL_SCANCODE_C)) {
        const std::string selected = script_editor_selected_text(editor_world);
        if (!selected.empty()) {
            SDL_SetClipboardText(selected.c_str());
        }
        return true;
    }
    if (ctrl_down && (key == SDLK_X || scancode == SDL_SCANCODE_X)) {
        std::string cut_text;
        if (script_editor_cut_selection(editor_world, cut_text) && !cut_text.empty()) {
            SDL_SetClipboardText(cut_text.c_str());
        }
        return true;
    }
    if (ctrl_down && (key == SDLK_V || scancode == SDL_SCANCODE_V)) {
        char* text = SDL_GetClipboardText();
        if (text != nullptr) {
            script_editor_replace_selection(editor_world, text);
            SDL_free(text);
        }
        return true;
    }
    if (ctrl_down && (key == SDLK_Z || scancode == SDL_SCANCODE_Z)) {
        if (shift_down) {
            script_editor_local_redo(editor_world);
        } else {
            script_editor_local_undo(editor_world);
        }
        return true;
    }
    if (ctrl_down && (key == SDLK_Y || scancode == SDL_SCANCODE_Y)) {
        script_editor_local_redo(editor_world);
        return true;
    }
    if (ctrl_down && (key == SDLK_S || scancode == SDL_SCANCODE_S)) {
        return false;
    }
    if (ctrl_down && (key == SDLK_O || scancode == SDL_SCANCODE_O)) {
        return false;
    }

    switch (key) {
    case SDLK_ESCAPE:
        editor_world.script_editor.open = false;
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        script_editor_newline(editor_world);
        return true;
    case SDLK_TAB:
        script_editor_insert_tab(editor_world);
        return true;
    case SDLK_BACKSPACE:
        script_editor_backspace(editor_world);
        return true;
    case SDLK_DELETE:
        script_editor_delete(editor_world);
        return true;
    case SDLK_LEFT:
        script_editor_move_caret(editor_world, ScriptEditorMove::Left, shift_down);
        return true;
    case SDLK_RIGHT:
        script_editor_move_caret(editor_world, ScriptEditorMove::Right, shift_down);
        return true;
    case SDLK_UP:
        script_editor_move_caret(editor_world, ScriptEditorMove::Up, shift_down);
        return true;
    case SDLK_DOWN:
        script_editor_move_caret(editor_world, ScriptEditorMove::Down, shift_down);
        return true;
    case SDLK_HOME:
        script_editor_move_caret(editor_world, ScriptEditorMove::Home, shift_down);
        return true;
    case SDLK_END:
        script_editor_move_caret(editor_world, ScriptEditorMove::End, shift_down);
        return true;
    case SDLK_PAGEUP:
        script_editor_move_caret(editor_world, ScriptEditorMove::PageUp, shift_down);
        return true;
    case SDLK_PAGEDOWN:
        script_editor_move_caret(editor_world, ScriptEditorMove::PageDown, shift_down);
        return true;
    default:
        break;
    }

    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return false;
    }
    return !ctrl_down;
}

EffectsMenuLayout effects_menu_layout(const int width, const int height) {
    constexpr float menu_w = 270.0F;
    constexpr float title_h = 30.0F;
    constexpr float row_h = 28.0F;
    constexpr float pad = 16.0F;
    constexpr float rows = 7.0F;
    const float menu_h = title_h + row_h * rows + 14.0F;
    return EffectsMenuLayout{
        std::max(12.0F, static_cast<float>(width) - menu_w - pad),
        std::max(12.0F, static_cast<float>(height) - menu_h - pad),
        menu_w,
        menu_h,
        0.0F,
        row_h,
    };
}

void draw_screen_rect_outline(
    const float x,
    const float y,
    const float w,
    const float h,
    const int width,
    const int height
) {
    draw_screen_line(x, y, x + w, y, width, height);
    draw_screen_line(x + w, y, x + w, y + h, width, height);
    draw_screen_line(x + w, y + h, x, y + h, width, height);
    draw_screen_line(x, y + h, x, y, width, height);
}

void set_render_effect_toggle(GameRenderConfig& config, const RenderEffectToggle effect, const bool enabled) {
    switch (effect) {
    case RenderEffectToggle::Vsm:
        config.vsm_shadows_enabled = enabled;
        std::cout << "VSM shadows " << (enabled ? "enabled" : "disabled") << '\n';
        break;
    case RenderEffectToggle::Csm:
        config.csm_shadows_enabled = enabled;
        std::cout << "CSM sun shadows " << (enabled ? "enabled" : "disabled") << '\n';
        break;
    case RenderEffectToggle::ScreenSpace:
        config.screen_space_shadows_enabled = enabled;
        std::cout << "Screen-space shadows " << (enabled ? "enabled" : "disabled") << '\n';
        break;
    case RenderEffectToggle::Fog:
        config.fog_enabled = enabled;
        std::cout << "Fog " << (enabled ? "enabled" : "disabled") << '\n';
        break;
    case RenderEffectToggle::None:
        break;
    }
}

void toggle_render_effect(GameRenderConfig& config, const RenderEffectToggle effect) {
    switch (effect) {
    case RenderEffectToggle::Vsm:
        set_render_effect_toggle(config, effect, !config.vsm_shadows_enabled);
        break;
    case RenderEffectToggle::Csm:
        set_render_effect_toggle(config, effect, !config.csm_shadows_enabled);
        break;
    case RenderEffectToggle::ScreenSpace:
        set_render_effect_toggle(config, effect, !config.screen_space_shadows_enabled);
        break;
    case RenderEffectToggle::Fog:
        set_render_effect_toggle(config, effect, !config.fog_enabled);
        break;
    case RenderEffectToggle::None:
        break;
    }
}

RenderMenuRow render_menu_row_from_index(const int row) {
    switch (row) {
    case 0:
        return RenderMenuRow::Vsm;
    case 1:
        return RenderMenuRow::Csm;
    case 2:
        return RenderMenuRow::ScreenSpace;
    case 3:
        return RenderMenuRow::Fog;
    case 4:
        return RenderMenuRow::Present;
    case 5:
        return RenderMenuRow::Cap;
    case 6:
        return RenderMenuRow::Yield;
    default:
        return RenderMenuRow::None;
    }
}

void activate_render_menu_row(
    GameRenderConfig& config,
    PresentConfig& present_config,
    SDL_Window* window,
    const RenderMenuRow row
) {
    switch (row) {
    case RenderMenuRow::Vsm:
        toggle_render_effect(config, RenderEffectToggle::Vsm);
        break;
    case RenderMenuRow::Csm:
        toggle_render_effect(config, RenderEffectToggle::Csm);
        break;
    case RenderMenuRow::ScreenSpace:
        toggle_render_effect(config, RenderEffectToggle::ScreenSpace);
        break;
    case RenderMenuRow::Fog:
        toggle_render_effect(config, RenderEffectToggle::Fog);
        break;
    case RenderMenuRow::Present:
        cycle_present_mode(window, present_config);
        break;
    case RenderMenuRow::Cap:
        cycle_fps_cap(present_config);
        break;
    case RenderMenuRow::Yield:
        present_config.yield_when_uncapped = !present_config.yield_when_uncapped;
        std::cout << "Uncapped yield " << (present_config.yield_when_uncapped ? "enabled" : "disabled") << '\n';
        break;
    case RenderMenuRow::None:
        break;
    }
}

bool handle_effects_menu_click(
    GameRenderConfig& config,
    PresentConfig& present_config,
    SDL_Window* window,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    EffectsMenuLayout layout = effects_menu_layout(width, height);
    if (!point_in_screen_rect(mouse_x, mouse_y, layout.x, layout.y, layout.w, layout.h)) {
        return false;
    }

    constexpr float title_h = 30.0F;
    const float relative_y = mouse_y - layout.y - title_h;
    if (relative_y >= 0.0F) {
        const int row = static_cast<int>(relative_y / layout.row_h);
        activate_render_menu_row(config, present_config, window, render_menu_row_from_index(row));
    }
    return true;
}

void queue_dialog_message(MapDialogRequests& requests, std::string message) {
    std::lock_guard<std::mutex> lock(requests.mutex);
    requests.messages.push_back(std::move(message));
}

void queue_dialog_message(MaterialDialogRequests& requests, std::string message) {
    std::lock_guard<std::mutex> lock(requests.mutex);
    requests.messages.push_back(std::move(message));
}

void SDLCALL save_map_dialog_callback(void* userdata, const char* const* filelist, int) {
    auto* requests = static_cast<MapDialogRequests*>(userdata);
    if (filelist == nullptr) {
        queue_dialog_message(*requests, std::string("Save dialog failed: ") + SDL_GetError());
        return;
    }

    if (filelist[0] == nullptr) {
        queue_dialog_message(*requests, "Save canceled.");
        return;
    }

    std::lock_guard<std::mutex> lock(requests->mutex);
    requests->pending_save_path = filelist[0];
}

void SDLCALL load_map_dialog_callback(void* userdata, const char* const* filelist, int) {
    auto* requests = static_cast<MapDialogRequests*>(userdata);
    if (filelist == nullptr) {
        queue_dialog_message(*requests, std::string("Load dialog failed: ") + SDL_GetError());
        return;
    }

    if (filelist[0] == nullptr) {
        queue_dialog_message(*requests, "Load canceled.");
        return;
    }

    std::lock_guard<std::mutex> lock(requests->mutex);
    requests->pending_load_path = filelist[0];
}

void SDLCALL material_texture_dialog_callback(void* userdata, const char* const* filelist, int) {
    auto* requests = static_cast<MaterialDialogRequests*>(userdata);
    if (filelist == nullptr) {
        queue_dialog_message(*requests, std::string("Texture dialog failed: ") + SDL_GetError());
        return;
    }

    if (filelist[0] == nullptr) {
        queue_dialog_message(*requests, "Texture assignment canceled.");
        return;
    }

    std::lock_guard<std::mutex> lock(requests->mutex);
    requests->pending_texture_path = filelist[0];
    requests->pending_material_id = requests->requested_material_id;
    requests->pending_channel = requests->requested_channel;
}

const SDL_DialogFileFilter* map_dialog_filters() {
    static const SDL_DialogFileFilter filters[] = {
        {"Undecedent Map", "udmap"},
    };
    return filters;
}

const SDL_DialogFileFilter* material_dialog_filters() {
    static const SDL_DialogFileFilter filters[] = {
        {"Image Texture", "png;jpg;jpeg;bmp;jxl"},
    };
    return filters;
}

bool read_binary_file(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        message = "Could not open texture file: " + path.string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        message = "Texture file is empty: " + path.string();
        return false;
    }
    return true;
}

void show_save_map_dialog(SDL_Window* window, MapDialogRequests& requests) {
    SDL_ShowSaveFileDialog(save_map_dialog_callback, &requests, window, map_dialog_filters(), 1, nullptr);
    std::cout << "Opening save dialog...\n";
}

void show_load_map_dialog(SDL_Window* window, MapDialogRequests& requests) {
    SDL_ShowOpenFileDialog(load_map_dialog_callback, &requests, window, map_dialog_filters(), 1, nullptr, false);
    std::cout << "Opening load dialog...\n";
}

void show_material_texture_dialog(
    SDL_Window* window,
    MaterialDialogRequests& requests,
    const int material_id,
    const MaterialTextureChannel channel
) {
    {
        std::lock_guard<std::mutex> lock(requests.mutex);
        requests.requested_material_id = material_id;
        requests.requested_channel = channel;
    }
    SDL_ShowOpenFileDialog(
        material_texture_dialog_callback,
        &requests,
        window,
        material_dialog_filters(),
        1,
        nullptr,
        false
    );
    std::cout << "Opening " << undecedent::material_texture_channel_label(channel)
              << " texture dialog for material " << (material_id + 1) << "...\n";
}

std::filesystem::path normalize_save_map_path(std::filesystem::path path) {
    if (path.extension().empty()) {
        path += ".udmap";
    }
    return path;
}

void print_benchmark_report(
    const BenchmarkState& benchmark,
    const BenchmarkAccumulator& accumulator,
    const PresentConfig& present_config,
    const bool editor_enabled,
    const int total_triangles,
    const int visible_triangles,
    const int sectors,
    const int walls
) {
    if (accumulator.frames <= 0 || accumulator.seconds <= 0.0) {
        return;
    }

    const double inv_frames = 1.0 / static_cast<double>(accumulator.frames);
    std::cout
        << "[benchmark] fps=" << format_fps(static_cast<double>(accumulator.frames) / accumulator.seconds)
        << " frame=" << format_ms(accumulator.frame_ms * inv_frames) << "ms"
        << " events=" << format_ms(accumulator.events_ms * inv_frames) << "ms"
        << " update=" << format_ms(accumulator.update_ms * inv_frames) << "ms"
        << " render=" << format_ms(accumulator.render_ms * inv_frames) << "ms"
        << " gbuf=" << format_ms(accumulator.gbuffer_ms * inv_frames) << "ms"
        << " light=" << format_ms(accumulator.lighting_ms * inv_frames) << "ms"
        << " shpack=" << format_ms(accumulator.shadow_pack_upload_ms * inv_frames) << "ms"
        << " shadow=" << format_ms(accumulator.shadow_ms * inv_frames) << "ms"
        << " ssshad=" << format_ms(accumulator.screen_shadow_ms * inv_frames) << "ms"
        << " wire=" << format_ms(accumulator.wire_overlay_ms * inv_frames) << "ms"
        << " overlay=" << format_ms(accumulator.overlay_ms * inv_frames) << "ms"
        << " pace=" << format_ms(accumulator.pace_ms * inv_frames) << "ms"
        << " finish=" << format_ms(accumulator.finish_ms * inv_frames) << "ms"
        << " swap=" << format_ms(accumulator.swap_ms * inv_frames) << "ms"
        << " present=" << swap_interval_label(present_config.swap_interval)
        << " swapi=" << present_config.effective_swap_interval
        << " cap=" << fps_cap_label(present_config.fps_cap)
        << " clear=" << (benchmark.skip_clear ? "skip" : "on")
        << " swap_mode=" << (benchmark.skip_swap ? "skip" : "on")
        << " mode=" << (editor_enabled ? "editor" : "game")
        << " tris=" << total_triangles << '/' << visible_triangles
        << " sectors=" << sectors
        << " walls=" << walls
        << std::endl;
}

GameControlConfig game_control_config() {
    return GameControlConfig{
        kGameMoveSpeed,
        kGameLookSpeed,
        kPlayerRadius,
        kPlayerHeight,
        kPlayerEyeHeight,
        kPlayerGravity,
        kPlayerJumpVelocity,
        kPlayerTerminalFallSpeed,
        kPlayerGravityDrag,
        kPlayerGroundProbe,
        0.0025F,
    };
}

undecedent::PlayerSpawn fallback_player_spawn_from_sectors(const std::vector<undecedent::SectorPlane>& sectors) {
    undecedent::PlayerSpawn spawn;
    for (const undecedent::SectorPlane& sector : sectors) {
        if (!sector.triangles.empty()) {
            const undecedent::Triangle triangle = sector.triangles.front();
            spawn.position = undecedent::Vec3{
                (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0F,
                sector.floor_height + kPlayerEyeHeight,
                (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0F,
            };
            spawn.yaw = 0.0F;
            spawn.set = true;
            return spawn;
        }
    }
    return spawn;
}

void apply_player_spawn_to_camera(const undecedent::PlayerSpawn& spawn, GameCamera& game_camera) {
    if (!spawn.set) {
        return;
    }
    game_camera.x = spawn.position.x;
    game_camera.y = spawn.position.y;
    game_camera.z = spawn.position.z;
    game_camera.yaw = spawn.yaw;
    game_camera.pitch = 0.0F;
}

void spawn_playtest_camera(EditorWorld& editor_world, GameCamera& game_camera) {
    undecedent::PlayerSpawn spawn = undecedent::player_spawn_from_entities(editor_world.entities);
    if (!spawn.set) {
        spawn = fallback_player_spawn_from_sectors(editor_world.sectors);
    }
    apply_player_spawn_to_camera(spawn, game_camera);
    if (!undecedent::player_fits_at(editor_world.runtime_world, spawn.position, player_physics_config(game_control_config()))) {
        std::cout << "Player spawn is outside playable space; movement may be blocked until a valid spawn is set.\n";
    }
}

void reset_playtest_player_state(
    PlaytestPlayerState& playtest_state,
    const undecedent::RuntimeWorld& world,
    const GameCamera& game_camera
) {
    playtest_state.vertical_velocity = 0.0F;
    playtest_state.jump_was_down = false;
    const undecedent::Vec3 eye{game_camera.x, game_camera.y, game_camera.z};
    playtest_state.grounded =
        undecedent::player_fits_at(world, eye, player_physics_config(game_control_config())) &&
        !undecedent::player_fits_at(
            world,
            undecedent::Vec3{eye.x, eye.y - kPlayerGroundProbe, eye.z},
            player_physics_config(game_control_config())
        );
}

bool script_store_has_scripts(const undecedent::ScriptStore& scripts) {
    return scripts.has_global_script || !scripts.entity_scripts.empty() || !scripts.sector_scripts.empty();
}

void sync_script_components(EditorWorld& editor_world) {
    for (const auto& [entity_id, program] : editor_world.scripts.entity_scripts) {
        (void)program;
        const undecedent::EntityHandle entity =
            undecedent::find_entity_by_stable_id(editor_world.entities, entity_id);
        if (undecedent::entity_alive(editor_world.entities, entity)) {
            undecedent::add_script(editor_world.entities, entity, undecedent::ScriptComponent{entity_id, true});
        }
    }
}

void reset_playtest_scripts(
    undecedent::ScriptVm& vm,
    bool& map_start_dispatched,
    bool& runtime_failed,
    std::uint64_t& current_sector_id
) {
    vm.log.clear();
    map_start_dispatched = false;
    runtime_failed = false;
    current_sector_id = 0;
}

void dispatch_playtest_scripts(
    undecedent::ScriptVm& vm,
    EditorWorld& editor_world,
    const GameCamera& game_camera,
    bool& map_start_dispatched,
    bool& runtime_failed,
    std::uint64_t& current_sector_id
) {
    if (runtime_failed || !script_store_has_scripts(editor_world.scripts)) {
        return;
    }

    const auto handle_result = [&runtime_failed](const undecedent::ScriptRunResult& result) {
        if (!result.ok) {
            std::cout << "Script runtime error: " << result.message << '\n';
            runtime_failed = true;
        }
    };

    const auto handle_results = [&handle_result, &runtime_failed](const std::vector<undecedent::ScriptRunResult>& results) {
        for (const undecedent::ScriptRunResult& result : results) {
            handle_result(result);
            if (runtime_failed) {
                return;
            }
        }
    };

    if (!map_start_dispatched) {
        handle_results(undecedent::run_script_store_event(
            vm,
            editor_world.entities,
            editor_world.scripts,
            "on_map_start"
        ));
        map_start_dispatched = true;
        if (runtime_failed) {
            return;
        }
    }

    const int runtime_sector = undecedent::sector_at_point(
        editor_world.runtime_world,
        undecedent::Vec3{game_camera.x, game_camera.y, game_camera.z}
    );
    std::uint64_t next_sector_id = 0;
    if (runtime_sector >= 0 && runtime_sector < static_cast<int>(editor_world.runtime_world.sectors.size())) {
        next_sector_id = editor_world.runtime_world.sectors[static_cast<std::size_t>(runtime_sector)].source_sector_id;
    }

    if (current_sector_id != next_sector_id) {
        if (current_sector_id != 0) {
            handle_result(undecedent::run_sector_script_event(
                vm,
                editor_world.entities,
                editor_world.scripts,
                current_sector_id,
                "on_sector_exit"
            ));
            if (runtime_failed) {
                return;
            }
        }
        current_sector_id = next_sector_id;
        if (current_sector_id != 0) {
            handle_result(undecedent::run_sector_script_event(
                vm,
                editor_world.entities,
                editor_world.scripts,
                current_sector_id,
                "on_sector_enter"
            ));
            if (runtime_failed) {
                return;
            }
        }
    }

    if (current_sector_id != 0) {
        handle_result(undecedent::run_sector_script_event(
            vm,
            editor_world.entities,
            editor_world.scripts,
            current_sector_id,
            "on_sector_stay"
        ));
        if (runtime_failed) {
            return;
        }
    }

    handle_results(undecedent::run_script_store_event(
        vm,
        editor_world.entities,
        editor_world.scripts,
        "on_tick"
    ));

    if (!vm.log.empty()) {
        for (const std::string& line : vm.log) {
            std::cout << "Script: " << line << '\n';
        }
        vm.log.clear();
    }
}

void process_pending_map_dialogs(
    EditorWorld& editor_world,
    MaterialTextureArray& material_textures,
    GameCamera& game_camera,
    PlaytestPlayerState& playtest_state,
    undecedent::ScriptVm& script_vm,
    bool& script_map_start_dispatched,
    bool& script_runtime_failed,
    std::uint64_t& script_current_sector_id,
    const bool editor_enabled,
    MapDialogRequests& requests
) {
    std::string save_path_string;
    std::string load_path_string;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(requests.mutex);
        save_path_string = std::move(requests.pending_save_path);
        load_path_string = std::move(requests.pending_load_path);
        messages = std::move(requests.messages);
        requests.pending_save_path.clear();
        requests.pending_load_path.clear();
        requests.messages.clear();
    }

    for (const std::string& message : messages) {
        std::cout << message << '\n';
    }

    if (!save_path_string.empty()) {
        const std::filesystem::path save_path = normalize_save_map_path(save_path_string);
        undecedent::ensure_editor_stable_ids(editor_world);
        const undecedent::SaveMapResult result =
            undecedent::save_map_file_dirty(
                editor_world.sectors,
                undecedent::player_spawn_from_entities(editor_world.entities),
                undecedent::point_lights_from_entities(editor_world.entities),
                editor_world.world_lighting,
                editor_world.material_library,
                editor_world.scripts,
                undecedent::editor_map_dirty_state(editor_world),
                save_path
            );
        if (result.ok) {
            undecedent::clear_map_dirty_state(editor_world);
            undecedent::script_editor_clear_clean_drafts(editor_world);
        }
        std::cout << result.message << '\n';
    }

    if (!load_path_string.empty()) {
        const undecedent::LoadMapResult result = undecedent::load_map_file(load_path_string);
        if (!result.ok) {
            std::cout << "Cannot load map: " << result.message << '\n';
            return;
        }

        finish_committed_vertex_drag(editor_world);
        cancel_plane_tool(editor_world);
        push_undo_snapshot(editor_world, "load map");
        editor_world.sectors = result.sectors;
        editor_world.entities = undecedent::entity_registry_from_authored_entities(
            result.player_spawn,
            result.point_lights
        );
        editor_world.world_lighting = result.world_lighting;
        editor_world.material_library = result.material_library;
        undecedent::mark_material_textures_dirty(material_textures);
        editor_world.scripts = result.scripts;
        editor_world.script_editor = {};
        sync_script_components(editor_world);
        clear_sector_selection(editor_world);
        clear_entity_selection(editor_world);
        rebuild_runtime_geometry(editor_world);
        undecedent::clear_map_dirty_state(editor_world);
        if (!editor_enabled) {
            reset_playtest_scripts(
                script_vm,
                script_map_start_dispatched,
                script_runtime_failed,
                script_current_sector_id
            );
            spawn_playtest_camera(editor_world, game_camera);
            reset_playtest_player_state(playtest_state, editor_world.runtime_world, game_camera);
        }
        std::cout << result.message << " Sectors: " << editor_world.sectors.size() << '\n';
    }
}

void process_pending_material_dialogs(
    EditorWorld& editor_world,
    MaterialTextureArray& material_textures,
    MaterialDialogRequests& requests
) {
    std::string texture_path_string;
    int material_id = -1;
    MaterialTextureChannel channel = MaterialTextureChannel::Albedo;
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(requests.mutex);
        texture_path_string = std::move(requests.pending_texture_path);
        material_id = requests.pending_material_id;
        channel = requests.pending_channel;
        messages = std::move(requests.messages);
        requests.pending_texture_path.clear();
        requests.pending_material_id = -1;
        requests.pending_channel = MaterialTextureChannel::Albedo;
        requests.messages.clear();
    }

    for (const std::string& message : messages) {
        std::cout << message << '\n';
    }

    if (texture_path_string.empty() || material_id < 0) {
        return;
    }

    const std::filesystem::path texture_path = texture_path_string;
    std::vector<std::uint8_t> texture_bytes;
    std::string read_message;
    if (!read_binary_file(texture_path, texture_bytes, read_message)) {
        std::cout << read_message << '\n';
        return;
    }

    push_undo_snapshot(editor_world, "assign material texture");
    set_material_texture(
        editor_world.material_library,
        material_id,
        channel,
        texture_path,
        texture_path.filename().generic_string(),
        std::move(texture_bytes)
    );
    editor_world.dirty_materials = true;
    undecedent::mark_material_textures_dirty(material_textures);
    std::cout << "Assigned " << undecedent::material_texture_channel_label(channel)
              << " texture to material " << (material_id + 1) << ": " << texture_path_string << '\n';
}

void draw_overlay_text(
    const std::string& label,
    const float x,
    const float y,
    const float stroke_size,
    const int width,
    const int height,
    const float alpha = 0.92F
) {
    if (draw_sdf_text(
            label,
            x,
            y - 1.0F,
            stroke_size * 2.2F,
            width,
            height,
            undecedent::SdfTextColor{1.0F, 1.0F, 1.0F, alpha}
        )) {
        return;
    }
    core_begin(GL_LINES);
    core_color4f(1.0F, 1.0F, 1.0F, alpha);
    draw_stroke_text(label, x, y, stroke_size, width, height);
    core_end();
}

void draw_fps_counter(const int fps, const int width, const int height) {
    const std::string label = "FPS " + std::to_string(std::max(0, fps));
    const float size = 8.0F;
    const float x = 16.0F;
    const float y = 16.0F;
    const float box_width = 18.0F + static_cast<float>(label.size()) * size * 1.45F;
    const float box_height = 30.0F;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    core_begin(kCoreQuads);
    core_color4f(0.0F, 0.0F, 0.0F, 0.38F);
    draw_screen_quad(10.0F, 10.0F, box_width, box_height, width, height);
    core_end();

    core_set_line_width(1.5F);
    draw_overlay_text(label, x, y, size, width, height);

    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_material_texture_controls(
    const EditorWorld& editor_world,
    const int active_material,
    const MaterialTextureChannel active_channel,
    const int width,
    const int height
) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const MaterialTextureControlsLayout layout = material_texture_controls_layout(height);
    const auto draw_button = [&](const float x, const float y, const float w, const std::string& label, const bool active) {
        core_begin(kCoreQuads);
        core_color4f(active ? 0.18F : 0.0F, active ? 0.40F : 0.0F, active ? 0.34F : 0.0F, active ? 0.78F : 0.44F);
        draw_screen_quad(x, y, w, layout.button_h, width, height);
        core_end();
        core_begin(GL_LINES);
        core_color4f(0.90F, 0.96F, 0.76F, 0.90F);
        draw_screen_rect_outline(x, y, w, layout.button_h, width, height);
        core_end();
        draw_overlay_text(label, x + 9.0F, y + 6.0F, 4.6F, width, height);
    };

    const int slot_id = undecedent::clamped_material_id(active_material);
    const undecedent::MaterialSlot& slot =
        editor_world.material_library.slots[static_cast<std::size_t>(slot_id)];
    const undecedent::MaterialTextureSource& source =
        undecedent::material_texture_source(slot, active_channel);
    const bool has_texture =
        undecedent::material_texture_source_has_texture(source);
    const bool compressed_mode =
        source.storage_mode != undecedent::MaterialTextureStorageMode::SourceBytes;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_set_line_width(1.25F);
    for (int channel_index = 0; channel_index < undecedent::kMaterialTextureChannelCount; ++channel_index) {
        const auto channel = static_cast<undecedent::MaterialTextureChannel>(channel_index);
        const bool channel_active = channel == active_channel;
        const bool channel_has_texture =
            undecedent::material_texture_source_has_texture(undecedent::material_texture_source(slot, channel));
        const float x = layout.x + (static_cast<float>(channel_index) * (layout.channel_w + layout.gap));
        std::string label = undecedent::material_texture_channel_short_label(channel);
        if (channel_has_texture) {
            label += "*";
        }
        draw_button(x, layout.channel_y, layout.channel_w, label, channel_active);
    }
    draw_button(layout.x, layout.y, layout.button_w, has_texture ? "TEXTURE*" : "TEXTURE", has_texture);
    draw_button(layout.x + layout.button_w + layout.gap, layout.y, layout.button_w, "CLEAR", false);
    draw_button(
        layout.x + ((layout.button_w + layout.gap) * 2.0F),
        layout.y,
        layout.button_w,
        material_texture_storage_mode_label(source.storage_mode),
        compressed_mode
    );
    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_profiler_overlay(
    const ProfilerDisplay& profiler,
    const int width,
    const int height,
    const float top_y
) {
    const float size = 6.0F;
    const float x = 16.0F;
    const float line_height = 16.0F;
    const std::vector<std::string> lines{
        "FPS " + std::to_string(std::max(0, profiler.fps)),
        "FRAME " + format_ms(profiler.frame_ms) + "MS",
        "EVENTS " + format_ms(profiler.events_ms) + "MS",
        "UPDATE " + format_ms(profiler.update_ms) + "MS",
        "RENDER " + format_ms(profiler.render_ms) + "MS",
        "GBUF " + format_ms(profiler.gbuffer_ms) + "MS",
        "LIGHT " + format_ms(profiler.lighting_ms) + "MS",
        "SHPACK " + format_ms(profiler.shadow_pack_upload_ms) + "MS",
        "SHADOW " + format_ms(profiler.shadow_ms) + "MS",
        "SSSHAD " + format_ms(profiler.screen_shadow_ms) + "MS",
        "WIRE " + format_ms(profiler.wire_overlay_ms) + "MS",
        "OVERLAY " + format_ms(profiler.overlay_ms) + "MS",
        "PACE " + format_ms(profiler.pace_ms) + "MS",
        "FINISH " + format_ms(profiler.finish_ms) + "MS",
        "SWAP " + format_ms(profiler.swap_ms) + "MS",
        "PRESENT " + std::string(swap_interval_label(profiler.effective_swap_interval)) + " CAP " +
            fps_cap_label(profiler.fps_cap),
        "SWAPI " + std::to_string(profiler.effective_swap_interval),
        "SHC " + std::to_string(profiler.shadow_cache_hits) + "/" + std::to_string(profiler.shadow_cache_misses),
        "SHDRAW P" + std::to_string(profiler.point_shadow_lights) +
            " F" + std::to_string(profiler.point_shadow_faces) +
            " C" + std::to_string(profiler.sun_shadow_cascades),
        "TRIS " + std::to_string(profiler.total_triangles) + "/" + std::to_string(profiler.visible_triangles),
        "SECT " + std::to_string(profiler.sectors) + " WALL " + std::to_string(profiler.walls),
    };

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    core_begin(kCoreQuads);
    core_color4f(0.0F, 0.0F, 0.0F, 0.42F);
    draw_screen_quad(10.0F, top_y - 6.0F, 286.0F, 12.0F + line_height * static_cast<float>(lines.size()), width, height);
    core_end();

    core_set_line_width(1.35F);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        draw_overlay_text(lines[i], x, top_y + static_cast<float>(i) * line_height, size, width, height);
    }

    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_effects_menu(const GameRenderConfig& config, const PresentConfig& present_config, const int width, const int height) {
    const EffectsMenuLayout layout = effects_menu_layout(width, height);
    struct Row {
        const char* label;
        bool enabled;
        std::string value;
    };
    const Row rows[] = {
        {"VSM", config.vsm_shadows_enabled, config.vsm_shadows_enabled ? "ON" : "OFF"},
        {"CSM", config.csm_shadows_enabled, config.csm_shadows_enabled ? "ON" : "OFF"},
        {"SSS", config.screen_space_shadows_enabled, config.screen_space_shadows_enabled ? "ON" : "OFF"},
        {"FOG", config.fog_enabled, config.fog_enabled ? "ON" : "OFF"},
        {"PRESENT", present_config.effective_swap_interval != 0, swap_interval_label(present_config.swap_interval)},
        {"CAP", present_config.fps_cap > 0, fps_cap_label(present_config.fps_cap)},
        {"YIELD", present_config.yield_when_uncapped, present_config.yield_when_uncapped ? "ON" : "OFF"},
    };

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    core_begin(kCoreQuads);
    core_color4f(0.0F, 0.0F, 0.0F, 0.58F);
    draw_screen_quad(layout.x, layout.y, layout.w, layout.h, width, height);
    core_end();

    core_set_line_width(1.5F);
    core_begin(GL_LINES);
    core_color4f(0.90F, 0.96F, 0.76F, 0.96F);
    draw_screen_rect_outline(layout.x, layout.y, layout.w, layout.h, width, height);
    core_end();
    draw_overlay_text("RENDER", layout.x + 10.0F, layout.y + 9.0F, 5.5F, width, height, 0.96F);

    constexpr float title_h = 30.0F;
    for (int i = 0; i < 7; ++i) {
        const float row_y = layout.y + title_h + static_cast<float>(i) * layout.row_h;
        const float toggle_x = layout.x + layout.w - 74.0F;
        const float toggle_y = row_y + 6.0F;

        core_begin(kCoreQuads);
        core_color4f(0.04F, 0.08F, 0.07F, rows[i].enabled ? 0.76F : 0.35F);
        draw_screen_quad(layout.x + 6.0F, row_y + 2.0F, layout.w - 12.0F, layout.row_h - 4.0F, width, height);
        if (rows[i].enabled) {
            core_color4f(0.45F, 0.70F, 0.55F, 0.70F);
            draw_screen_quad(toggle_x, toggle_y, 60.0F, 16.0F, width, height);
        }
        core_end();

        core_begin(GL_LINES);
        core_color4f(0.90F, 0.96F, 0.76F, 0.95F);
        draw_screen_rect_outline(toggle_x, toggle_y, 60.0F, 16.0F, width, height);
        core_end();
        draw_overlay_text(rows[i].label, layout.x + 14.0F, row_y + 9.0F, 5.5F, width, height, 0.95F);
        draw_overlay_text(rows[i].value, toggle_x + 6.0F, row_y + 10.0F, 4.8F, width, height, 0.95F);
    }

    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void frame_game_camera_on_sectors(GameCamera& camera, const std::vector<undecedent::SectorPlane>& sectors) {
    if (sectors.empty()) {
        return;
    }

    bool initialized = false;
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
    float max_height = 96.0F;
    float max_ceiling = 96.0F;

    const auto include_point = [&](const undecedent::Vec2 point) {
        if (!initialized) {
            min_x = max_x = point.x;
            min_z = max_z = point.y;
            initialized = true;
            return;
        }

        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_z = std::min(min_z, point.y);
        max_z = std::max(max_z, point.y);
    };

    for (const undecedent::SectorPlane& sector : sectors) {
        max_height = std::max(max_height, sector.height);
        max_ceiling = std::max(max_ceiling, sector.floor_height + sector.height);
        for (const undecedent::Vec2 vertex : sector.outer.vertices) {
            include_point(vertex);
        }
        for (const undecedent::PolygonLoop& hole : sector.holes) {
            for (const undecedent::Vec2 vertex : hole.vertices) {
                include_point(vertex);
            }
        }
    }

    if (!initialized) {
        return;
    }

    const float center_x = (min_x + max_x) * 0.5F;
    const float center_z = (min_z + max_z) * 0.5F;
    const float extent = std::max(max_x - min_x, max_z - min_z);
    const float distance = std::max(160.0F, extent * 1.6F);

    camera.x = center_x;
    camera.y = std::max(48.0F, std::max(max_height * 0.65F, max_ceiling * 0.65F));
    camera.z = center_z + distance;
    camera.yaw = 0.0F;
    camera.pitch = 0.0F;
}

const char* app_mode_name(const AppMode mode) {
    switch (mode) {
    case AppMode::Editor3D: return "3D editor";
    case AppMode::Editor2D: return "2D editor";
    case AppMode::Playtest: return "playtest";
    }
    return "unknown";
}

bool is_editor_mode(const AppMode mode) {
    return mode == AppMode::Editor3D || mode == AppMode::Editor2D;
}

bool is_2d_editor_mode(const AppMode mode) {
    return mode == AppMode::Editor2D;
}

void set_app_mode(SDL_Window* window, const AppMode mode) {
    switch (mode) {
    case AppMode::Editor3D:
        SDL_SetWindowTitle(window, "Undecedent - 3D Editor");
        break;
    case AppMode::Editor2D:
        SDL_SetWindowTitle(window, "Undecedent - 2D Editor");
        break;
    case AppMode::Playtest:
        SDL_SetWindowTitle(window, "Undecedent - Playtest");
        break;
    }

    if (mode == AppMode::Playtest) {
        SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, true);
        if (!SDL_SetWindowRelativeMouseMode(window, true)) {
            std::cout << "Cannot enable relative mouse mode: " << SDL_GetError() << '\n';
        }
    } else {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, mode == AppMode::Editor2D || mode == AppMode::Editor3D);
    }
    if (mode != AppMode::Editor2D && mode != AppMode::Editor3D && mode != AppMode::Playtest) {
        SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    }
    std::cout << "Mode: " << app_mode_name(mode) << '\n';
}
} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("SDL_Init failed");
        return EXIT_FAILURE;
    }

    if (!configure_gl_attributes()) {
        log_sdl_error("OpenGL attribute setup failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Undecedent",
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr) {
        log_sdl_error("SDL_CreateWindow failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        log_sdl_error("SDL_GL_CreateContext failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "gladLoadGLLoader failed\n";
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);
    SDL_SetWindowRelativeMouseMode(window, false);

    bool running = true;
    AppMode app_mode = AppMode::Editor3D;
    AppMode previous_editor_mode = AppMode::Editor3D;
    bool fps_counter_enabled = false;
    bool profiler_enabled = false;
    bool profiler_finish_diagnostic_enabled = false;
    bool runtime_wire_overlay_enabled = false;
    bool effects_menu_open = false;
    BenchmarkState benchmark{};
    float fps_counter_seconds = 0.0F;
    int fps_counter_frames = 0;
    int displayed_fps = 0;
    ProfilerAccumulator profiler_accumulator{};
    BenchmarkAccumulator benchmark_accumulator{};
    ProfilerDisplay displayed_profiler{};
    undecedent::FullscreenState fullscreen_state{};
    MapDialogRequests map_dialog_requests{};
    MaterialDialogRequests material_dialog_requests{};
    EditorCamera editor_camera{};
    GameCamera game_camera{};
    PlaytestPlayerState playtest_state{};
    EditorWorld editor_world{};
    undecedent::ScriptVm script_vm{};
    bool script_map_start_dispatched = false;
    bool script_runtime_failed = false;
    std::uint64_t script_current_sector_id = 0;
    bool script_text_input_active = false;
    const Editor2DRenderConfig editor_2d_render_config{
        kEditorMajorGridEvery,
        kEditorMinZoom,
        kScaleIndicatorTargetPixels,
        kScaleIndicatorMinPixels,
        kScaleIndicatorMaxPixels,
        kPlayerEyeHeight,
    };
    GameRenderConfig game_render_config{
        kGameNearPlane,
        kGameFarPlane,
        70.0F,
        kPlayerEyeHeight,
        kPlayerHeight,
        kPlayerRadius,
    };
    PresentConfig present_config{};
    apply_swap_interval(window, present_config);
    undecedent::DeferredRenderer deferred_renderer{};
    MaterialTextureArray material_textures{};
    int active_material = undecedent::kDefaultMaterialId;
    MaterialTextureChannel active_material_channel = MaterialTextureChannel::Albedo;
    set_app_mode(window, app_mode);
    Uint64 previous_ticks = SDL_GetTicksNS();
    while (running) {
        const Uint64 current_ticks = SDL_GetTicksNS();
        const Uint64 frame_start_ticks = current_ticks;
        const float dt = std::min(
            static_cast<float>(current_ticks - previous_ticks) / 1'000'000'000.0F,
            0.1F
        );
        previous_ticks = current_ticks;
        fps_counter_seconds += dt;
        ++fps_counter_frames;
        if (fps_counter_seconds >= kFpsCounterUpdateSeconds) {
            displayed_fps = static_cast<int>(std::round(static_cast<float>(fps_counter_frames) / fps_counter_seconds));
            fps_counter_seconds = 0.0F;
            fps_counter_frames = 0;
        }

        double events_ms = 0.0;
        double update_ms = 0.0;
        double render_ms = 0.0;
        double gbuffer_ms = 0.0;
        double shadow_pack_upload_ms = 0.0;
        double shadow_ms = 0.0;
        double screen_shadow_ms = 0.0;
        double lighting_ms = 0.0;
        double wire_overlay_ms = 0.0;
        double overlay_ms = 0.0;
        double pace_ms = 0.0;
        double finish_ms = 0.0;
        double swap_ms = 0.0;
        int visible_triangle_count = 0;
        int shadow_cache_hits = 0;
        int shadow_cache_misses = 0;
        int point_shadow_lights = 0;
        int point_shadow_faces = 0;
        int sun_shadow_cascades = 0;

        const Uint64 events_start_ticks = SDL_GetTicksNS();
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (event.type == SDL_EVENT_TEXT_INPUT && editor_world.script_editor.open && is_editor_mode(app_mode)) {
                if (event.text.text != nullptr) {
                    script_editor_insert_text(editor_world, event.text.text);
                }
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                const SDL_Scancode scancode = event.key.scancode;
                const bool ctrl_down = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                const bool shift_down = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                const bool editor_2d = is_2d_editor_mode(app_mode);
                const bool editor_3d = app_mode == AppMode::Editor3D;

                if ((key == SDLK_F12 || scancode == SDL_SCANCODE_F12) && !event.key.repeat && is_editor_mode(app_mode)) {
                    undecedent::toggle_script_editor(editor_world);
                    continue;
                }

                if (is_editor_mode(app_mode) && handle_script_editor_key_event(editor_world, event.key)) {
                    continue;
                }

                if (ctrl_down && (key == SDLK_Z || scancode == SDL_SCANCODE_Z) && !event.key.repeat && is_editor_mode(app_mode)) {
                    if (shift_down) {
                        if (redo_editor_action(editor_world)) {
                            undecedent::mark_material_textures_dirty(material_textures);
                        }
                    } else if (undo_editor_action(editor_world)) {
                        undecedent::mark_material_textures_dirty(material_textures);
                    }
                    continue;
                }

                if (ctrl_down && (key == SDLK_Y || scancode == SDL_SCANCODE_Y) && !event.key.repeat && is_editor_mode(app_mode)) {
                    if (redo_editor_action(editor_world)) {
                        undecedent::mark_material_textures_dirty(material_textures);
                    }
                    continue;
                }

                if (ctrl_down && (key == SDLK_S || scancode == SDL_SCANCODE_S) && !event.key.repeat) {
                    if (!script_editor_apply_dirty_before_save(editor_world)) {
                        std::cout << "Save blocked by script compile error.\n";
                        continue;
                    }
                    show_save_map_dialog(window, map_dialog_requests);
                    continue;
                }

                if (ctrl_down && (key == SDLK_O || scancode == SDL_SCANCODE_O) && !event.key.repeat) {
                    show_load_map_dialog(window, map_dialog_requests);
                    continue;
                }

                if (editor_2d && key == SDLK_P && !event.key.repeat) {
                    start_outer_plane(editor_world);
                    continue;
                }

                if (editor_2d && key == SDLK_H && !event.key.repeat) {
                    start_hole_plane(editor_world);
                    continue;
                }

                if (editor_2d && key == SDLK_K && !event.key.repeat) {
                    start_knife_tool(editor_world);
                    continue;
                }

                if (editor_2d && key == SDLK_M && !event.key.repeat &&
                    editor_world.plane_tool == PlaneToolMode::None) {
                    merge_selected_sectors(editor_world);
                    continue;
                }

                if (editor_2d && (key == SDLK_DELETE || scancode == SDL_SCANCODE_DELETE) && !event.key.repeat) {
                    delete_selected_sectors(editor_world);
                    continue;
                }

                if (editor_2d && editor_world.plane_tool == PlaneToolMode::None &&
                    (key == SDLK_KP_PLUS || scancode == SDL_SCANCODE_KP_PLUS) && !event.key.repeat) {
                    if (shift_down) {
                        adjust_selected_sector_floor_heights(editor_world, kSectorHeightStep);
                    } else {
                        adjust_selected_sector_heights(editor_world, kSectorHeightStep);
                    }
                    continue;
                }

                if (editor_2d && editor_world.plane_tool == PlaneToolMode::None &&
                    (key == SDLK_KP_MINUS || scancode == SDL_SCANCODE_KP_MINUS) && !event.key.repeat) {
                    if (shift_down) {
                        adjust_selected_sector_floor_heights(editor_world, -kSectorHeightStep);
                    } else {
                        adjust_selected_sector_heights(editor_world, -kSectorHeightStep);
                    }
                    continue;
                }

                if (editor_2d && key == SDLK_RETURN && editor_world.plane_tool != PlaneToolMode::None) {
                    commit_plane_tool(editor_world);
                    continue;
                }

                if (editor_2d && key == SDLK_BACKSPACE && editor_world.plane_tool != PlaneToolMode::None) {
                    if (!editor_world.draft_vertices.empty()) {
                        editor_world.draft_vertices.pop_back();
                        refresh_draft(editor_world);
                    }
                    continue;
                }

                if (editor_2d && key == SDLK_ESCAPE && editor_world.plane_tool != PlaneToolMode::None) {
                    cancel_plane_tool(editor_world);
                    continue;
                }

                if (editor_3d && !event.key.repeat && !effects_menu_open) {
                    int material_key = -1;
                    if (key >= SDLK_1 && key <= SDLK_8) {
                        material_key = static_cast<int>(key - SDLK_1);
                    } else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_8) {
                        material_key = static_cast<int>(scancode - SDL_SCANCODE_1);
                    }
                    if (material_key >= 0) {
                        active_material = material_key;
                        std::cout << "Selected material " << (active_material + 1) << '\n';
                        continue;
                    }
                }

                if (editor_3d && !event.key.repeat && editor_world.displacement_sculpt_enabled) {
                    if (key == SDLK_COMMA || scancode == SDL_SCANCODE_COMMA) {
                        adjust_displacement_brush_radius(editor_world, -8.0F);
                        continue;
                    }
                    if (key == SDLK_PERIOD || scancode == SDL_SCANCODE_PERIOD) {
                        adjust_displacement_brush_radius(editor_world, 8.0F);
                        continue;
                    }
                }

                if (app_mode == AppMode::Playtest && key == SDLK_ESCAPE && !event.key.repeat) {
                    app_mode = previous_editor_mode;
                    set_app_mode(window, app_mode);
                    continue;
                }

                if (key == SDLK_Q || (key == SDLK_ESCAPE && app_mode != AppMode::Playtest)) {
                    running = false;
                }

                if ((key == SDLK_F2 || scancode == SDL_SCANCODE_F2) && !event.key.repeat && is_editor_mode(app_mode)) {
                    if (editor_2d) {
                        place_entity_at_origin(
                            editor_world,
                            undecedent::Vec3{editor_world.snapped_mouse.x, editor_world.slice_z, editor_world.snapped_mouse.y},
                            0.0F,
                            game_render_config.player_eye_height
                        );
                    } else {
                        place_entity(
                            editor_world,
                            undecedent::Vec3{game_camera.x, game_camera.y, game_camera.z},
                            game_camera.yaw
                        );
                    }
                    continue;
                }

                if (key == SDLK_F1 && !event.key.repeat) {
                    editor_camera.panning = false;
                    if (app_mode == AppMode::Editor2D) {
                        finish_committed_vertex_drag(editor_world);
                        cancel_plane_tool(editor_world);
                        app_mode = AppMode::Editor3D;
                    } else if (app_mode == AppMode::Editor3D) {
                        app_mode = AppMode::Editor2D;
                    }
                    if (is_editor_mode(app_mode)) {
                        previous_editor_mode = app_mode;
                    }
                    set_app_mode(window, app_mode);
                }

                if (key == SDLK_F3 && !event.key.repeat) {
                    fps_counter_enabled = !fps_counter_enabled;
                    std::cout << "FPS counter " << (fps_counter_enabled ? "enabled" : "disabled") << '\n';
                }

                if (key == SDLK_F4 && !event.key.repeat) {
                    profiler_enabled = !profiler_enabled;
                    std::cout << "Profiler " << (profiler_enabled ? "enabled" : "disabled") << '\n';
                }

                if ((key == SDLK_F5 || scancode == SDL_SCANCODE_F5) && !event.key.repeat) {
                    effects_menu_open = !effects_menu_open;
                    std::cout << "Render effects menu " << (effects_menu_open ? "opened" : "closed") << '\n';
                    continue;
                }

                if (effects_menu_open && !event.key.repeat) {
                    int effect_key = -1;
                    if (key >= SDLK_1 && key <= SDLK_7) {
                        effect_key = static_cast<int>(key - SDLK_1);
                    } else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_7) {
                        effect_key = static_cast<int>(scancode - SDL_SCANCODE_1);
                    }
                    if (effect_key >= 0) {
                        activate_render_menu_row(
                            game_render_config,
                            present_config,
                            window,
                            render_menu_row_from_index(effect_key)
                        );
                        continue;
                    }
                }

                if ((key == SDLK_F7 || scancode == SDL_SCANCODE_F7) && !event.key.repeat) {
                    profiler_finish_diagnostic_enabled = !profiler_finish_diagnostic_enabled;
                    std::cout << "Profiler GL finish diagnostic "
                              << (profiler_finish_diagnostic_enabled ? "enabled" : "disabled") << '\n';
                }

                if ((key == SDLK_F8 || scancode == SDL_SCANCODE_F8) && !event.key.repeat) {
                    benchmark.enabled = !benchmark.enabled;
                    benchmark_accumulator = {};
                    if (benchmark.enabled) {
                        profiler_finish_diagnostic_enabled = false;
                    }
                    std::cout << "Benchmark mode " << (benchmark.enabled ? "enabled" : "disabled")
                              << " (Shift+F9 clear " << (benchmark.skip_clear ? "skip" : "on")
                              << ", F10 swap " << (benchmark.skip_swap ? "skip" : "on") << ")\n"
                              << std::flush;
                }

                if ((key == SDLK_F9 || scancode == SDL_SCANCODE_F9) && !event.key.repeat && !shift_down) {
                    if (app_mode == AppMode::Playtest) {
                        app_mode = previous_editor_mode;
                    } else {
                        if (is_editor_mode(app_mode)) {
                            previous_editor_mode = app_mode;
                        }
                        if (!script_editor_apply_dirty_before_save(editor_world)) {
                            std::cout << "Playtest blocked by script compile error.\n";
                            continue;
                        }
                        editor_world.script_editor.open = false;
                        finish_committed_vertex_drag(editor_world);
                        cancel_plane_tool(editor_world);
                        sync_script_components(editor_world);
                        reset_playtest_scripts(
                            script_vm,
                            script_map_start_dispatched,
                            script_runtime_failed,
                            script_current_sector_id
                        );
                        spawn_playtest_camera(editor_world, game_camera);
                        reset_playtest_player_state(playtest_state, editor_world.runtime_world, game_camera);
                        app_mode = AppMode::Playtest;
                    }
                    set_app_mode(window, app_mode);
                    continue;
                }

                if ((key == SDLK_F9 || scancode == SDL_SCANCODE_F9) && !event.key.repeat && shift_down) {
                    benchmark.skip_clear = !benchmark.skip_clear;
                    benchmark_accumulator = {};
                    std::cout << "Benchmark clear " << (benchmark.skip_clear ? "skipped" : "enabled") << '\n'
                              << std::flush;
                }

                if ((key == SDLK_F10 || scancode == SDL_SCANCODE_F10) && !event.key.repeat) {
                    benchmark.skip_swap = !benchmark.skip_swap;
                    benchmark_accumulator = {};
                    std::cout << "Benchmark swap " << (benchmark.skip_swap ? "skipped" : "enabled") << '\n'
                              << std::flush;
                }

                if (key == SDLK_F6 && !event.key.repeat) {
                    runtime_wire_overlay_enabled = !runtime_wire_overlay_enabled;
                    std::cout << "Runtime wire overlay "
                              << (runtime_wire_overlay_enabled ? "enabled" : "disabled") << '\n';
                }

                if ((key == SDLK_F11 || event.key.scancode == SDL_SCANCODE_F11) && !event.key.repeat) {
                    toggle_exclusive_fullscreen(window, fullscreen_state);
                    apply_swap_interval(window, present_config);
                }
            }

            if (editor_world.script_editor.open && is_editor_mode(app_mode) &&
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                handle_script_editor_mouse_click(editor_world, width, height, event.button.x, event.button.y);
                continue;
            }

            if (editor_world.script_editor.open && is_editor_mode(app_mode) &&
                event.type == SDL_EVENT_MOUSE_WHEEL) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                float scroll_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    scroll_y *= -1.0F;
                }
                handle_script_editor_mouse_wheel(
                    editor_world,
                    width,
                    height,
                    event.wheel.mouse_x,
                    event.wheel.mouse_y,
                    scroll_y
                );
                continue;
            }

            if (effects_menu_open && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                const EffectsMenuLayout layout = effects_menu_layout(width, height);
                if (point_in_screen_rect(
                        event.button.x,
                        event.button.y,
                        layout.x,
                        layout.y,
                        layout.w,
                        layout.h
                    )) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        handle_effects_menu_click(
                            game_render_config,
                            present_config,
                            window,
                            width,
                            height,
                            event.button.x,
                            event.button.y
                        );
                    }
                    continue;
                }
            }

            if (app_mode == AppMode::Playtest && event.type == SDL_EVENT_MOUSE_MOTION) {
                update_game_camera_mouse_look(
                    game_camera,
                    event.motion.xrel,
                    event.motion.yrel,
                    game_control_config()
                );
                continue;
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                if (effects_menu_open &&
                    handle_effects_menu_click(
                        game_render_config,
                        present_config,
                        window,
                        width,
                        height,
                        event.button.x,
                        event.button.y
                    )) {
                    continue;
                }
                if (handle_subdivision_controls_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_sculpt_button_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                const MaterialTextureControlAction material_action =
                    material_texture_control_at(height, event.button.x, event.button.y);
                if (material_action == MaterialTextureControlAction::SelectAlbedo ||
                    material_action == MaterialTextureControlAction::SelectNormal ||
                    material_action == MaterialTextureControlAction::SelectSmoothness ||
                    material_action == MaterialTextureControlAction::SelectAo ||
                    material_action == MaterialTextureControlAction::SelectMetallic) {
                    switch (material_action) {
                    case MaterialTextureControlAction::SelectNormal:
                        active_material_channel = MaterialTextureChannel::Normal;
                        break;
                    case MaterialTextureControlAction::SelectSmoothness:
                        active_material_channel = MaterialTextureChannel::Smoothness;
                        break;
                    case MaterialTextureControlAction::SelectAo:
                        active_material_channel = MaterialTextureChannel::AmbientOcclusion;
                        break;
                    case MaterialTextureControlAction::SelectMetallic:
                        active_material_channel = MaterialTextureChannel::Metallic;
                        break;
                    case MaterialTextureControlAction::SelectAlbedo:
                    default:
                        active_material_channel = MaterialTextureChannel::Albedo;
                        break;
                    }
                    std::cout << "Selected " << undecedent::material_texture_channel_label(active_material_channel)
                              << " texture channel\n";
                    continue;
                }
                if (material_action == MaterialTextureControlAction::Assign) {
                    show_material_texture_dialog(window, material_dialog_requests, active_material, active_material_channel);
                    continue;
                }
                if (material_action == MaterialTextureControlAction::Clear) {
                    push_undo_snapshot(editor_world, "clear material texture");
                    clear_material_texture_path(editor_world.material_library, active_material, active_material_channel);
                    editor_world.dirty_materials = true;
                    undecedent::mark_material_textures_dirty(material_textures);
                    std::cout << "Cleared " << undecedent::material_texture_channel_label(active_material_channel)
                              << " texture for material " << (active_material + 1) << '\n';
                    continue;
                }
                if (material_action == MaterialTextureControlAction::CycleStorage) {
                    push_undo_snapshot(editor_world, "cycle material texture storage");
                    undecedent::MaterialSlot& slot = editor_world.material_library.slots[
                        static_cast<std::size_t>(undecedent::clamped_material_id(active_material))
                    ];
                    undecedent::MaterialTextureSource& source =
                        undecedent::material_texture_source(slot, active_material_channel);
                    cycle_material_texture_storage_mode(source);
                    editor_world.dirty_materials = true;
                    undecedent::mark_material_textures_dirty(material_textures);
                    std::cout << "Material " << (active_material + 1) << ' '
                              << undecedent::material_texture_channel_label(active_material_channel)
                              << " texture storage "
                              << material_texture_storage_mode_label(source.storage_mode) << '\n';
                    continue;
                }
                if (handle_entity_dropdown_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_script_quick_button_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_entity_inspector_click(
                        editor_world,
                        width,
                        height,
                        event.button.x,
                        event.button.y,
                        (SDL_GetModState() & SDL_KMOD_SHIFT) != 0
                    )) {
                    continue;
                }
            }

            if (app_mode == AppMode::Editor2D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                if (effects_menu_open &&
                    handle_effects_menu_click(
                        game_render_config,
                        present_config,
                        window,
                        width,
                        height,
                        event.button.x,
                        event.button.y
                    )) {
                    continue;
                }
                if (handle_subdivision_controls_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_sculpt_button_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_entity_dropdown_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_script_quick_button_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
                if (handle_entity_inspector_click(
                        editor_world,
                        width,
                        height,
                        event.button.x,
                        event.button.y,
                        (SDL_GetModState() & SDL_KMOD_SHIFT) != 0
                    )) {
                    continue;
                }
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                const bool shift_select = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (event.button.button == SDL_BUTTON_LEFT &&
                    !shift_select &&
                    !editor_world.displacement_sculpt_enabled) {
                    if (start_translation_gizmo_drag(
                            editor_world,
                            game_camera,
                            width,
                            height,
                            event.button.x,
                            event.button.y,
                            game_render_config
                        )) {
                        continue;
                    }
                    const auto entity_pick = pick_editor_entity_3d(
                        editor_world,
                        game_camera,
                        width,
                        height,
                        event.button.x,
                        event.button.y,
                        game_render_config
                    );
                    if (entity_pick.hit) {
                        select_entity(editor_world, entity_pick.entity);
                        continue;
                    }
                }
                const SurfacePick pick = pick_runtime_surface(
                    editor_world.runtime_world,
                    game_camera,
                    width,
                    height,
                        event.button.x,
                    event.button.y,
                    game_render_config
                );
                if (editor_world.displacement_sculpt_enabled) {
                    if (event.button.button == SDL_BUTTON_LEFT && pick.hit) {
                        select_single_sector(editor_world, pick.sector_id);
                    }
                    continue;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (!shift_select && pick.hit) {
                        undecedent::Vec3 position = pick.point;
                        if (editor_world.entity_placement == EntityPlacementType::PointLight) {
                            position = add_vec3(position, mul_vec3(pick.normal, 24.0F));
                            place_entity(editor_world, position, game_camera.yaw);
                        } else {
                            place_entity_at_origin(
                                editor_world,
                                position,
                                game_camera.yaw,
                                game_render_config.player_eye_height
                            );
                        }
                        continue;
                    }
                    if (pick.hit) {
                        if (shift_select) {
                            toggle_sector_selection(editor_world, pick.sector_id);
                        } else {
                            select_single_sector(editor_world, pick.sector_id);
                        }
                        std::cout << "Selected sectors: " << editor_world.selected_sectors.size() << '\n';
                    } else if (!shift_select) {
                        clear_sector_selection(editor_world);
                    }
                    continue;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    if (pick.hit && apply_material_to_surface(editor_world, pick, active_material)) {
                        std::cout << "Applied material " << (active_material + 1) << " to sector " << pick.sector_id << '\n';
                    } else {
                        std::cout << "No 3D surface under cursor for material assignment.\n";
                    }
                    continue;
                }
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_WHEEL &&
                editor_world.displacement_sculpt_enabled) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);

                float scroll_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    scroll_y *= -1.0F;
                }

                const SurfacePick pick = pick_runtime_surface(
                    editor_world.runtime_world,
                    game_camera,
                    width,
                    height,
                    event.wheel.mouse_x,
                    event.wheel.mouse_y,
                    game_render_config
                );
                const bool shift_down = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                const float eased_scroll = editor_scroll_zoom_delta(scroll_y);
                if (eased_scroll == 0.0F) {
                    continue;
                }
                const float delta = eased_scroll * (shift_down ? 1.0F : kSectorHeightStep);
                if (sculpt_displacement_at_pick(editor_world, pick, delta)) {
                    std::cout << "Sculpted displacement at sector " << pick.sector_id << '\n';
                }
                continue;
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_MOTION &&
                editor_world.has_dragged_entity_gizmo) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                update_translation_gizmo_drag(
                    editor_world,
                    game_camera,
                    width,
                    height,
                    event.motion.x,
                    event.motion.y,
                    game_render_config,
                    (SDL_GetModState() & SDL_KMOD_SHIFT) != 0
                );
                continue;
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_LEFT &&
                editor_world.has_dragged_entity_gizmo) {
                finish_translation_gizmo_drag(editor_world);
                continue;
            }

            if (!is_2d_editor_mode(app_mode)) {
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                    editor_camera.panning = true;
                }

                if (event.button.button == SDL_BUTTON_LEFT) {
                    int width = 0;
                    int height = 0;
                    SDL_GetWindowSizeInPixels(window, &width, &height);
                    update_snapped_mouse(editor_world, editor_camera, width, height, event.button.x, event.button.y);

                    if (editor_world.plane_tool != PlaneToolMode::None) {
                        const int vertex_index = draft_vertex_at_screen(
                            editor_world,
                            editor_camera,
                            width,
                            height,
                            event.button.x,
                            event.button.y
                        );

                        if (editor_world.plane_tool == PlaneToolMode::DrawKnife) {
                            if (vertex_index >= 0) {
                                editor_world.dragged_draft_vertex = vertex_index;
                                editor_world.dragged_draft_vertex_moved = false;
                            } else {
                                if (editor_world.draft_vertices.size() >= 2) {
                                    editor_world.draft_vertices.clear();
                                }
                                editor_world.draft_vertices.push_back(editor_world.snapped_mouse);
                                refresh_draft(editor_world);
                                if (editor_world.draft_vertices.size() == 2) {
                                    commit_plane_tool(editor_world);
                                }
                            }
                        } else if (vertex_index >= 0) {
                            const bool double_clicked_last_vertex =
                                event.button.clicks >= 2 &&
                                editor_world.draft_vertices.size() >= 3 &&
                                vertex_index == static_cast<int>(editor_world.draft_vertices.size()) - 1;

                            if (double_clicked_last_vertex) {
                                commit_plane_tool(editor_world);
                            } else {
                                editor_world.dragged_draft_vertex = vertex_index;
                                editor_world.dragged_draft_vertex_moved = false;
                            }
                        } else if (draft_contains_point(editor_world, editor_world.snapped_mouse)) {
                            std::cout << "Vertex already exists in active loop.\n";
                        } else {
                            editor_world.draft_vertices.push_back(editor_world.snapped_mouse);
                            refresh_draft(editor_world);
                        }
                    } else {
                        const bool shift_select = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                        if (!shift_select) {
                            const auto entity_pick = pick_editor_entity_2d(
                                editor_world,
                                editor_camera,
                                width,
                                height,
                                event.button.x,
                                event.button.y,
                                game_render_config.player_eye_height
                            );
                            if (entity_pick.hit) {
                                select_entity(editor_world, entity_pick.entity);
                                continue;
                            }
                        }
                        undecedent::Vec2 hit_vertex{};
                        if (committed_vertex_at_screen(
                                editor_world,
                                editor_camera,
                                width,
                                height,
                                event.button.x,
                                event.button.y,
                                hit_vertex
                            )) {
                            editor_world.committed_drag_snapshot = editor_world.sectors;
                            editor_world.dragged_committed_refs = matching_committed_vertices(editor_world, hit_vertex);
                            editor_world.dragged_committed_vertex = hit_vertex;
                            editor_world.has_dragged_committed_vertex = !editor_world.dragged_committed_refs.empty();
                            editor_world.dragged_committed_vertex_moved = false;
                            editor_world.has_hovered_committed_vertex = false;
                            continue;
                        }

                        const undecedent::Vec2 world{
                            screen_to_world_x(event.button.x, width, editor_camera),
                            screen_to_world_y(event.button.y, height, editor_camera),
                        };
                        const int clicked_sector = sector_at_point(editor_world, world);
                        if (!shift_select && clicked_sector < 0) {
                            place_entity_at_origin(
                                editor_world,
                                undecedent::Vec3{editor_world.snapped_mouse.x, editor_world.slice_z, editor_world.snapped_mouse.y},
                                0.0F,
                                game_render_config.player_eye_height
                            );
                        } else if (shift_select) {
                            toggle_sector_selection(editor_world, clicked_sector);
                        } else {
                            select_single_sector(editor_world, clicked_sector);
                        }

                        if (editor_world.selected_sector >= 0) {
                            std::cout << "Selected sectors: " << editor_world.selected_sectors.size() << '\n';
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                    editor_camera.panning = false;
                }

                if (event.button.button == SDL_BUTTON_LEFT && editor_world.has_dragged_committed_vertex) {
                    finish_committed_vertex_drag(editor_world);
                }

                if (event.button.button == SDL_BUTTON_LEFT && editor_world.dragged_draft_vertex >= 0) {
                    const bool should_close =
                        !editor_world.dragged_draft_vertex_moved &&
                        editor_world.dragged_draft_vertex == 0 &&
                        editor_world.draft_vertices.size() >= 3;

                    if (should_close) {
                        commit_plane_tool(editor_world);
                    } else {
                        refresh_draft(editor_world);
                    }

                    editor_world.dragged_draft_vertex = -1;
                    editor_world.dragged_draft_vertex_moved = false;
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && editor_camera.panning) {
                const float pan_x = event.motion.xrel / editor_camera.zoom;
                const float pan_y = event.motion.yrel / editor_camera.zoom;
                editor_camera.x -= pan_x;
                editor_camera.y -= pan_y;
                editor_camera.target_x -= pan_x;
                editor_camera.target_y -= pan_y;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                update_snapped_mouse(editor_world, editor_camera, width, height, event.motion.x, event.motion.y);

                if (editor_world.dragged_draft_vertex >= 0 &&
                    editor_world.dragged_draft_vertex < static_cast<int>(editor_world.draft_vertices.size()) &&
                    !draft_contains_point_except(
                        editor_world,
                        editor_world.snapped_mouse,
                        editor_world.dragged_draft_vertex
                    )) {
                    undecedent::Vec2& dragged =
                        editor_world.draft_vertices[static_cast<std::size_t>(editor_world.dragged_draft_vertex)];
                    if (!same_editor_point(dragged, editor_world.snapped_mouse)) {
                        dragged = editor_world.snapped_mouse;
                        editor_world.dragged_draft_vertex_moved = true;
                        refresh_draft(editor_world);
                    }
                }

                if (editor_world.has_dragged_committed_vertex &&
                    !same_editor_point(editor_world.dragged_committed_vertex, editor_world.snapped_mouse)) {
                    move_dragged_committed_vertices(editor_world, editor_world.snapped_mouse);
                    editor_world.dragged_committed_vertex_moved = true;
                }

                update_committed_vertex_hover(
                    editor_world,
                    editor_camera,
                    width,
                    height,
                    event.motion.x,
                    event.motion.y
                );
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);

                float scroll_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    scroll_y *= -1.0F;
                }

                if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
                    apply_editor_slice_scroll(editor_world, editor_camera, scroll_y);
                } else {
                    apply_editor_scroll_zoom(
                        editor_camera,
                        scroll_y,
                        event.wheel.mouse_x,
                        event.wheel.mouse_y,
                        width,
                        height
                    );
                }
            }
        }
        sync_script_text_input(window, editor_world, script_text_input_active);
        events_ms = ticks_to_ms(events_start_ticks, SDL_GetTicksNS());
        const bool editor_2d_now = is_2d_editor_mode(app_mode);
        const bool editor_3d_now = app_mode == AppMode::Editor3D;
        const bool runtime_view_now = app_mode == AppMode::Editor3D || app_mode == AppMode::Playtest;
        process_pending_map_dialogs(
            editor_world,
            material_textures,
            game_camera,
            playtest_state,
            script_vm,
            script_map_start_dispatched,
            script_runtime_failed,
            script_current_sector_id,
            is_editor_mode(app_mode),
            map_dialog_requests
        );
        process_pending_material_dialogs(editor_world, material_textures, material_dialog_requests);

        const Uint64 update_start_ticks = SDL_GetTicksNS();
        if (editor_2d_now) {
            update_editor_camera(editor_camera, dt);
        } else if (app_mode == AppMode::Playtest) {
            update_playtest_camera(game_camera, editor_world.runtime_world, playtest_state, dt, game_control_config());
            dispatch_playtest_scripts(
                script_vm,
                editor_world,
                game_camera,
                script_map_start_dispatched,
                script_runtime_failed,
                script_current_sector_id
            );
        } else {
            update_game_camera(game_camera, dt, game_control_config());
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        core_draw_begin_frame(width, height);
        sdf_text_begin_frame();
        if (editor_2d_now) {
            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            update_snapped_mouse(editor_world, editor_camera, width, height, mouse_x, mouse_y);
            update_committed_vertex_hover(editor_world, editor_camera, width, height, mouse_x, mouse_y);
        }
        update_ms = ticks_to_ms(update_start_ticks, SDL_GetTicksNS());

        const Uint64 render_start_ticks = SDL_GetTicksNS();
        glViewport(0, 0, width, height);
        if (editor_2d_now) {
            glClearColor(0.025F, 0.035F, 0.04F, 1.0F);
        } else {
            glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
        }
        const bool deferred_playtest_now = app_mode == AppMode::Playtest;
        if (!deferred_playtest_now && (!benchmark.enabled || !benchmark.skip_clear)) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        if (runtime_view_now) {
            undecedent::ensure_material_texture_array(material_textures, editor_world.material_library);
        }

        if (editor_2d_now) {
            core_set_identity_mvp();
            core_set_identity_mvp();
            draw_editor_2d_view(editor_world, width, height, editor_camera, editor_2d_render_config);
        } else if (runtime_view_now) {
            if (app_mode == AppMode::Playtest) {
                visible_triangle_count = draw_deferred_runtime_world(
                    deferred_renderer,
                    editor_world.runtime_world,
                    editor_world.runtime_render_cache,
                    undecedent::point_lights_from_entities(editor_world.entities),
                    editor_world.world_lighting,
                    width,
                    height,
                    game_camera,
                    runtime_wire_overlay_enabled,
                    game_render_config,
                    &material_textures
                );
                gbuffer_ms = deferred_renderer.last_gbuffer_ms;
                shadow_pack_upload_ms = deferred_renderer.last_shadow_pack_upload_ms;
                shadow_ms = deferred_renderer.last_shadow_ms;
                screen_shadow_ms = deferred_renderer.last_screen_shadow_ms;
                lighting_ms = deferred_renderer.last_lighting_ms;
                wire_overlay_ms = deferred_renderer.last_wire_overlay_ms;
                shadow_cache_hits = deferred_renderer.last_shadow_cache_hits;
                shadow_cache_misses = deferred_renderer.last_shadow_cache_misses;
                point_shadow_lights = deferred_renderer.last_point_shadow_lights_rendered;
                point_shadow_faces = deferred_renderer.last_point_shadow_faces_rendered;
                sun_shadow_cascades = deferred_renderer.last_sun_shadow_cascades_rendered;
            } else {
                visible_triangle_count = draw_runtime_world(
                    editor_world.runtime_world,
                    editor_world.runtime_render_cache,
                    width,
                    height,
                    game_camera,
                    runtime_wire_overlay_enabled,
                    false,
                    game_render_config,
                    &material_textures
                );
            }
            if (editor_3d_now) {
                draw_point_lights_3d(
                    undecedent::point_lights_from_entities(editor_world.entities),
                    width,
                    height,
                    game_camera,
                    game_render_config
                );
                draw_player_spawn_3d(
                    undecedent::player_spawn_from_entities(editor_world.entities),
                    width,
                    height,
                    game_camera,
                    game_render_config
                );
                draw_translation_gizmo(editor_world, width, height, game_camera, game_render_config);
            }
        }
        core_draw_flush();
        sdf_text_flush();
        render_ms = ticks_to_ms(render_start_ticks, SDL_GetTicksNS());

        const Uint64 overlay_start_ticks = SDL_GetTicksNS();
        if (!benchmark.enabled && fps_counter_enabled) {
            core_set_identity_mvp();
            core_set_identity_mvp();
            draw_fps_counter(displayed_fps, width, height);
        }

        if (!benchmark.enabled && profiler_enabled) {
            core_set_identity_mvp();
            core_set_identity_mvp();
            ProfilerDisplay profiler_to_draw = displayed_profiler;
            profiler_to_draw.fps = displayed_fps;
            profiler_to_draw.total_triangles = static_cast<int>(editor_world.runtime_world.triangles.size());
            profiler_to_draw.visible_triangles = editor_2d_now
                ? 0
                : visible_triangle_count;
            profiler_to_draw.sectors = static_cast<int>(editor_world.runtime_world.sectors.size());
            profiler_to_draw.walls = static_cast<int>(editor_world.runtime_world.walls.size());
            profiler_to_draw.shadow_cache_hits = shadow_cache_hits;
            profiler_to_draw.shadow_cache_misses = shadow_cache_misses;
            profiler_to_draw.point_shadow_lights = point_shadow_lights;
            profiler_to_draw.point_shadow_faces = point_shadow_faces;
            profiler_to_draw.sun_shadow_cascades = sun_shadow_cascades;
            draw_profiler_overlay(profiler_to_draw, width, height, fps_counter_enabled ? 50.0F : 16.0F);
        }
        if (!benchmark.enabled && effects_menu_open) {
            core_set_identity_mvp();
            draw_effects_menu(game_render_config, present_config, width, height);
        }
        if (!benchmark.enabled && editor_3d_now) {
            core_set_identity_mvp();
            core_set_identity_mvp();
            draw_material_selector(editor_world.material_library, active_material, width, height);
            draw_material_texture_controls(editor_world, active_material, active_material_channel, width, height);
        }
        if (!benchmark.enabled && is_editor_mode(app_mode)) {
            core_set_identity_mvp();
            core_set_identity_mvp();
            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            draw_subdivision_controls(editor_world, width, height);
            draw_sculpt_button(editor_world, width, height, mouse_x, mouse_y);
            draw_entity_dropdown(editor_world, width, height);
            draw_entity_inspector(editor_world, width, height);
            draw_script_quick_buttons(editor_world, width, height);
            draw_script_editor_workspace(editor_world, width, height);
        }
        core_draw_flush();
        sdf_text_flush();
        overlay_ms = ticks_to_ms(overlay_start_ticks, SDL_GetTicksNS());

        if (profiler_finish_diagnostic_enabled) {
            const Uint64 finish_start_ticks = SDL_GetTicksNS();
            glFinish();
            finish_ms = ticks_to_ms(finish_start_ticks, SDL_GetTicksNS());
        }

        pace_ms = pace_frame_before_swap(frame_start_ticks, present_config);

        const Uint64 swap_start_ticks = SDL_GetTicksNS();
        if (!benchmark.enabled || !benchmark.skip_swap) {
            SDL_GL_SwapWindow(window);
        }
        swap_ms = ticks_to_ms(swap_start_ticks, SDL_GetTicksNS());

        const double frame_ms = ticks_to_ms(frame_start_ticks, SDL_GetTicksNS());
        profiler_accumulator.frame_ms += frame_ms;
        profiler_accumulator.events_ms += events_ms;
        profiler_accumulator.update_ms += update_ms;
        profiler_accumulator.render_ms += render_ms;
        profiler_accumulator.gbuffer_ms += gbuffer_ms;
        profiler_accumulator.shadow_pack_upload_ms += shadow_pack_upload_ms;
        profiler_accumulator.shadow_ms += shadow_ms;
        profiler_accumulator.screen_shadow_ms += screen_shadow_ms;
        profiler_accumulator.lighting_ms += lighting_ms;
        profiler_accumulator.wire_overlay_ms += wire_overlay_ms;
        profiler_accumulator.overlay_ms += overlay_ms;
        profiler_accumulator.pace_ms += pace_ms;
        profiler_accumulator.finish_ms += finish_ms;
        profiler_accumulator.swap_ms += swap_ms;
        profiler_accumulator.seconds += frame_ms / 1000.0;
        ++profiler_accumulator.frames;

        if (benchmark.enabled) {
            benchmark_accumulator.frame_ms += frame_ms;
            benchmark_accumulator.events_ms += events_ms;
            benchmark_accumulator.update_ms += update_ms;
            benchmark_accumulator.render_ms += render_ms;
            benchmark_accumulator.gbuffer_ms += gbuffer_ms;
            benchmark_accumulator.shadow_pack_upload_ms += shadow_pack_upload_ms;
            benchmark_accumulator.shadow_ms += shadow_ms;
            benchmark_accumulator.screen_shadow_ms += screen_shadow_ms;
            benchmark_accumulator.lighting_ms += lighting_ms;
            benchmark_accumulator.wire_overlay_ms += wire_overlay_ms;
            benchmark_accumulator.overlay_ms += overlay_ms;
            benchmark_accumulator.pace_ms += pace_ms;
            benchmark_accumulator.finish_ms += finish_ms;
            benchmark_accumulator.swap_ms += swap_ms;
            benchmark_accumulator.seconds += frame_ms / 1000.0;
            ++benchmark_accumulator.frames;
            if (benchmark_accumulator.seconds >= 1.0) {
                print_benchmark_report(
                    benchmark,
                    benchmark_accumulator,
                    present_config,
                    is_editor_mode(app_mode),
                    static_cast<int>(editor_world.runtime_world.triangles.size()),
                    editor_2d_now ? 0 : visible_triangle_count,
                    static_cast<int>(editor_world.runtime_world.sectors.size()),
                    static_cast<int>(editor_world.runtime_world.walls.size())
                );
                benchmark_accumulator = {};
            }
        }

        if (profiler_accumulator.seconds >= kFpsCounterUpdateSeconds && profiler_accumulator.frames > 0) {
            const double inv_frames = 1.0 / static_cast<double>(profiler_accumulator.frames);
            displayed_profiler.frame_ms = profiler_accumulator.frame_ms * inv_frames;
            displayed_profiler.events_ms = profiler_accumulator.events_ms * inv_frames;
            displayed_profiler.update_ms = profiler_accumulator.update_ms * inv_frames;
            displayed_profiler.render_ms = profiler_accumulator.render_ms * inv_frames;
            displayed_profiler.gbuffer_ms = profiler_accumulator.gbuffer_ms * inv_frames;
            displayed_profiler.shadow_pack_upload_ms = profiler_accumulator.shadow_pack_upload_ms * inv_frames;
            displayed_profiler.shadow_ms = profiler_accumulator.shadow_ms * inv_frames;
            displayed_profiler.screen_shadow_ms = profiler_accumulator.screen_shadow_ms * inv_frames;
            displayed_profiler.lighting_ms = profiler_accumulator.lighting_ms * inv_frames;
            displayed_profiler.wire_overlay_ms = profiler_accumulator.wire_overlay_ms * inv_frames;
            displayed_profiler.overlay_ms = profiler_accumulator.overlay_ms * inv_frames;
            displayed_profiler.pace_ms = profiler_accumulator.pace_ms * inv_frames;
            displayed_profiler.finish_ms = profiler_accumulator.finish_ms * inv_frames;
            displayed_profiler.swap_ms = profiler_accumulator.swap_ms * inv_frames;
            displayed_profiler.fps = static_cast<int>(std::round(static_cast<double>(profiler_accumulator.frames) / profiler_accumulator.seconds));
            displayed_profiler.effective_swap_interval = present_config.effective_swap_interval;
            displayed_profiler.fps_cap = present_config.fps_cap;
            displayed_profiler.total_triangles = static_cast<int>(editor_world.runtime_world.triangles.size());
            displayed_profiler.visible_triangles = editor_2d_now ? 0 : visible_triangle_count;
            displayed_profiler.sectors = static_cast<int>(editor_world.runtime_world.sectors.size());
            displayed_profiler.walls = static_cast<int>(editor_world.runtime_world.walls.size());
            displayed_profiler.shadow_cache_hits = shadow_cache_hits;
            displayed_profiler.shadow_cache_misses = shadow_cache_misses;
            displayed_profiler.point_shadow_lights = point_shadow_lights;
            displayed_profiler.point_shadow_faces = point_shadow_faces;
            displayed_profiler.sun_shadow_cascades = sun_shadow_cascades;
            profiler_accumulator = {};
        }
    }

    if (script_text_input_active) {
        SDL_StopTextInput(window);
    }
    destroy_runtime_render_cache(editor_world.runtime_render_cache);
    undecedent::destroy_material_texture_array(material_textures);
    undecedent::destroy_deferred_renderer(deferred_renderer);
    sdf_text_shutdown();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
