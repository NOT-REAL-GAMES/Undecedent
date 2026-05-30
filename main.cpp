#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
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
#include "undecedent/materials.hpp"
#include "undecedent/physics.hpp"
#include "undecedent/runtime_render_cache.hpp"
#include "undecedent/runtime_render.hpp"
#include "undecedent/runtime_pick.hpp"
#include "undecedent/runtime_world.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/sdl_platform.hpp"
#include "undecedent/triangulator.hpp"

namespace {
using undecedent::draw_screen_line;
using undecedent::draw_screen_quad;
using undecedent::draw_editor_2d_view;
using undecedent::draw_deferred_runtime_world;
using undecedent::draw_point_lights_3d;
using undecedent::draw_player_spawn_3d;
using undecedent::draw_runtime_world;
using undecedent::draw_stroke_text;
using undecedent::draw_entity_dropdown;
using undecedent::draw_material_selector;
using undecedent::screen_to_ndc_x;
using undecedent::screen_to_ndc_y;
using undecedent::configure_gl_attributes;
using undecedent::log_sdl_error;
using undecedent::toggle_exclusive_fullscreen;
using undecedent::add_vec3;
using undecedent::mul_vec3;
using undecedent::adjust_selected_sector_floor_heights;
using undecedent::adjust_selected_sector_heights;
using undecedent::apply_editor_scroll_zoom;
using undecedent::apply_editor_slice_scroll;
using undecedent::apply_material_to_surface;
using undecedent::cancel_plane_tool;
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
using undecedent::GameCamera;
using undecedent::GameControlConfig;
using undecedent::GameRenderConfig;
using undecedent::handle_entity_dropdown_click;
using undecedent::is_dragged_committed_ref;
using undecedent::is_sector_selected;
using undecedent::matching_committed_vertices;
using undecedent::merge_selected_sectors;
using undecedent::move_dragged_committed_vertices;
using undecedent::place_entity;
using undecedent::place_entity_at_origin;
using undecedent::PlaneToolMode;
using undecedent::pick_runtime_surface;
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
using undecedent::start_hole_plane;
using undecedent::start_knife_tool;
using undecedent::start_outer_plane;
using undecedent::SurfacePick;
using undecedent::toggle_sector_selection;
using undecedent::undo_editor_action;
using undecedent::update_committed_vertex_hover;
using undecedent::update_editor_camera;
using undecedent::update_game_camera;
using undecedent::update_playtest_camera;
using undecedent::update_snapped_mouse;
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
constexpr int kMaxDeferredPointLights = 32;
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
    double overlay_ms = 0.0;
    double finish_ms = 0.0;
    double swap_ms = 0.0;
    int fps = 0;
    int total_triangles = 0;
    int visible_triangles = 0;
    int sectors = 0;
    int walls = 0;
};

struct ProfilerAccumulator {
    double frame_ms = 0.0;
    double events_ms = 0.0;
    double update_ms = 0.0;
    double render_ms = 0.0;
    double overlay_ms = 0.0;
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
    double overlay_ms = 0.0;
    double finish_ms = 0.0;
    double swap_ms = 0.0;
    double seconds = 0.0;
    int frames = 0;
};

struct MapDialogRequests {
    std::mutex mutex;
    std::string pending_save_path;
    std::string pending_load_path;
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

void queue_dialog_message(MapDialogRequests& requests, std::string message) {
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

const SDL_DialogFileFilter* map_dialog_filters() {
    static const SDL_DialogFileFilter filters[] = {
        {"Undecedent Map", "udmap"},
    };
    return filters;
}

void show_save_map_dialog(SDL_Window* window, MapDialogRequests& requests) {
    SDL_ShowSaveFileDialog(save_map_dialog_callback, &requests, window, map_dialog_filters(), 1, nullptr);
    std::cout << "Opening save dialog...\n";
}

void show_load_map_dialog(SDL_Window* window, MapDialogRequests& requests) {
    SDL_ShowOpenFileDialog(load_map_dialog_callback, &requests, window, map_dialog_filters(), 1, nullptr, false);
    std::cout << "Opening load dialog...\n";
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
        << " overlay=" << format_ms(accumulator.overlay_ms * inv_frames) << "ms"
        << " finish=" << format_ms(accumulator.finish_ms * inv_frames) << "ms"
        << " swap=" << format_ms(accumulator.swap_ms * inv_frames) << "ms"
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
    undecedent::PlayerSpawn spawn = editor_world.player_spawn;
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

void process_pending_map_dialogs(
    EditorWorld& editor_world,
    GameCamera& game_camera,
    PlaytestPlayerState& playtest_state,
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
        const undecedent::SaveMapResult result =
            undecedent::save_map_file(
                editor_world.sectors,
                editor_world.player_spawn,
                editor_world.point_lights,
                save_path
            );
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
        editor_world.player_spawn = result.player_spawn;
        editor_world.point_lights = result.point_lights;
        clear_sector_selection(editor_world);
        rebuild_runtime_geometry(editor_world);
        if (!editor_enabled) {
            spawn_playtest_camera(editor_world, game_camera);
            reset_playtest_player_state(playtest_state, editor_world.runtime_world, game_camera);
        }
        std::cout << result.message << " Sectors: " << editor_world.sectors.size() << '\n';
    }
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

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.38F);
    draw_screen_quad(10.0F, 10.0F, box_width, box_height, width, height);
    glEnd();

    glLineWidth(1.5F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
    draw_stroke_text(label, x, y, size, width, height);
    glEnd();

    glLineWidth(1.0F);
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
        "OVERLAY " + format_ms(profiler.overlay_ms) + "MS",
        "FINISH " + format_ms(profiler.finish_ms) + "MS",
        "SWAP " + format_ms(profiler.swap_ms) + "MS",
        "TRIS " + std::to_string(profiler.total_triangles) + "/" + std::to_string(profiler.visible_triangles),
        "SECT " + std::to_string(profiler.sectors) + " WALL " + std::to_string(profiler.walls),
    };

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.42F);
    draw_screen_quad(10.0F, top_y - 6.0F, 236.0F, 12.0F + line_height * static_cast<float>(lines.size()), width, height);
    glEnd();

    glLineWidth(1.35F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.92F);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        draw_stroke_text(lines[i], x, top_y + static_cast<float>(i) * line_height, size, width, height);
    }
    glEnd();

    glLineWidth(1.0F);
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

    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, mode == AppMode::Editor2D);
    if (mode != AppMode::Editor2D) {
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

    SDL_GL_SetSwapInterval(0);
    SDL_SetEventEnabled(SDL_EVENT_MOUSE_MOTION, false);

    bool running = true;
    AppMode app_mode = AppMode::Editor3D;
    AppMode previous_editor_mode = AppMode::Editor3D;
    bool fps_counter_enabled = false;
    bool profiler_enabled = false;
    bool profiler_finish_diagnostic_enabled = false;
    bool runtime_wire_overlay_enabled = false;
    BenchmarkState benchmark{};
    float fps_counter_seconds = 0.0F;
    int fps_counter_frames = 0;
    int displayed_fps = 0;
    ProfilerAccumulator profiler_accumulator{};
    BenchmarkAccumulator benchmark_accumulator{};
    ProfilerDisplay displayed_profiler{};
    undecedent::FullscreenState fullscreen_state{};
    MapDialogRequests map_dialog_requests{};
    EditorCamera editor_camera{};
    GameCamera game_camera{};
    PlaytestPlayerState playtest_state{};
    EditorWorld editor_world{};
    const Editor2DRenderConfig editor_2d_render_config{
        kEditorMajorGridEvery,
        kEditorMinZoom,
        kScaleIndicatorTargetPixels,
        kScaleIndicatorMinPixels,
        kScaleIndicatorMaxPixels,
        kPlayerEyeHeight,
    };
    const GameRenderConfig game_render_config{
        kGameNearPlane,
        kGameFarPlane,
        70.0F,
        kPlayerEyeHeight,
        kPlayerHeight,
        kPlayerRadius,
    };
    undecedent::DeferredRenderer deferred_renderer{};
    int active_material = undecedent::kDefaultMaterialId;
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
        double overlay_ms = 0.0;
        double finish_ms = 0.0;
        double swap_ms = 0.0;
        int visible_triangle_count = 0;

        const Uint64 events_start_ticks = SDL_GetTicksNS();
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;
                const SDL_Scancode scancode = event.key.scancode;
                const bool ctrl_down = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                const bool shift_down = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                const bool editor_2d = is_2d_editor_mode(app_mode);
                const bool editor_3d = app_mode == AppMode::Editor3D;

                if (ctrl_down && (key == SDLK_Z || scancode == SDL_SCANCODE_Z) && !event.key.repeat && is_editor_mode(app_mode)) {
                    if (shift_down) {
                        redo_editor_action(editor_world);
                    } else {
                        undo_editor_action(editor_world);
                    }
                    continue;
                }

                if (ctrl_down && (key == SDLK_Y || scancode == SDL_SCANCODE_Y) && !event.key.repeat && is_editor_mode(app_mode)) {
                    redo_editor_action(editor_world);
                    continue;
                }

                if (ctrl_down && (key == SDLK_S || scancode == SDL_SCANCODE_S) && !event.key.repeat) {
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

                if (editor_3d && !event.key.repeat) {
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

                if (key == SDLK_ESCAPE || key == SDLK_Q) {
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
                        finish_committed_vertex_drag(editor_world);
                        cancel_plane_tool(editor_world);
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
                }
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                if (handle_entity_dropdown_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
            }

            if (app_mode == AppMode::Editor2D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                if (handle_entity_dropdown_click(editor_world, width, height, event.button.x, event.button.y)) {
                    continue;
                }
            }

            if (app_mode == AppMode::Editor3D && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                const SurfacePick pick = pick_runtime_surface(
                    editor_world.runtime_world,
                    game_camera,
                    width,
                    height,
                        event.button.x,
                    event.button.y,
                    game_render_config
                );
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const bool shift_select = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
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
                        const bool shift_select = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
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
        events_ms = ticks_to_ms(events_start_ticks, SDL_GetTicksNS());
        const bool editor_2d_now = is_2d_editor_mode(app_mode);
        const bool editor_3d_now = app_mode == AppMode::Editor3D;
        const bool runtime_view_now = app_mode == AppMode::Editor3D || app_mode == AppMode::Playtest;
        process_pending_map_dialogs(
            editor_world,
            game_camera,
            playtest_state,
            is_editor_mode(app_mode),
            map_dialog_requests
        );

        const Uint64 update_start_ticks = SDL_GetTicksNS();
        if (editor_2d_now) {
            update_editor_camera(editor_camera, dt);
        } else if (app_mode == AppMode::Playtest) {
            update_playtest_camera(game_camera, editor_world.runtime_world, playtest_state, dt, game_control_config());
        } else {
            update_game_camera(game_camera, dt, game_control_config());
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
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
        if (!benchmark.enabled || !benchmark.skip_clear) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        if (editor_2d_now) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            draw_editor_2d_view(editor_world, width, height, editor_camera, editor_2d_render_config);
        } else if (runtime_view_now) {
            if (app_mode == AppMode::Playtest) {
                visible_triangle_count = draw_deferred_runtime_world(
                    deferred_renderer,
                    editor_world.runtime_world,
                    editor_world.runtime_render_cache,
                    editor_world.point_lights,
                    width,
                    height,
                    game_camera,
                    runtime_wire_overlay_enabled,
                    game_render_config
                );
            } else {
                visible_triangle_count = draw_runtime_world(
                    editor_world.runtime_world,
                    editor_world.runtime_render_cache,
                    width,
                    height,
                    game_camera,
                    runtime_wire_overlay_enabled,
                    false,
                    game_render_config
                );
            }
            if (editor_3d_now) {
                draw_point_lights_3d(editor_world.point_lights, width, height, game_camera, game_render_config);
                draw_player_spawn_3d(editor_world.player_spawn, width, height, game_camera, game_render_config);
            }
        }
        render_ms = ticks_to_ms(render_start_ticks, SDL_GetTicksNS());

        const Uint64 overlay_start_ticks = SDL_GetTicksNS();
        if (!benchmark.enabled && fps_counter_enabled) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            draw_fps_counter(displayed_fps, width, height);
        }

        if (!benchmark.enabled && profiler_enabled) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            ProfilerDisplay profiler_to_draw = displayed_profiler;
            profiler_to_draw.fps = displayed_fps;
            profiler_to_draw.total_triangles = static_cast<int>(editor_world.runtime_world.triangles.size());
            profiler_to_draw.visible_triangles = editor_2d_now
                ? 0
                : visible_triangle_count;
            profiler_to_draw.sectors = static_cast<int>(editor_world.runtime_world.sectors.size());
            profiler_to_draw.walls = static_cast<int>(editor_world.runtime_world.walls.size());
            draw_profiler_overlay(profiler_to_draw, width, height, fps_counter_enabled ? 50.0F : 16.0F);
        }
        if (!benchmark.enabled && editor_3d_now) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            draw_material_selector(active_material, width, height);
        }
        if (!benchmark.enabled && is_editor_mode(app_mode)) {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            draw_entity_dropdown(editor_world, width, height);
        }
        overlay_ms = ticks_to_ms(overlay_start_ticks, SDL_GetTicksNS());

        if (profiler_finish_diagnostic_enabled) {
            const Uint64 finish_start_ticks = SDL_GetTicksNS();
            glFinish();
            finish_ms = ticks_to_ms(finish_start_ticks, SDL_GetTicksNS());
        }

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
        profiler_accumulator.overlay_ms += overlay_ms;
        profiler_accumulator.finish_ms += finish_ms;
        profiler_accumulator.swap_ms += swap_ms;
        profiler_accumulator.seconds += frame_ms / 1000.0;
        ++profiler_accumulator.frames;

        if (benchmark.enabled) {
            benchmark_accumulator.frame_ms += frame_ms;
            benchmark_accumulator.events_ms += events_ms;
            benchmark_accumulator.update_ms += update_ms;
            benchmark_accumulator.render_ms += render_ms;
            benchmark_accumulator.overlay_ms += overlay_ms;
            benchmark_accumulator.finish_ms += finish_ms;
            benchmark_accumulator.swap_ms += swap_ms;
            benchmark_accumulator.seconds += frame_ms / 1000.0;
            ++benchmark_accumulator.frames;
            if (benchmark_accumulator.seconds >= 1.0) {
                print_benchmark_report(
                    benchmark,
                    benchmark_accumulator,
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
            displayed_profiler.overlay_ms = profiler_accumulator.overlay_ms * inv_frames;
            displayed_profiler.finish_ms = profiler_accumulator.finish_ms * inv_frames;
            displayed_profiler.swap_ms = profiler_accumulator.swap_ms * inv_frames;
            displayed_profiler.fps = static_cast<int>(std::round(static_cast<double>(profiler_accumulator.frames) / profiler_accumulator.seconds));
            displayed_profiler.total_triangles = static_cast<int>(editor_world.runtime_world.triangles.size());
            displayed_profiler.visible_triangles = editor_2d_now ? 0 : visible_triangle_count;
            displayed_profiler.sectors = static_cast<int>(editor_world.runtime_world.sectors.size());
            displayed_profiler.walls = static_cast<int>(editor_world.runtime_world.walls.size());
            profiler_accumulator = {};
        }
    }

    destroy_runtime_render_cache(editor_world.runtime_render_cache);
    undecedent::destroy_deferred_renderer(deferred_renderer);
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
