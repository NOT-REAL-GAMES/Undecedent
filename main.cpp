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

#include "undecedent/csg.hpp"
#include "undecedent/deferred_renderer.hpp"
#include "undecedent/editor_slice.hpp"
#include "undecedent/map_io.hpp"
#include "undecedent/math3d.hpp"
#include "undecedent/materials.hpp"
#include "undecedent/physics.hpp"
#include "undecedent/runtime_geometry.hpp"
#include "undecedent/runtime_world.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/sdl_platform.hpp"
#include "undecedent/triangulator.hpp"

namespace {
using undecedent::draw_screen_line;
using undecedent::draw_screen_quad;
using undecedent::screen_to_ndc_x;
using undecedent::screen_to_ndc_y;
using undecedent::configure_gl_attributes;
using undecedent::log_sdl_error;
using undecedent::toggle_exclusive_fullscreen;
using undecedent::add_vec3;
using undecedent::cross_vec3;
using undecedent::dot_vec3;
using undecedent::mul_vec3;
using undecedent::normalize_vec3;
using undecedent::ray_triangle_intersection;

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kEditorMajorGridEvery = 4;
constexpr float kEditorBaseGridWorldUnits = 1.0F;
constexpr float kEditorMinGridPixels = 28.0F;
constexpr float kEditorMaxGridPixels = 112.0F;
constexpr float kEditorMinZoom = 1.0F / 65536.0F;
constexpr float kEditorMaxZoom = 16.0F;
constexpr float kEditorZoomStep = 1.08F;
constexpr float kEditorScrollSensitivity = 0.55F;
constexpr float kEditorScrollDeadzone = 0.02F;
constexpr float kEditorScrollLogStrength = 4.0F;
constexpr float kEditorMaxScrollDelta = 64.0F;
constexpr float kEditorCameraSmoothRate = 14.0F;
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
constexpr float kSectorMinHeight = 8.0F;
constexpr float kScaleIndicatorTargetPixels = 160.0F;
constexpr float kScaleIndicatorMinPixels = 80.0F;
constexpr float kScaleIndicatorMaxPixels = 190.0F;
constexpr float kCloseVertexPixels = 12.0F;
constexpr float kFpsCounterUpdateSeconds = 0.25F;
constexpr std::size_t kEditorHistoryLimit = 128;
constexpr float kPi = 3.14159265358979323846F;

struct EditorCamera {
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;
    float target_x = 0.0F;
    float target_y = 0.0F;
    float target_zoom = 1.0F;
    bool panning = false;
};

struct GameCamera {
    float x = 0.0F;
    float y = 64.0F;
    float z = 220.0F;
    float yaw = 0.0F;
    float pitch = 0.0F;
};

struct PlaytestPlayerState {
    float vertical_velocity = 0.0F;
    bool grounded = false;
    bool jump_was_down = false;
};

struct RuntimeRenderVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
};

struct RuntimeRenderRange {
    GLint first_vertex = 0;
    GLsizei vertex_count = 0;
};

struct RuntimeRenderCache {
    GLuint vertex_buffer = 0;
    GLsizei total_vertices = 0;
    std::vector<RuntimeRenderRange> sector_ranges;
};

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

enum class PlaneToolMode {
    None,
    DrawOuter,
    DrawHole,
};

enum class EntityPlacementType {
    PlayerSpawn,
    PointLight,
};

struct SurfacePick {
    bool hit = false;
    int sector_id = -1;
    int material_id = undecedent::kDefaultMaterialId;
    float distance = 0.0F;
    undecedent::Vec3 point;
    undecedent::Vec3 normal;
    undecedent::RuntimeSurfaceRef surface;
};

struct CommittedVertexRef {
    std::size_t sector = 0;
    std::size_t vertex = 0;
};

struct EditorHistorySnapshot {
    std::vector<undecedent::SectorPlane> sectors;
    std::set<int> selected_sectors;
    undecedent::PlayerSpawn player_spawn;
    std::vector<undecedent::PointLight> point_lights;
    float slice_z = 0.0F;
    int selected_sector = -1;
};

struct EditorWorld {
    std::vector<undecedent::SectorPlane> sectors;
    std::vector<undecedent::Vec2> draft_vertices;
    std::vector<undecedent::SectorPlane> committed_drag_snapshot;
    std::vector<CommittedVertexRef> dragged_committed_refs;
    std::set<int> selected_sectors;
    std::vector<EditorHistorySnapshot> undo_stack;
    std::vector<EditorHistorySnapshot> redo_stack;
    undecedent::PlayerSpawn player_spawn;
    std::vector<undecedent::PointLight> point_lights;
    undecedent::RuntimeWorld runtime_world;
    RuntimeRenderCache runtime_render_cache;
    undecedent::TriangulationResult draft_result;
    undecedent::Vec2 snapped_mouse;
    undecedent::Vec2 hovered_committed_vertex;
    undecedent::Vec2 dragged_committed_vertex;
    float slice_z = 0.0F;
    float slice_scroll_remainder = 0.0F;
    int dragged_draft_vertex = -1;
    int selected_sector = -1;
    EntityPlacementType entity_placement = EntityPlacementType::PlayerSpawn;
    PlaneToolMode plane_tool = PlaneToolMode::None;
    bool entity_dropdown_open = false;
    bool has_hovered_committed_vertex = false;
    bool has_dragged_committed_vertex = false;
    bool dragged_draft_vertex_moved = false;
    bool dragged_committed_vertex_moved = false;
};

void frame_game_camera_on_sectors(GameCamera& camera, const std::vector<undecedent::SectorPlane>& sectors);
void set_game_projection(int width, int height, const GameCamera& camera);
void push_undo_snapshot(EditorWorld& editor_world, const char* label);
void push_undo_snapshot(EditorWorld& editor_world, EditorHistorySnapshot snapshot, const char* label);

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

float screen_to_world_x(const float screen_x, const int width, const EditorCamera& camera) {
    const float center_x = static_cast<float>(width) * 0.5F;
    return ((screen_x - center_x) / camera.zoom) + camera.x;
}

float screen_to_world_y(const float screen_y, const int height, const EditorCamera& camera) {
    const float center_y = static_cast<float>(height) * 0.5F;
    return ((screen_y - center_y) / camera.zoom) + camera.y;
}

float world_to_screen_x(const float world_x, const int width, const EditorCamera& camera) {
    const float center_x = static_cast<float>(width) * 0.5F;
    return center_x + ((world_x - camera.x) * camera.zoom);
}

float world_to_screen_y(const float world_y, const int height, const EditorCamera& camera) {
    const float center_y = static_cast<float>(height) * 0.5F;
    return center_y + ((world_y - camera.y) * camera.zoom);
}

float world_to_ndc_x(const float world_x, const int width, const EditorCamera& camera) {
    return screen_to_ndc_x(world_to_screen_x(world_x, width, camera), width);
}

float world_to_ndc_y(const float world_y, const int height, const EditorCamera& camera) {
    return screen_to_ndc_y(world_to_screen_y(world_y, height, camera), height);
}

float editor_grid_world_step(const float zoom) {
    float world_step = kEditorBaseGridWorldUnits;
    float screen_step = world_step * zoom;

    while (screen_step < kEditorMinGridPixels) {
        world_step *= 2.0F;
        screen_step *= 2.0F;
    }

    while (screen_step > kEditorMaxGridPixels && world_step > kEditorBaseGridWorldUnits) {
        world_step *= 0.5F;
        screen_step *= 0.5F;
    }

    return world_step;
}

undecedent::Vec2 snap_to_grid(const undecedent::Vec2 point, const float grid_step) {
    return undecedent::Vec2{
        std::round(point.x / grid_step) * grid_step,
        std::round(point.y / grid_step) * grid_step,
    };
}

undecedent::PolygonLoop draft_loop(const EditorWorld& editor_world) {
    return undecedent::PolygonLoop{editor_world.draft_vertices};
}

bool rebuild_sector(undecedent::SectorPlane& sector) {
    const undecedent::TriangulationResult result = undecedent::triangulate_polygon(sector.outer, sector.holes);
    sector.status = result.status;
    sector.status_message = result.message;
    sector.triangles = result.triangles;
    return result.status == undecedent::TriangulationStatus::Ok;
}

bool point_in_triangle(
    const undecedent::Vec2 p,
    const undecedent::Vec2 a,
    const undecedent::Vec2 b,
    const undecedent::Vec2 c
) {
    const auto cross2 = [](const undecedent::Vec2 p0, const undecedent::Vec2 p1, const undecedent::Vec2 p2) {
        return ((p1.x - p0.x) * (p2.y - p0.y)) - ((p1.y - p0.y) * (p2.x - p0.x));
    };
    const float ab = cross2(a, b, p);
    const float bc = cross2(b, c, p);
    const float ca = cross2(c, a, p);
    return (ab >= -undecedent::kGeometryEpsilon && bc >= -undecedent::kGeometryEpsilon &&
        ca >= -undecedent::kGeometryEpsilon) ||
        (ab <= undecedent::kGeometryEpsilon && bc <= undecedent::kGeometryEpsilon &&
            ca <= undecedent::kGeometryEpsilon);
}

float distance_squared(const undecedent::Vec2 a, const undecedent::Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool same_editor_point(const undecedent::Vec2 a, const undecedent::Vec2 b) {
    return distance_squared(a, b) <= (undecedent::kGeometryEpsilon * undecedent::kGeometryEpsilon);
}

void append_runtime_vertex(
    std::vector<RuntimeRenderVertex>& vertices,
    const undecedent::Vec3 point,
    const float r,
    const float g,
    const float b,
    const undecedent::Vec3 normal
) {
    vertices.push_back(RuntimeRenderVertex{point.x, point.y, point.z, r, g, b, normal.x, normal.y, normal.z});
}

undecedent::Vec3 runtime_triangle_lighting_normal(const undecedent::RuntimeTriangle& triangle) {
    const undecedent::Vec3 edge_ab{
        triangle.b.x - triangle.a.x,
        triangle.b.y - triangle.a.y,
        triangle.b.z - triangle.a.z,
    };
    const undecedent::Vec3 edge_ac{
        triangle.c.x - triangle.a.x,
        triangle.c.y - triangle.a.y,
        triangle.c.z - triangle.a.z,
    };
    undecedent::Vec3 normal{
        -((edge_ab.y * edge_ac.z) - (edge_ab.z * edge_ac.y)),
        -((edge_ab.z * edge_ac.x) - (edge_ab.x * edge_ac.z)),
        -((edge_ab.x * edge_ac.y) - (edge_ab.y * edge_ac.x)),
    };
    const float length = std::sqrt((normal.x * normal.x) + (normal.y * normal.y) + (normal.z * normal.z));
    if (length > 0.00001F) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
        return normal;
    }

    return undecedent::Vec3{0.0F, 1.0F, 0.0F};
}

void append_runtime_triangle(
    std::vector<RuntimeRenderVertex>& vertices,
    const undecedent::RuntimeTaggedTriangle& tagged_triangle
) {
    const undecedent::RuntimeTriangle& triangle = tagged_triangle.triangle;
    const undecedent::MaterialColor color = undecedent::material_color(tagged_triangle.material_id);
    const undecedent::Vec3 normal = runtime_triangle_lighting_normal(triangle);
    append_runtime_vertex(vertices, triangle.a, color.r, color.g, color.b, normal);
    append_runtime_vertex(vertices, triangle.b, color.r, color.g, color.b, normal);
    append_runtime_vertex(vertices, triangle.c, color.r, color.g, color.b, normal);
}

void rebuild_runtime_render_cache(RuntimeRenderCache& render_cache, const undecedent::RuntimeWorld& world) {
    std::vector<RuntimeRenderVertex> vertices;
    render_cache.sector_ranges.assign(world.sectors.size(), RuntimeRenderRange{});

    for (std::size_t sector_index = 0; sector_index < world.sectors.size(); ++sector_index) {
        RuntimeRenderRange range{};
        range.first_vertex = static_cast<GLint>(vertices.size());
        for (const undecedent::RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (tagged_triangle.sector_id != static_cast<int>(sector_index)) {
                continue;
            }
            append_runtime_triangle(vertices, tagged_triangle);
        }
        range.vertex_count = static_cast<GLsizei>(vertices.size()) - range.first_vertex;
        render_cache.sector_ranges[sector_index] = range;
    }

    if (render_cache.vertex_buffer == 0) {
        glGenBuffers(1, &render_cache.vertex_buffer);
    }

    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(RuntimeRenderVertex)),
        vertices.empty() ? nullptr : vertices.data(),
        GL_STATIC_DRAW
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    render_cache.total_vertices = static_cast<GLsizei>(vertices.size());
}

void destroy_runtime_render_cache(RuntimeRenderCache& render_cache) {
    if (render_cache.vertex_buffer != 0) {
        glDeleteBuffers(1, &render_cache.vertex_buffer);
    }
    render_cache = {};
}

bool is_sector_selected(const EditorWorld& editor_world, const int sector_index) {
    return editor_world.selected_sectors.contains(sector_index);
}

void clear_sector_selection(EditorWorld& editor_world) {
    editor_world.selected_sectors.clear();
    editor_world.selected_sector = -1;
}

void select_single_sector(EditorWorld& editor_world, const int sector_index) {
    editor_world.selected_sectors.clear();
    if (sector_index >= 0 && sector_index < static_cast<int>(editor_world.sectors.size())) {
        editor_world.selected_sectors.insert(sector_index);
        editor_world.selected_sector = sector_index;
    } else {
        editor_world.selected_sector = -1;
    }
}

void toggle_sector_selection(EditorWorld& editor_world, const int sector_index) {
    if (sector_index < 0 || sector_index >= static_cast<int>(editor_world.sectors.size())) {
        return;
    }

    if (editor_world.selected_sectors.contains(sector_index)) {
        editor_world.selected_sectors.erase(sector_index);
        editor_world.selected_sector = editor_world.selected_sectors.empty()
            ? -1
            : *editor_world.selected_sectors.rbegin();
    } else {
        editor_world.selected_sectors.insert(sector_index);
        editor_world.selected_sector = sector_index;
    }
}

std::vector<int> selected_sector_indices(const EditorWorld& editor_world) {
    return std::vector<int>(editor_world.selected_sectors.begin(), editor_world.selected_sectors.end());
}

bool sector_visible_in_slice(const EditorWorld& editor_world, const undecedent::SectorPlane& sector) {
    return undecedent::sector_intersects_z_slice(sector, editor_world.slice_z);
}

bool sector_on_active_floor(const EditorWorld& editor_world, const undecedent::SectorPlane& sector) {
    return undecedent::sector_floor_matches_slice(sector, editor_world.slice_z);
}

void clear_selection_outside_slice(EditorWorld& editor_world) {
    for (auto it = editor_world.selected_sectors.begin(); it != editor_world.selected_sectors.end();) {
        const int sector_index = *it;
        if (sector_index < 0 || sector_index >= static_cast<int>(editor_world.sectors.size()) ||
            !sector_visible_in_slice(editor_world, editor_world.sectors[static_cast<std::size_t>(sector_index)])) {
            it = editor_world.selected_sectors.erase(it);
        } else {
            ++it;
        }
    }

    if (editor_world.selected_sectors.empty()) {
        editor_world.selected_sector = -1;
    } else if (!editor_world.selected_sectors.contains(editor_world.selected_sector)) {
        editor_world.selected_sector = *editor_world.selected_sectors.rbegin();
    }
}

bool draft_contains_point(const EditorWorld& editor_world, const undecedent::Vec2 point) {
    return std::any_of(
        editor_world.draft_vertices.begin(),
        editor_world.draft_vertices.end(),
        [point](const undecedent::Vec2 vertex) {
            return same_editor_point(vertex, point);
        }
    );
}

bool draft_contains_point_except(
    const EditorWorld& editor_world,
    const undecedent::Vec2 point,
    const int ignored_index
) {
    for (std::size_t i = 0; i < editor_world.draft_vertices.size(); ++i) {
        if (static_cast<int>(i) == ignored_index) {
            continue;
        }

        if (same_editor_point(editor_world.draft_vertices[i], point)) {
            return true;
        }
    }
    return false;
}

int draft_vertex_at_screen(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y
) {
    int nearest = -1;
    float nearest_distance = kCloseVertexPixels * kCloseVertexPixels;

    for (std::size_t i = 0; i < editor_world.draft_vertices.size(); ++i) {
        const undecedent::Vec2 vertex = editor_world.draft_vertices[i];
        const float vertex_x = world_to_screen_x(vertex.x, width, camera);
        const float vertex_y = world_to_screen_y(vertex.y, height, camera);
        const float dx = vertex_x - screen_x;
        const float dy = vertex_y - screen_y;
        const float distance = (dx * dx) + (dy * dy);
        if (distance <= nearest_distance) {
            nearest = static_cast<int>(i);
            nearest_distance = distance;
        }
    }

    return nearest;
}

bool committed_vertex_at_screen(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    undecedent::Vec2& out_vertex
) {
    bool found = false;
    float nearest_distance = kCloseVertexPixels * kCloseVertexPixels;

    for (const undecedent::SectorPlane& sector : editor_world.sectors) {
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (const undecedent::Vec2 vertex : sector.outer.vertices) {
            const float vertex_x = world_to_screen_x(vertex.x, width, camera);
            const float vertex_y = world_to_screen_y(vertex.y, height, camera);
            const float dx = vertex_x - screen_x;
            const float dy = vertex_y - screen_y;
            const float distance = (dx * dx) + (dy * dy);
            if (distance <= nearest_distance) {
                found = true;
                nearest_distance = distance;
                out_vertex = vertex;
            }
        }
    }

    return found;
}

std::vector<CommittedVertexRef> matching_committed_vertices(
    const EditorWorld& editor_world,
    const undecedent::Vec2 point
) {
    std::vector<CommittedVertexRef> refs;
    for (std::size_t sector_index = 0; sector_index < editor_world.sectors.size(); ++sector_index) {
        const undecedent::SectorPlane& sector = editor_world.sectors[sector_index];
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (std::size_t vertex_index = 0; vertex_index < sector.outer.vertices.size(); ++vertex_index) {
            if (same_editor_point(sector.outer.vertices[vertex_index], point)) {
                refs.push_back(CommittedVertexRef{sector_index, vertex_index});
            }
        }
    }
    return refs;
}

bool is_dragged_committed_ref(
    const EditorWorld& editor_world,
    const std::size_t sector_index,
    const std::size_t vertex_index
) {
    return std::any_of(
        editor_world.dragged_committed_refs.begin(),
        editor_world.dragged_committed_refs.end(),
        [sector_index, vertex_index](const CommittedVertexRef ref) {
            return ref.sector == sector_index && ref.vertex == vertex_index;
        }
    );
}

void update_committed_vertex_hover(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    if (editor_world.plane_tool != PlaneToolMode::None || editor_world.has_dragged_committed_vertex) {
        editor_world.has_hovered_committed_vertex = false;
        return;
    }

    editor_world.has_hovered_committed_vertex = committed_vertex_at_screen(
        editor_world,
        camera,
        width,
        height,
        mouse_x,
        mouse_y,
        editor_world.hovered_committed_vertex
    );
}

void move_dragged_committed_vertices(EditorWorld& editor_world, const undecedent::Vec2 point) {
    for (const CommittedVertexRef ref : editor_world.dragged_committed_refs) {
        if (ref.sector >= editor_world.sectors.size()) {
            continue;
        }
        undecedent::SectorPlane& sector = editor_world.sectors[ref.sector];
        if (ref.vertex >= sector.outer.vertices.size()) {
            continue;
        }
        sector.outer.vertices[ref.vertex] = point;
        rebuild_sector(sector);
    }

    editor_world.dragged_committed_vertex = point;
}

void rebuild_runtime_geometry(EditorWorld& editor_world) {
    editor_world.runtime_world = undecedent::build_runtime_world(editor_world.sectors);
    rebuild_runtime_render_cache(editor_world.runtime_render_cache, editor_world.runtime_world);
}

EditorHistorySnapshot make_history_snapshot(const EditorWorld& editor_world) {
    return EditorHistorySnapshot{
        editor_world.sectors,
        editor_world.selected_sectors,
        editor_world.player_spawn,
        editor_world.point_lights,
        editor_world.slice_z,
        editor_world.selected_sector,
    };
}

void trim_history_stack(std::vector<EditorHistorySnapshot>& stack) {
    if (stack.size() > kEditorHistoryLimit) {
        stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(stack.size() - kEditorHistoryLimit));
    }
}

void push_undo_snapshot(EditorWorld& editor_world, EditorHistorySnapshot snapshot, const char* label) {
    editor_world.undo_stack.push_back(std::move(snapshot));
    trim_history_stack(editor_world.undo_stack);
    editor_world.redo_stack.clear();
    std::cout << "Undo checkpoint: " << label << '\n';
}

void push_undo_snapshot(EditorWorld& editor_world, const char* label) {
    push_undo_snapshot(editor_world, make_history_snapshot(editor_world), label);
}

void restore_history_snapshot(EditorWorld& editor_world, const EditorHistorySnapshot& snapshot) {
    editor_world.sectors = snapshot.sectors;
    editor_world.selected_sectors = snapshot.selected_sectors;
    editor_world.player_spawn = snapshot.player_spawn;
    editor_world.point_lights = snapshot.point_lights;
    editor_world.slice_z = snapshot.slice_z;
    editor_world.selected_sector = snapshot.selected_sector;
    editor_world.slice_scroll_remainder = 0.0F;
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.dragged_draft_vertex = -1;
    editor_world.dragged_draft_vertex_moved = false;
    editor_world.committed_drag_snapshot.clear();
    editor_world.dragged_committed_refs.clear();
    editor_world.has_dragged_committed_vertex = false;
    editor_world.dragged_committed_vertex_moved = false;
    editor_world.has_hovered_committed_vertex = false;
    editor_world.plane_tool = PlaneToolMode::None;
    clear_selection_outside_slice(editor_world);
    rebuild_runtime_geometry(editor_world);
}

bool undo_editor_action(EditorWorld& editor_world) {
    if (editor_world.has_dragged_committed_vertex) {
        std::cout << "Release the dragged vertex before undoing.\n";
        return false;
    }

    if (editor_world.undo_stack.empty()) {
        std::cout << "Nothing to undo.\n";
        return false;
    }

    editor_world.redo_stack.push_back(make_history_snapshot(editor_world));
    trim_history_stack(editor_world.redo_stack);
    const EditorHistorySnapshot snapshot = std::move(editor_world.undo_stack.back());
    editor_world.undo_stack.pop_back();
    restore_history_snapshot(editor_world, snapshot);
    std::cout << "Undo.\n";
    return true;
}

bool redo_editor_action(EditorWorld& editor_world) {
    if (editor_world.has_dragged_committed_vertex) {
        std::cout << "Release the dragged vertex before redoing.\n";
        return false;
    }

    if (editor_world.redo_stack.empty()) {
        std::cout << "Nothing to redo.\n";
        return false;
    }

    editor_world.undo_stack.push_back(make_history_snapshot(editor_world));
    trim_history_stack(editor_world.undo_stack);
    const EditorHistorySnapshot snapshot = std::move(editor_world.redo_stack.back());
    editor_world.redo_stack.pop_back();
    restore_history_snapshot(editor_world, snapshot);
    std::cout << "Redo.\n";
    return true;
}

undecedent::PlayerPhysicsConfig player_physics_config() {
    return undecedent::PlayerPhysicsConfig{kPlayerRadius, kPlayerHeight, kPlayerEyeHeight};
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
    if (!undecedent::player_fits_at(editor_world.runtime_world, spawn.position, player_physics_config())) {
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
        undecedent::player_fits_at(world, eye, player_physics_config()) &&
        !undecedent::player_fits_at(
            world,
            undecedent::Vec3{eye.x, eye.y - kPlayerGroundProbe, eye.z},
            player_physics_config()
        );
}

undecedent::PointLight default_point_light_at(const undecedent::Vec3 position) {
    undecedent::PointLight light;
    light.position = position;
    return light;
}

void place_entity(EditorWorld& editor_world, const undecedent::Vec3 position, const float yaw = 0.0F) {
    push_undo_snapshot(editor_world, "place entity");
    switch (editor_world.entity_placement) {
    case EntityPlacementType::PlayerSpawn:
        editor_world.player_spawn.position = position;
        editor_world.player_spawn.yaw = yaw;
        editor_world.player_spawn.set = true;
        std::cout << "Placed player spawn at " << position.x << ", " << position.y << ", " << position.z << '\n';
        break;
    case EntityPlacementType::PointLight:
        editor_world.point_lights.push_back(default_point_light_at(position));
        std::cout << "Placed point light at " << position.x << ", " << position.y << ", " << position.z << '\n';
        break;
    }
}

const char* entity_placement_label(const EntityPlacementType type) {
    switch (type) {
    case EntityPlacementType::PlayerSpawn: return "PLAYER SPAWN";
    case EntityPlacementType::PointLight: return "POINT LIGHT";
    }
    return "ENTITY";
}

void normalize_sector_materials(undecedent::SectorPlane& sector) {
    sector.floor_material = undecedent::clamped_material_id(sector.floor_material);
    sector.ceiling_material = undecedent::clamped_material_id(sector.ceiling_material);
    sector.wall_materials.resize(sector.outer.vertices.size(), undecedent::kDefaultMaterialId);
    for (int& material : sector.wall_materials) {
        material = undecedent::clamped_material_id(material);
    }
    sector.hole_wall_materials.resize(sector.holes.size());
    for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
        sector.hole_wall_materials[hole_index].resize(
            sector.holes[hole_index].vertices.size(),
            undecedent::kDefaultMaterialId
        );
        for (int& material : sector.hole_wall_materials[hole_index]) {
            material = undecedent::clamped_material_id(material);
        }
    }
}

bool apply_material_to_surface(EditorWorld& editor_world, const SurfacePick& pick, const int material_id) {
    if (!pick.hit || pick.sector_id < 0 || pick.sector_id >= static_cast<int>(editor_world.sectors.size())) {
        return false;
    }

    undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(pick.sector_id)];
    normalize_sector_materials(sector);
    const int clamped = undecedent::clamped_material_id(material_id);
    bool changed = false;
    switch (pick.surface.kind) {
    case undecedent::RuntimeSurfaceKind::Floor:
        changed = sector.floor_material != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.floor_material = clamped;
        break;
    case undecedent::RuntimeSurfaceKind::Ceiling:
        changed = sector.ceiling_material != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.ceiling_material = clamped;
        break;
    case undecedent::RuntimeSurfaceKind::Wall:
        if (pick.surface.index < 0 || pick.surface.index >= static_cast<int>(sector.wall_materials.size())) {
            return false;
        }
        changed = sector.wall_materials[static_cast<std::size_t>(pick.surface.index)] != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.wall_materials[static_cast<std::size_t>(pick.surface.index)] = clamped;
        break;
    case undecedent::RuntimeSurfaceKind::HoleWall:
        if (pick.surface.index < 0 || pick.surface.index >= static_cast<int>(sector.hole_wall_materials.size())) {
            return false;
        }
        if (pick.surface.sub_index < 0 ||
            pick.surface.sub_index >= static_cast<int>(sector.hole_wall_materials[static_cast<std::size_t>(pick.surface.index)].size())) {
            return false;
        }
        changed = sector.hole_wall_materials[static_cast<std::size_t>(pick.surface.index)]
            [static_cast<std::size_t>(pick.surface.sub_index)] != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.hole_wall_materials[static_cast<std::size_t>(pick.surface.index)]
            [static_cast<std::size_t>(pick.surface.sub_index)] = clamped;
        break;
    }

    rebuild_runtime_geometry(editor_world);
    return true;
}

void finish_committed_vertex_drag(EditorWorld& editor_world) {
    if (!editor_world.has_dragged_committed_vertex) {
        return;
    }

    if (editor_world.dragged_committed_vertex_moved) {
        const undecedent::CsgAddResult rebuild_result = undecedent::csg_rebuild_sectors(editor_world.sectors);
        if (rebuild_result.ok) {
            EditorHistorySnapshot snapshot = make_history_snapshot(editor_world);
            snapshot.sectors = editor_world.committed_drag_snapshot;
            push_undo_snapshot(editor_world, std::move(snapshot), "vertex move");
            editor_world.sectors = rebuild_result.sectors;
            select_single_sector(editor_world, editor_world.selected_sector);
            clear_selection_outside_slice(editor_world);
            rebuild_runtime_geometry(editor_world);
            std::cout << "Rebuilt sector topology after vertex move; sectors: " << editor_world.sectors.size() << '\n';
        } else {
            editor_world.sectors = editor_world.committed_drag_snapshot;
            std::cout << "Rejected vertex move: " << rebuild_result.message << '\n';
        }
    }

    editor_world.committed_drag_snapshot.clear();
    editor_world.dragged_committed_refs.clear();
    editor_world.has_dragged_committed_vertex = false;
    editor_world.dragged_committed_vertex_moved = false;
}

int sector_at_point(const EditorWorld& editor_world, const undecedent::Vec2 point) {
    for (int i = static_cast<int>(editor_world.sectors.size()) - 1; i >= 0; --i) {
        const undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(i)];
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (const undecedent::Triangle& triangle : sector.triangles) {
            if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
                return i;
            }
        }
    }
    return -1;
}

bool point_in_sector(const undecedent::SectorPlane& sector, const undecedent::Vec2 point) {
    for (const undecedent::Triangle& triangle : sector.triangles) {
        if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
            return true;
        }
    }
    return false;
}

undecedent::TriangulationResult draft_preview_result(const EditorWorld& editor_world) {
    if (editor_world.plane_tool == PlaneToolMode::None) {
        return {};
    }

    if (editor_world.dragged_draft_vertex >= 0) {
        return editor_world.draft_result;
    }

    std::vector<undecedent::Vec2> preview_vertices = editor_world.draft_vertices;
    const bool closes_loop =
        preview_vertices.size() >= 3 &&
        same_editor_point(editor_world.snapped_mouse, preview_vertices.front());

    if (!closes_loop) {
        if (draft_contains_point(editor_world, editor_world.snapped_mouse)) {
            return undecedent::TriangulationResult{
                undecedent::TriangulationStatus::DuplicateVertex,
                "Preview vertex duplicates an existing vertex.",
                {}
            };
        }

        preview_vertices.push_back(editor_world.snapped_mouse);
    }

    if (preview_vertices.size() < 3) {
        return undecedent::TriangulationResult{
            undecedent::TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
    }

    const undecedent::PolygonLoop preview_loop{preview_vertices};
    if (editor_world.plane_tool == PlaneToolMode::DrawOuter ||
        editor_world.plane_tool == PlaneToolMode::DrawHole) {
        return undecedent::triangulate_polygon(preview_loop);
    }

    return {};
}

void refresh_draft(EditorWorld& editor_world) {
    if (editor_world.draft_vertices.size() < 3) {
        editor_world.draft_result = undecedent::TriangulationResult{
            undecedent::TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
        return;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter ||
        editor_world.plane_tool == PlaneToolMode::DrawHole) {
        editor_world.draft_result = undecedent::triangulate_polygon(draft_loop(editor_world));
        return;
    }

    editor_world.draft_result = undecedent::TriangulationResult{
        undecedent::TriangulationStatus::TriangulationFailed,
        "No active plane tool.",
        {}
    };
}

void update_snapped_mouse(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    const undecedent::Vec2 world{
        screen_to_world_x(mouse_x, width, camera),
        screen_to_world_y(mouse_y, height, camera),
    };
    editor_world.snapped_mouse = snap_to_grid(world, editor_grid_world_step(camera.zoom));
}

void cancel_plane_tool(EditorWorld& editor_world) {
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.dragged_draft_vertex = -1;
    editor_world.dragged_draft_vertex_moved = false;
    editor_world.plane_tool = PlaneToolMode::None;
}

void start_outer_plane(EditorWorld& editor_world) {
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.plane_tool = PlaneToolMode::DrawOuter;
    std::cout << "Plane tool: draw outer loop\n";
}

void start_hole_plane(EditorWorld& editor_world) {
    const bool has_active_floor_sector = std::any_of(
        editor_world.sectors.begin(),
        editor_world.sectors.end(),
        [&editor_world](const undecedent::SectorPlane& sector) {
            return sector_on_active_floor(editor_world, sector);
        }
    );
    if (!has_active_floor_sector) {
        std::cout << "Create a sector on this Z slice before drawing a hole.\n";
        return;
    }

    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.plane_tool = PlaneToolMode::DrawHole;
    std::cout << "Plane tool: draw hole loop\n";
}

bool commit_plane_tool(EditorWorld& editor_world) {
    refresh_draft(editor_world);
    if (editor_world.draft_result.status != undecedent::TriangulationStatus::Ok) {
        std::cout << "Cannot commit loop: " << editor_world.draft_result.message << '\n';
        return false;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        const undecedent::CsgAddResult csg_result =
            undecedent::csg_add_sector_at_floor(
                editor_world.sectors,
                draft_loop(editor_world),
                editor_world.slice_z
            );
        if (!csg_result.ok) {
            std::cout << "Cannot commit CSG add: " << csg_result.message << '\n';
            return false;
        }

        push_undo_snapshot(editor_world, "CSG add");
        editor_world.sectors = csg_result.sectors;
        select_single_sector(
            editor_world,
            editor_world.sectors.empty() ? -1 : static_cast<int>(editor_world.sectors.size()) - 1
        );
        clear_selection_outside_slice(editor_world);
        rebuild_runtime_geometry(editor_world);
        cancel_plane_tool(editor_world);
        std::cout << "Committed CSG add; sectors: " << editor_world.sectors.size() << '\n';
        return true;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawHole) {
        const undecedent::CsgAddResult csg_result =
            undecedent::csg_subtract_sector_at_floor(
                editor_world.sectors,
                draft_loop(editor_world),
                editor_world.slice_z
            );
        if (!csg_result.ok) {
            std::cout << "Cannot commit CSG subtract: " << csg_result.message << '\n';
            return false;
        }

        push_undo_snapshot(editor_world, "CSG subtract");
        editor_world.sectors = csg_result.sectors;
        select_single_sector(
            editor_world,
            editor_world.sectors.empty()
                ? -1
                : std::min(editor_world.selected_sector, static_cast<int>(editor_world.sectors.size()) - 1)
        );
        clear_selection_outside_slice(editor_world);
        rebuild_runtime_geometry(editor_world);
        cancel_plane_tool(editor_world);
        std::cout << "Committed CSG subtract; sectors: " << editor_world.sectors.size() << '\n';
        return true;
    }

    return false;
}

bool merge_selected_sectors(EditorWorld& editor_world) {
    const std::vector<int> selected = selected_sector_indices(editor_world);
    if (selected.size() < 2) {
        std::cout << "Select at least two connected sectors before pressing M.\n";
        return false;
    }

    const undecedent::CsgAddResult merge_result = undecedent::csg_merge_sectors(editor_world.sectors, selected);
    if (!merge_result.ok) {
        std::cout << "Cannot merge sectors: " << merge_result.message << '\n';
        return false;
    }

    push_undo_snapshot(editor_world, "merge sectors");
    editor_world.sectors = merge_result.sectors;
    select_single_sector(
        editor_world,
        editor_world.sectors.empty() ? -1 : static_cast<int>(editor_world.sectors.size()) - 1
    );
    rebuild_runtime_geometry(editor_world);
    std::cout << "Merged sectors; sectors: " << editor_world.sectors.size() << '\n';
    return true;
}

bool delete_selected_sectors(EditorWorld& editor_world) {
    if (editor_world.plane_tool != PlaneToolMode::None) {
        std::cout << "Finish or cancel the active plane tool before deleting sectors.\n";
        return false;
    }

    if (editor_world.has_dragged_committed_vertex) {
        std::cout << "Release the dragged vertex before deleting sectors.\n";
        return false;
    }

    const std::vector<int> selected = selected_sector_indices(editor_world);
    if (selected.empty()) {
        std::cout << "Select at least one sector before pressing Delete.\n";
        return false;
    }

    const undecedent::CsgAddResult delete_result = undecedent::csg_delete_sectors(editor_world.sectors, selected);
    if (!delete_result.ok) {
        std::cout << "Cannot delete sectors: " << delete_result.message << '\n';
        return false;
    }

    push_undo_snapshot(editor_world, "delete sectors");
    const std::size_t deleted_count = editor_world.sectors.size() - delete_result.sectors.size();
    editor_world.sectors = delete_result.sectors;
    clear_sector_selection(editor_world);
    rebuild_runtime_geometry(editor_world);
    std::cout << "Deleted sectors: " << deleted_count << "; sectors: " << editor_world.sectors.size() << '\n';
    return true;
}

bool adjust_selected_sector_heights(EditorWorld& editor_world, const float delta) {
    if (editor_world.selected_sectors.empty()) {
        std::cout << "Select at least one sector before adjusting height.\n";
        return false;
    }

    std::vector<int> targets;
    for (const int sector_index : editor_world.selected_sectors) {
        if (sector_index < 0 || sector_index >= static_cast<int>(editor_world.sectors.size())) {
            continue;
        }

        const undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
        if (std::max(kSectorMinHeight, sector.height + delta) != sector.height) {
            targets.push_back(sector_index);
        }
    }

    if (targets.empty()) {
        std::cout << "Selected sector height is already at the limit.\n";
        return false;
    }

    push_undo_snapshot(editor_world, "ceiling height");
    for (const int sector_index : targets) {
        undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
        sector.height = std::max(kSectorMinHeight, sector.height + delta);
    }

    rebuild_runtime_geometry(editor_world);
    clear_selection_outside_slice(editor_world);
    std::cout << "Adjusted sector height by " << delta << " for " << targets.size() << " sectors.\n";
    return true;
}

bool adjust_selected_sector_floor_heights(EditorWorld& editor_world, const float delta) {
    if (editor_world.selected_sectors.empty()) {
        std::cout << "Select at least one sector before adjusting floor height.\n";
        return false;
    }

    std::vector<int> targets;
    for (const int sector_index : editor_world.selected_sectors) {
        if (sector_index < 0 || sector_index >= static_cast<int>(editor_world.sectors.size())) {
            continue;
        }

        targets.push_back(sector_index);
    }

    if (targets.empty()) {
        std::cout << "No valid selected sectors to adjust.\n";
        return false;
    }

    push_undo_snapshot(editor_world, "floor height");
    for (const int sector_index : targets) {
        undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
        sector.floor_height += delta;
    }

    rebuild_runtime_geometry(editor_world);
    clear_selection_outside_slice(editor_world);
    std::cout << "Adjusted sector floor by " << delta << " for " << targets.size() << " sectors.\n";
    return true;
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

std::uint64_t scale_indicator_world_units(const float zoom) {
    const double safe_zoom = std::max(static_cast<double>(zoom), static_cast<double>(kEditorMinZoom));
    const double target_world_units = std::max(1.0, kScaleIndicatorTargetPixels / safe_zoom);
    double world_units = std::pow(2.0, std::floor(std::log2(target_world_units)));
    double screen_pixels = world_units * safe_zoom;

    while (screen_pixels < kScaleIndicatorMinPixels) {
        world_units *= 2.0;
        screen_pixels *= 2.0;
    }

    while (screen_pixels > kScaleIndicatorMaxPixels && world_units > 1.0) {
        world_units *= 0.5;
        screen_pixels *= 0.5;
    }

    return static_cast<std::uint64_t>(std::max(1.0, world_units));
}

float editor_scroll_zoom_delta(const float scroll_y) {
    const float magnitude = std::abs(scroll_y);
    if (magnitude <= kEditorScrollDeadzone) {
        return 0.0F;
    }

    const float clamped = std::min(magnitude, kEditorMaxScrollDelta);
    const float curved = std::log1p(clamped * kEditorScrollLogStrength) / std::log1p(kEditorScrollLogStrength);
    const float sign = scroll_y < 0.0F ? -1.0F : 1.0F;
    return sign * curved * kEditorScrollSensitivity;
}

std::string format_world_units(const float value) {
    const float rounded = std::round(value);
    if (std::abs(value - rounded) <= 0.001F) {
        return std::to_string(static_cast<int>(rounded));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

void apply_editor_slice_scroll(EditorWorld& editor_world, const EditorCamera& camera, const float scroll_y) {
    const float scroll_delta = editor_scroll_zoom_delta(scroll_y);
    if (scroll_delta == 0.0F) {
        return;
    }

    editor_world.slice_scroll_remainder += scroll_delta;
    const int whole_steps = static_cast<int>(editor_world.slice_scroll_remainder);
    if (whole_steps == 0) {
        return;
    }

    editor_world.slice_scroll_remainder -= static_cast<float>(whole_steps);
    const float grid_step = editor_grid_world_step(camera.target_zoom);
    editor_world.slice_z =
        std::round((editor_world.slice_z + (static_cast<float>(whole_steps) * grid_step)) / grid_step) * grid_step;
    clear_selection_outside_slice(editor_world);
    std::cout << "Editor Z slice: " << format_world_units(editor_world.slice_z) << "u\n";
}

void draw_stroke_segment(
    const int segment,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    const float w = size;
    const float h = size * 1.65F;
    const float mid_y = y + h * 0.5F;
    const float right_x = x + w;
    const float bottom_y = y + h;

    switch (segment) {
        case 0: draw_screen_line(x, y, right_x, y, width, height); break;
        case 1: draw_screen_line(right_x, y, right_x, mid_y, width, height); break;
        case 2: draw_screen_line(right_x, mid_y, right_x, bottom_y, width, height); break;
        case 3: draw_screen_line(x, bottom_y, right_x, bottom_y, width, height); break;
        case 4: draw_screen_line(x, mid_y, x, bottom_y, width, height); break;
        case 5: draw_screen_line(x, y, x, mid_y, width, height); break;
        case 6: draw_screen_line(x, mid_y, right_x, mid_y, width, height); break;
        default: break;
    }
}

void draw_stroke_digit(
    const char digit,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    constexpr unsigned char masks[] = {
        0b00111111,
        0b00000110,
        0b01011011,
        0b01001111,
        0b01100110,
        0b01101101,
        0b01111101,
        0b00000111,
        0b01111111,
        0b01101111,
    };

    const unsigned char mask = masks[digit - '0'];
    for (int segment = 0; segment < 7; ++segment) {
        if ((mask & (1 << segment)) != 0) {
            draw_stroke_segment(segment, x, y, size, width, height);
        }
    }
}

void draw_stroke_u(
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    draw_stroke_segment(2, x, y, size, width, height);
    draw_stroke_segment(3, x, y, size, width, height);
    draw_stroke_segment(4, x, y, size, width, height);
}

void draw_stroke_letter(
    const char letter,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    const float glyph_h = size * 1.65F;
    const auto line = [&](const float x1, const float y1, const float x2, const float y2) {
        draw_screen_line(
            x + (size * x1),
            y + (glyph_h * y1),
            x + (size * x2),
            y + (glyph_h * y2),
            width,
            height
        );
    };

    switch (letter) {
        case 'A':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            break;
        case 'B':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.82F, 0.0F);
            line(0.82F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.82F, 0.5F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            line(0.82F, 0.5F, 1.0F, 0.62F);
            line(1.0F, 0.62F, 1.0F, 0.88F);
            line(1.0F, 0.88F, 0.82F, 1.0F);
            line(0.0F, 1.0F, 0.82F, 1.0F);
            break;
        case 'C':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'D':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.78F, 0.0F);
            line(0.78F, 0.0F, 1.0F, 0.2F);
            line(1.0F, 0.2F, 1.0F, 0.8F);
            line(1.0F, 0.8F, 0.78F, 1.0F);
            line(0.0F, 1.0F, 0.78F, 1.0F);
            break;
        case 'E':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'F':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            break;
        case 'G':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.55F);
            line(1.0F, 0.55F, 0.56F, 0.55F);
            break;
        case 'H':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            break;
        case 'I':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.5F, 0.0F, 0.5F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'J':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.72F, 0.0F, 0.72F, 0.82F);
            line(0.72F, 0.82F, 0.54F, 1.0F);
            line(0.54F, 1.0F, 0.12F, 1.0F);
            line(0.12F, 1.0F, 0.0F, 0.84F);
            break;
        case 'K':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(1.0F, 0.0F, 0.0F, 0.55F);
            line(0.0F, 0.55F, 1.0F, 1.0F);
            break;
        case 'L':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'M':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.5F, 0.42F);
            line(0.5F, 0.42F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            break;
        case 'N':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            break;
        case 'O':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 0.0F, 0.0F);
            break;
        case 'P':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.86F, 0.0F);
            line(0.86F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.86F, 0.5F);
            line(0.0F, 0.5F, 0.86F, 0.5F);
            break;
        case 'Q':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 0.82F);
            line(1.0F, 0.82F, 0.82F, 1.0F);
            line(0.82F, 1.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.56F, 0.68F, 1.0F, 1.08F);
            break;
        case 'R':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.84F, 0.0F);
            line(0.84F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.84F, 0.5F);
            line(0.0F, 0.5F, 0.84F, 0.5F);
            line(0.42F, 0.5F, 1.0F, 1.0F);
            break;
        case 'S':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 0.5F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            line(1.0F, 0.5F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 0.0F, 1.0F);
            break;
        case 'T':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.5F, 0.0F, 0.5F, 1.0F);
            break;
        case 'U':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            break;
        case 'V':
            line(0.0F, 0.0F, 0.5F, 1.0F);
            line(0.5F, 1.0F, 1.0F, 0.0F);
            break;
        case 'W':
            line(0.0F, 0.0F, 0.22F, 1.0F);
            line(0.22F, 1.0F, 0.5F, 0.58F);
            line(0.5F, 0.58F, 0.78F, 1.0F);
            line(0.78F, 1.0F, 1.0F, 0.0F);
            break;
        case 'X':
            line(0.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 0.0F, 0.0F, 1.0F);
            break;
        case 'Y':
            line(0.0F, 0.0F, 0.5F, 0.5F);
            line(1.0F, 0.0F, 0.5F, 0.5F);
            line(0.5F, 0.5F, 0.5F, 1.0F);
            break;
        case 'Z':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        default:
            break;
    }
}

void draw_stroke_text(
    const std::string& label,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    float cursor_x = x;
    for (const char ch : label) {
        if (ch >= '0' && ch <= '9') {
            draw_stroke_digit(ch, cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch >= 'A' && ch <= 'Z') {
            draw_stroke_letter(ch, cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch == 'u') {
            draw_stroke_u(cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch == '.') {
            draw_screen_line(
                cursor_x + size * 0.35F,
                y + size * 1.55F,
                cursor_x + size * 0.38F,
                y + size * 1.55F,
                width,
                height
            );
            cursor_x += size * 0.75F;
        } else if (ch == '/') {
            draw_screen_line(cursor_x, y + size * 1.65F, cursor_x + size, y, width, height);
            cursor_x += size * 1.05F;
        } else if (ch == '-') {
            draw_screen_line(cursor_x, y + size * 0.82F, cursor_x + size, y + size * 0.82F, width, height);
            cursor_x += size * 1.05F;
        } else {
            cursor_x += size * 0.85F;
        }
    }
}

void draw_scale_label(
    const std::string& label,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    draw_stroke_text(label, x, y, size, width, height);
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

void draw_material_selector(const int active_material, const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const float swatch = 24.0F;
    const float gap = 7.0F;
    const float x = 16.0F;
    const float y = static_cast<float>(height) - 46.0F;
    const float box_width = (swatch * static_cast<float>(undecedent::kMaterialCount)) +
        (gap * static_cast<float>(undecedent::kMaterialCount - 1)) + 18.0F;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.40F);
    draw_screen_quad(10.0F, y - 8.0F, box_width, swatch + 22.0F, width, height);
    for (int i = 0; i < undecedent::kMaterialCount; ++i) {
        const undecedent::MaterialColor color = undecedent::material_color(i);
        glColor4f(color.r, color.g, color.b, 0.94F);
        draw_screen_quad(x + (static_cast<float>(i) * (swatch + gap)), y, swatch, swatch, width, height);
    }
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    for (int i = 0; i < undecedent::kMaterialCount; ++i) {
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

bool point_in_rect(const float px, const float py, const float x, const float y, const float w, const float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
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

void apply_editor_scroll_zoom(
    EditorCamera& camera,
    const float scroll_y,
    const float mouse_x,
    const float mouse_y,
    const int width,
    const int height
) {
    if (scroll_y == 0.0F || width <= 0 || height <= 0) {
        return;
    }

    EditorCamera target_camera = camera;
    target_camera.x = camera.target_x;
    target_camera.y = camera.target_y;
    target_camera.zoom = camera.target_zoom;

    const float before_x = screen_to_world_x(mouse_x, width, target_camera);
    const float before_y = screen_to_world_y(mouse_y, height, target_camera);
    const float scroll_delta = editor_scroll_zoom_delta(scroll_y);
    if (scroll_delta == 0.0F) {
        return;
    }

    const float zoom_factor = std::pow(kEditorZoomStep, scroll_delta);
    target_camera.zoom = std::clamp(target_camera.zoom * zoom_factor, kEditorMinZoom, kEditorMaxZoom);
    const float after_x = screen_to_world_x(mouse_x, width, target_camera);
    const float after_y = screen_to_world_y(mouse_y, height, target_camera);

    camera.target_zoom = target_camera.zoom;
    camera.target_x += before_x - after_x;
    camera.target_y += before_y - after_y;
}

void update_editor_camera(EditorCamera& camera, const float dt) {
    const float t = 1.0F - std::exp(-kEditorCameraSmoothRate * dt);
    camera.x += (camera.target_x - camera.x) * t;
    camera.y += (camera.target_y - camera.y) * t;

    const float current_zoom_log = std::log(camera.zoom);
    const float target_zoom_log = std::log(camera.target_zoom);
    camera.zoom = std::exp(current_zoom_log + ((target_zoom_log - current_zoom_log) * t));
}

void update_game_camera_look(GameCamera& camera, const float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    if (keys[SDL_SCANCODE_LEFT]) {
        camera.yaw += kGameLookSpeed * dt;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        camera.yaw -= kGameLookSpeed * dt;
    }
    if (keys[SDL_SCANCODE_UP]) {
        camera.pitch = std::min(camera.pitch + kGameLookSpeed * dt, 1.45F);
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        camera.pitch = std::max(camera.pitch - kGameLookSpeed * dt, -1.45F);
    }
}

void update_game_camera(GameCamera& camera, const float dt) {
    update_game_camera_look(camera, dt);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    const float forward_flat = std::cos(camera.pitch);
    const float forward_x = -std::sin(camera.yaw) * forward_flat;
    const float forward_y = std::sin(camera.pitch);
    const float forward_z = -std::cos(camera.yaw) * forward_flat;
    const float strafe_x = std::cos(camera.yaw);
    const float strafe_z = -std::sin(camera.yaw);
    const float step = kGameMoveSpeed * dt;

    if (keys[SDL_SCANCODE_W]) {
        camera.x += forward_x * step;
        camera.y += forward_y * step;
        camera.z += forward_z * step;
    }
    if (keys[SDL_SCANCODE_S]) {
        camera.x -= forward_x * step;
        camera.y -= forward_y * step;
        camera.z -= forward_z * step;
    }
    if (keys[SDL_SCANCODE_A]) {
        camera.x -= strafe_x * step;
        camera.z -= strafe_z * step;
    }
    if (keys[SDL_SCANCODE_D]) {
        camera.x += strafe_x * step;
        camera.z += strafe_z * step;
    }
    if (keys[SDL_SCANCODE_SPACE]) {
        camera.y += step;
    }
    if (keys[SDL_SCANCODE_C]) {
        camera.y -= step;
    }
}

void update_playtest_camera(
    GameCamera& camera,
    const undecedent::RuntimeWorld& world,
    PlaytestPlayerState& playtest_state,
    const float dt
) {
    update_game_camera_look(camera, dt);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    const float forward_x = -std::sin(camera.yaw);
    const float forward_z = -std::cos(camera.yaw);
    const float strafe_x = std::cos(camera.yaw);
    const float strafe_z = -std::sin(camera.yaw);
    const float step = kGameMoveSpeed * dt;

    undecedent::Vec3 delta{};
    if (keys[SDL_SCANCODE_W]) {
        delta.x += forward_x * step;
        delta.z += forward_z * step;
    }
    if (keys[SDL_SCANCODE_S]) {
        delta.x -= forward_x * step;
        delta.z -= forward_z * step;
    }
    if (keys[SDL_SCANCODE_A]) {
        delta.x -= strafe_x * step;
        delta.z -= strafe_z * step;
    }
    if (keys[SDL_SCANCODE_D]) {
        delta.x += strafe_x * step;
        delta.z += strafe_z * step;
    }

    const float horizontal_length = std::sqrt((delta.x * delta.x) + (delta.z * delta.z));
    if (horizontal_length > step && horizontal_length > 0.0F) {
        const float scale = step / horizontal_length;
        delta.x *= scale;
        delta.z *= scale;
    }

    const undecedent::Vec3 eye{camera.x, camera.y, camera.z};
    const bool fits_now = undecedent::player_fits_at(world, eye, player_physics_config());
    const bool grounded_now = fits_now &&
        !undecedent::player_fits_at(
            world,
            undecedent::Vec3{eye.x, eye.y - kPlayerGroundProbe, eye.z},
            player_physics_config()
        );
    const bool jump_down = keys[SDL_SCANCODE_SPACE];
    const bool jump_pressed = jump_down && !playtest_state.jump_was_down;
    playtest_state.jump_was_down = jump_down;
    playtest_state.grounded = grounded_now;

    if (jump_pressed && playtest_state.grounded) {
        playtest_state.vertical_velocity = kPlayerJumpVelocity;
        playtest_state.grounded = false;
    } else if (playtest_state.grounded && playtest_state.vertical_velocity < 0.0F) {
        playtest_state.vertical_velocity = 0.0F;
    }

    playtest_state.vertical_velocity -= kPlayerGravity * dt;
    playtest_state.vertical_velocity *= std::exp(-kPlayerGravityDrag * dt);
    playtest_state.vertical_velocity = std::max(playtest_state.vertical_velocity, -kPlayerTerminalFallSpeed);
    delta.y = playtest_state.vertical_velocity * dt;

    undecedent::PlayerPhysicsState state{
        undecedent::Vec3{camera.x, camera.y, camera.z},
        -1,
    };
    const float old_y = state.position.y;
    state = undecedent::move_player(world, state, delta, player_physics_config());
    camera.x = state.position.x;
    camera.y = state.position.y;
    camera.z = state.position.z;

    const bool blocked_vertically = std::abs(camera.y - old_y) <= undecedent::kGeometryEpsilon &&
        std::abs(delta.y) > undecedent::kGeometryEpsilon;
    if (blocked_vertically) {
        if (delta.y < 0.0F) {
            playtest_state.grounded = true;
        }
        playtest_state.vertical_velocity = 0.0F;
    }
}

undecedent::Vec3 camera_forward(const GameCamera& camera) {
    const float forward_flat = std::cos(camera.pitch);
    return undecedent::Vec3{
        -std::sin(camera.yaw) * forward_flat,
        std::sin(camera.pitch),
        -std::cos(camera.yaw) * forward_flat,
    };
}

undecedent::Vec3 camera_ray_direction(
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y
) {
    const float ndc_x = (2.0F * screen_x / static_cast<float>(width)) - 1.0F;
    const float ndc_y = 1.0F - (2.0F * screen_y / static_cast<float>(height));
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float tan_y = std::tan((70.0F * kPi / 180.0F) * 0.5F);
    const undecedent::Vec3 forward = normalize_vec3(camera_forward(camera));
    const undecedent::Vec3 right = undecedent::Vec3{std::cos(camera.yaw), 0.0F, -std::sin(camera.yaw)};
    const undecedent::Vec3 up = normalize_vec3(cross_vec3(right, forward));
    return normalize_vec3(add_vec3(
        add_vec3(forward, mul_vec3(right, ndc_x * tan_y * aspect)),
        mul_vec3(up, ndc_y * tan_y)
    ));
}

SurfacePick pick_runtime_surface(
    const undecedent::RuntimeWorld& world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y
) {
    if (width <= 0 || height <= 0) {
        return {};
    }

    const undecedent::Vec3 direction = camera_ray_direction(camera, width, height, screen_x, screen_y);
    const undecedent::Vec3 origin{camera.x, camera.y, camera.z};

    SurfacePick best{};
    best.distance = std::numeric_limits<float>::max();
    for (const undecedent::RuntimeTaggedTriangle& tagged : world.triangles) {
        float t = 0.0F;
        if (tagged.sector_id < 0 || !ray_triangle_intersection(origin, direction, tagged.triangle, t)) {
            continue;
        }
        if (t < best.distance) {
            best.hit = true;
            best.distance = t;
            best.point = add_vec3(origin, mul_vec3(direction, t));
            best.normal = runtime_triangle_lighting_normal(tagged.triangle);
            best.sector_id = tagged.sector_id;
            best.material_id = tagged.material_id;
            best.surface = tagged.surface;
        }
    }

    return best;
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

void draw_editor_grid(const int width, const int height, const EditorCamera& camera) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0F);

    const float grid_step = editor_grid_world_step(camera.zoom);
    const float min_world_x = screen_to_world_x(0.0F, width, camera);
    const float max_world_x = screen_to_world_x(static_cast<float>(width), width, camera);
    const float min_world_y = screen_to_world_y(0.0F, height, camera);
    const float max_world_y = screen_to_world_y(static_cast<float>(height), height, camera);

    glBegin(GL_LINES);
    const int first_vertical = static_cast<int>(std::floor(min_world_x / grid_step)) - 1;
    const int last_vertical = static_cast<int>(std::floor(max_world_x / grid_step)) + 1;
    for (int index = first_vertical; index <= last_vertical; ++index) {
        const bool major = (index % kEditorMajorGridEvery) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_x = static_cast<float>(index) * grid_step;
        const float screen_x = world_to_screen_x(world_x, width, camera);
        const float ndc_x = screen_to_ndc_x(screen_x, width);

        glColor4f(0.18F, 0.78F, 0.68F, alpha);
        glVertex2f(ndc_x, -1.0F);
        glVertex2f(ndc_x, 1.0F);
    }

    const int first_horizontal = static_cast<int>(std::floor(min_world_y / grid_step)) - 1;
    const int last_horizontal = static_cast<int>(std::floor(max_world_y / grid_step)) + 1;
    for (int index = first_horizontal; index <= last_horizontal; ++index) {
        const bool major = (index % kEditorMajorGridEvery) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_y = static_cast<float>(index) * grid_step;
        const float screen_y = world_to_screen_y(world_y, height, camera);
        const float ndc_y = screen_to_ndc_y(screen_y, height);

        glColor4f(0.18F, 0.78F, 0.68F, alpha);
        glVertex2f(-1.0F, ndc_y);
        glVertex2f(1.0F, ndc_y);
    }

    glColor4f(0.90F, 0.96F, 0.76F, 0.55F);
    const float origin_x = screen_to_ndc_x(world_to_screen_x(0.0F, width, camera), width);
    const float origin_y = screen_to_ndc_y(world_to_screen_y(0.0F, height, camera), height);
    glVertex2f(origin_x, -1.0F);
    glVertex2f(origin_x, 1.0F);
    glVertex2f(-1.0F, origin_y);
    glVertex2f(1.0F, origin_y);
    glEnd();

    glDisable(GL_BLEND);
}

void draw_loop_outline(
    const undecedent::PolygonLoop& loop,
    const int width,
    const int height,
    const EditorCamera& camera,
    const float red,
    const float green,
    const float blue,
    const float alpha
) {
    if (loop.vertices.empty()) {
        return;
    }

    glColor4f(red, green, blue, alpha);
    glBegin(GL_LINE_LOOP);
    for (const undecedent::Vec2 vertex : loop.vertices) {
        glVertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    glEnd();
}

void draw_vertex_marker(
    const undecedent::Vec2 point,
    const int width,
    const int height,
    const EditorCamera& camera,
    const float red,
    const float green,
    const float blue,
    const float alpha
) {
    const float screen_x = world_to_screen_x(point.x, width, camera);
    const float screen_y = world_to_screen_y(point.y, height, camera);
    glColor4f(red, green, blue, alpha);
    draw_screen_quad(screen_x - 3.0F, screen_y - 3.0F, 6.0F, 6.0F, width, height);
}

void draw_sector_planes(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_TRIANGLES);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        if (!sector_visible_in_slice(editor_world, editor_world.sectors[i])) {
            continue;
        }
        const bool selected = is_sector_selected(editor_world, static_cast<int>(i));
        if (selected) {
            glColor4f(0.36F, 0.78F, 0.68F, 0.28F);
        } else {
            glColor4f(0.22F, 0.58F, 0.68F, 0.20F);
        }

        for (const undecedent::Triangle& triangle : editor_world.sectors[i].triangles) {
            glVertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
    }
    glEnd();

    glLineWidth(2.0F);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        if (!sector_visible_in_slice(editor_world, editor_world.sectors[i])) {
            continue;
        }
        const bool selected = is_sector_selected(editor_world, static_cast<int>(i));
        const float alpha = selected ? 0.95F : 0.72F;
        draw_loop_outline(editor_world.sectors[i].outer, width, height, camera, 0.72F, 0.95F, 0.86F, alpha);
        for (const undecedent::PolygonLoop& hole : editor_world.sectors[i].holes) {
            draw_loop_outline(hole, width, height, camera, 0.95F, 0.78F, 0.42F, alpha);
        }
    }

    glBegin(GL_LINES);
    glColor4f(0.30F, 0.62F, 1.0F, 0.78F);
    for (const undecedent::SectorPlane& sector : editor_world.sectors) {
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (std::size_t edge_index = 0; edge_index < sector.edge_neighbors.size(); ++edge_index) {
            if (sector.edge_neighbors[edge_index] < 0) {
                continue;
            }

            const undecedent::Vec2 a = sector.outer.vertices[edge_index];
            const undecedent::Vec2 b = sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()];
            glVertex2f(world_to_ndc_x(a.x, width, camera), world_to_ndc_y(a.y, height, camera));
            glVertex2f(world_to_ndc_x(b.x, width, camera), world_to_ndc_y(b.y, height, camera));
        }
    }
    glEnd();

    glBegin(GL_QUADS);
    for (std::size_t sector_index = 0; sector_index < editor_world.sectors.size(); ++sector_index) {
        const undecedent::SectorPlane& sector = editor_world.sectors[sector_index];
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (std::size_t vertex_index = 0; vertex_index < sector.outer.vertices.size(); ++vertex_index) {
            const undecedent::Vec2 vertex = sector.outer.vertices[vertex_index];
            if (is_dragged_committed_ref(editor_world, sector_index, vertex_index)) {
                draw_vertex_marker(vertex, width, height, camera, 1.0F, 0.28F, 0.22F, 0.98F);
            } else if (editor_world.has_hovered_committed_vertex &&
                same_editor_point(editor_world.hovered_committed_vertex, vertex)) {
                draw_vertex_marker(vertex, width, height, camera, 0.36F, 0.78F, 1.0F, 0.98F);
            } else {
                draw_vertex_marker(vertex, width, height, camera, 0.90F, 0.96F, 0.76F, 0.85F);
            }
        }
        for (const undecedent::PolygonLoop& hole : sector.holes) {
            for (const undecedent::Vec2 vertex : hole.vertices) {
                draw_vertex_marker(vertex, width, height, camera, 0.95F, 0.78F, 0.42F, 0.85F);
            }
        }
    }
    glEnd();

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_draft_plane(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    if (editor_world.plane_tool == PlaneToolMode::None || editor_world.draft_vertices.empty()) {
        return;
    }

    const undecedent::TriangulationResult preview_result = draft_preview_result(editor_world);
    const bool valid = preview_result.status == undecedent::TriangulationStatus::Ok;
    const bool pending = preview_result.status == undecedent::TriangulationStatus::NotEnoughVertices;
    const float red = valid || pending ? 0.90F : 1.0F;
    const float green = valid ? 0.96F : (pending ? 0.78F : 0.25F);
    const float blue = valid ? 0.76F : (pending ? 0.42F : 0.20F);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (valid && editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        glBegin(GL_TRIANGLES);
        glColor4f(0.90F, 0.96F, 0.76F, 0.14F);
        for (const undecedent::Triangle& triangle : preview_result.triangles) {
            glVertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
        glEnd();
    }

    glLineWidth(2.0F);
    glBegin(GL_LINE_STRIP);
    glColor4f(red, green, blue, 0.92F);
    for (const undecedent::Vec2 vertex : editor_world.draft_vertices) {
        glVertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    glVertex2f(
        world_to_ndc_x(editor_world.snapped_mouse.x, width, camera),
        world_to_ndc_y(editor_world.snapped_mouse.y, height, camera)
    );
    glEnd();

    if (editor_world.draft_vertices.size() >= 3) {
        glBegin(GL_LINES);
        glColor4f(red, green, blue, 0.42F);
        const undecedent::Vec2 first = editor_world.draft_vertices.front();
        glVertex2f(world_to_ndc_x(editor_world.snapped_mouse.x, width, camera), world_to_ndc_y(editor_world.snapped_mouse.y, height, camera));
        glVertex2f(world_to_ndc_x(first.x, width, camera), world_to_ndc_y(first.y, height, camera));
        glEnd();
    }

    glBegin(GL_QUADS);
    for (const undecedent::Vec2 vertex : editor_world.draft_vertices) {
        draw_vertex_marker(vertex, width, height, camera, red, green, blue, 0.95F);
    }
    draw_vertex_marker(editor_world.snapped_mouse, width, height, camera, red, green, blue, 0.55F);
    glEnd();

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_scale_indicator(const int width, const int height, const EditorCamera& camera, const float slice_z) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const std::uint64_t world_units = scale_indicator_world_units(camera.zoom);
    const float bar_pixels = static_cast<float>(static_cast<double>(world_units) * camera.zoom);
    const float origin_x = 18.0F;
    const float origin_y = static_cast<float>(height) - 28.0F;
    const float label_y = static_cast<float>(height) - 56.0F;
    const std::string label = std::to_string(world_units) + "u";
    const std::string z_label = "Z " + format_world_units(slice_z) + "u";

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.34F);
    draw_screen_quad(10.0F, static_cast<float>(height) - 88.0F, std::max(bar_pixels + 34.0F, 112.0F), 76.0F, width, height);
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.88F);
    draw_screen_line(origin_x, origin_y, origin_x + bar_pixels, origin_y, width, height);
    draw_screen_line(origin_x, origin_y - 7.0F, origin_x, origin_y + 7.0F, width, height);
    draw_screen_line(origin_x + bar_pixels, origin_y - 7.0F, origin_x + bar_pixels, origin_y + 7.0F, width, height);
    glEnd();

    glLineWidth(1.0F);
    glBegin(GL_LINES);
    draw_scale_label(label, origin_x, label_y, 7.0F, width, height);
    draw_scale_label(z_label, origin_x, label_y - 18.0F, 7.0F, width, height);
    glEnd();

    glDisable(GL_BLEND);
}

void draw_player_spawn_2d(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    if (!editor_world.player_spawn.set || width <= 0 || height <= 0) {
        return;
    }

    const float spawn_floor = editor_world.player_spawn.position.y - kPlayerEyeHeight;
    const float visible_band = std::max(1.0F, editor_grid_world_step(camera.zoom) * 0.5F);
    if (std::abs(spawn_floor - editor_world.slice_z) > visible_band) {
        return;
    }

    const float x = world_to_screen_x(editor_world.player_spawn.position.x, width, camera);
    const float y = world_to_screen_y(editor_world.player_spawn.position.z, height, camera);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0F, 0.82F, 0.25F, 0.95F);
    glLineWidth(2.0F);
    glBegin(GL_LINES);
    draw_screen_line(x - 10.0F, y, x + 10.0F, y, width, height);
    draw_screen_line(x, y - 10.0F, x, y + 10.0F, width, height);
    const float facing_x = x - std::sin(editor_world.player_spawn.yaw) * 18.0F;
    const float facing_y = y - std::cos(editor_world.player_spawn.yaw) * 18.0F;
    draw_screen_line(x, y, facing_x, facing_y, width, height);
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_point_lights_2d(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    if (width <= 0 || height <= 0 || editor_world.point_lights.empty()) {
        return;
    }

    const float visible_band = std::max(1.0F, editor_grid_world_step(camera.zoom) * 0.5F);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0F);
    glBegin(GL_LINES);
    glColor4f(1.0F, 0.86F, 0.35F, 0.92F);
    for (const undecedent::PointLight& light : editor_world.point_lights) {
        if (std::abs(light.position.y - editor_world.slice_z) > visible_band) {
            continue;
        }
        const float x = world_to_screen_x(light.position.x, width, camera);
        const float y = world_to_screen_y(light.position.z, height, camera);
        draw_screen_line(x - 8.0F, y, x + 8.0F, y, width, height);
        draw_screen_line(x, y - 8.0F, x, y + 8.0F, width, height);
        draw_screen_line(x - 5.0F, y - 5.0F, x + 5.0F, y + 5.0F, width, height);
        draw_screen_line(x - 5.0F, y + 5.0F, x + 5.0F, y - 5.0F, width, height);
    }
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_player_spawn_3d(
    const undecedent::PlayerSpawn& spawn,
    const int width,
    const int height,
    const GameCamera& camera
) {
    if (!spawn.set || width <= 0 || height <= 0) {
        return;
    }

    set_game_projection(width, height, camera);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0F, 0.82F, 0.25F, 0.95F);
    glLineWidth(2.0F);
    const float feet = spawn.position.y - kPlayerEyeHeight;
    const float head = feet + kPlayerHeight;
    const float x = spawn.position.x;
    const float z = spawn.position.z;
    glBegin(GL_LINES);
    glVertex3f(x, feet, z);
    glVertex3f(x, head, z);
    glVertex3f(x - kPlayerRadius, feet, z);
    glVertex3f(x + kPlayerRadius, feet, z);
    glVertex3f(x, feet, z - kPlayerRadius);
    glVertex3f(x, feet, z + kPlayerRadius);
    glVertex3f(x, spawn.position.y, z);
    glVertex3f(x - std::sin(spawn.yaw) * 24.0F, spawn.position.y, z - std::cos(spawn.yaw) * 24.0F);
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_point_lights_3d(
    const std::vector<undecedent::PointLight>& point_lights,
    const int width,
    const int height,
    const GameCamera& camera
) {
    if (point_lights.empty() || width <= 0 || height <= 0) {
        return;
    }

    set_game_projection(width, height, camera);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0F, 0.86F, 0.35F, 0.92F);
    glLineWidth(2.0F);
    glBegin(GL_LINES);
    for (const undecedent::PointLight& light : point_lights) {
        const float x = light.position.x;
        const float y = light.position.y;
        const float z = light.position.z;
        constexpr float s = 8.0F;
        glVertex3f(x - s, y, z);
        glVertex3f(x + s, y, z);
        glVertex3f(x, y - s, z);
        glVertex3f(x, y + s, z);
        glVertex3f(x, y, z - s);
        glVertex3f(x, y, z + s);
        glVertex3f(x - s, y - s, z);
        glVertex3f(x + s, y + s, z);
        glVertex3f(x - s, y + s, z);
        glVertex3f(x + s, y - s, z);
    }
    glEnd();
    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void set_game_projection(const int width, const int height, const GameCamera& camera) {
    const double aspect = height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;
    const double fov_y_radians = 70.0 * 3.14159265358979323846 / 180.0;
    const double top = std::tan(fov_y_radians * 0.5) * kGameNearPlane;
    const double right = top * aspect;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, kGameNearPlane, kGameFarPlane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-camera.pitch * 180.0F / 3.14159265F, 1.0F, 0.0F, 0.0F);
    glRotatef(-camera.yaw * 180.0F / 3.14159265F, 0.0F, 1.0F, 0.0F);
    glTranslatef(-camera.x, -camera.y, -camera.z);
}

int draw_runtime_world(
    const undecedent::RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const int width,
    const int height,
    const GameCamera& camera,
    const bool draw_wire_overlay,
    const bool filter_connected_visibility
) {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    set_game_projection(width, height, camera);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glLineWidth(1.0F);

    const int camera_sector = undecedent::sector_at_point(world, undecedent::Vec3{camera.x, camera.y, camera.z});
    const std::vector<int> visible_sectors = undecedent::visible_sectors_from(world, camera_sector);
    const bool filter_visible_sectors = filter_connected_visibility && camera_sector >= 0;
    const auto is_visible = [&visible_sectors, filter_visible_sectors](const int sector_id) {
        return !filter_visible_sectors ||
            std::find(visible_sectors.begin(), visible_sectors.end(), sector_id) != visible_sectors.end();
    };

    int visible_triangle_count = 0;

    if (render_cache.vertex_buffer != 0 && render_cache.total_vertices > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(
            3,
            GL_FLOAT,
            sizeof(RuntimeRenderVertex),
            reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
        );
        glColorPointer(
            3,
            GL_FLOAT,
            sizeof(RuntimeRenderVertex),
            reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, r))
        );

        if (!filter_visible_sectors) {
            glDrawArrays(GL_TRIANGLES, 0, render_cache.total_vertices);
            visible_triangle_count = render_cache.total_vertices / 3;
        } else {
            for (const int sector_id : visible_sectors) {
                if (sector_id < 0 || sector_id >= static_cast<int>(render_cache.sector_ranges.size())) {
                    continue;
                }
                const RuntimeRenderRange range = render_cache.sector_ranges[static_cast<std::size_t>(sector_id)];
                if (range.vertex_count <= 0) {
                    continue;
                }
                glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
                visible_triangle_count += range.vertex_count / 3;
            }
        }

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (draw_wire_overlay) {
        glBegin(GL_LINES);
        glColor4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const undecedent::RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (!is_visible(tagged_triangle.sector_id)) {
                continue;
            }

            const undecedent::RuntimeTriangle& triangle = tagged_triangle.triangle;
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        glEnd();
    }

    return visible_triangle_count;
}

int draw_deferred_runtime_world(
    undecedent::DeferredRenderer& renderer,
    const undecedent::RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const std::vector<undecedent::PointLight>& point_lights,
    const int width,
    const int height,
    const GameCamera& camera,
    const bool draw_wire_overlay
) {
    if (!undecedent::ensure_deferred_renderer(renderer, width, height) ||
        render_cache.vertex_buffer == 0 || render_cache.total_vertices <= 0) {
        return draw_runtime_world(world, render_cache, width, height, camera, draw_wire_overlay, true);
    }

    const int camera_sector = undecedent::sector_at_point(world, undecedent::Vec3{camera.x, camera.y, camera.z});
    const std::vector<int> visible_sectors = undecedent::visible_sectors_from(world, camera_sector);
    const bool filter_visible_sectors = camera_sector >= 0;

    int visible_triangle_count = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.framebuffer);
    glViewport(0, 0, width, height);
    const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, draw_buffers);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    set_game_projection(width, height, camera);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(renderer.geometry_program);
    glBindBuffer(GL_ARRAY_BUFFER, render_cache.vertex_buffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, x))
    );
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, r))
    );
    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RuntimeRenderVertex),
        reinterpret_cast<const void*>(offsetof(RuntimeRenderVertex, nx))
    );

    if (!filter_visible_sectors) {
        glDrawArrays(GL_TRIANGLES, 0, render_cache.total_vertices);
        visible_triangle_count = render_cache.total_vertices / 3;
    } else {
        for (const int sector_id : visible_sectors) {
            if (sector_id < 0 || sector_id >= static_cast<int>(render_cache.sector_ranges.size())) {
                continue;
            }
            const RuntimeRenderRange range = render_cache.sector_ranges[static_cast<std::size_t>(sector_id)];
            if (range.vertex_count <= 0) {
                continue;
            }
            glDrawArrays(GL_TRIANGLES, range.first_vertex, range.vertex_count);
            visible_triangle_count += range.vertex_count / 3;
        }
    }

    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(renderer.lighting_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.position_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uPosition"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.normal_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uNormal"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.albedo_texture);
    glUniform1i(glGetUniformLocation(renderer.lighting_program, "uAlbedo"), 2);
    glUniform2f(
        glGetUniformLocation(renderer.lighting_program, "uInvViewport"),
        1.0F / static_cast<float>(width),
        1.0F / static_cast<float>(height)
    );
    glUniform3f(glGetUniformLocation(renderer.lighting_program, "uCameraPosition"), camera.x, camera.y, camera.z);
    glUniform3f(glGetUniformLocation(renderer.lighting_program, "uAmbientColor"), 0.14F, 0.17F, 0.18F);
    const GLint light_count_location = glGetUniformLocation(renderer.lighting_program, "uLightCount");
    int light_count = static_cast<int>(std::min<std::size_t>(point_lights.size(), kMaxDeferredPointLights));
    std::array<undecedent::PointLight, kMaxDeferredPointLights> fallback_lights{};
    const undecedent::PointLight* lights = point_lights.empty() ? fallback_lights.data() : point_lights.data();
    if (point_lights.empty()) {
        fallback_lights[0].position = undecedent::Vec3{camera.x, camera.y + 48.0F, camera.z};
        fallback_lights[0].color = undecedent::Vec3{1.0F, 0.93F, 0.80F};
        fallback_lights[0].radius = 640.0F;
        fallback_lights[0].intensity = 1.8F;
        light_count = 1;
    }
    glUniform1i(light_count_location, light_count);
    for (int i = 0; i < light_count; ++i) {
        const undecedent::PointLight& light = lights[i];
        const std::string index = std::to_string(i);
        glUniform3f(
            glGetUniformLocation(renderer.lighting_program, ("uLightPositions[" + index + "]").c_str()),
            light.position.x,
            light.position.y,
            light.position.z
        );
        glUniform3f(
            glGetUniformLocation(renderer.lighting_program, ("uLightColors[" + index + "]").c_str()),
            light.color.x,
            light.color.y,
            light.color.z
        );
        glUniform1f(
            glGetUniformLocation(renderer.lighting_program, ("uLightRadii[" + index + "]").c_str()),
            light.radius
        );
        glUniform1f(
            glGetUniformLocation(renderer.lighting_program, ("uLightIntensities[" + index + "]").c_str()),
            light.intensity
        );
    }

    glBegin(GL_QUADS);
    glVertex2f(-1.0F, -1.0F);
    glVertex2f(1.0F, -1.0F);
    glVertex2f(1.0F, 1.0F);
    glVertex2f(-1.0F, 1.0F);
    glEnd();
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (draw_wire_overlay) {
        set_game_projection(width, height, camera);
        glDisable(GL_DEPTH_TEST);
        glBegin(GL_LINES);
        glColor4f(0.84F, 0.96F, 0.78F, 0.58F);
        for (const undecedent::RuntimeTaggedTriangle& tagged_triangle : world.triangles) {
            if (filter_visible_sectors &&
                std::find(visible_sectors.begin(), visible_sectors.end(), tagged_triangle.sector_id) == visible_sectors.end()) {
                continue;
            }

            const undecedent::RuntimeTriangle& triangle = tagged_triangle.triangle;
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
        }
        glEnd();
    }

    return visible_triangle_count;
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
                        place_entity(
                            editor_world,
                            undecedent::Vec3{editor_world.snapped_mouse.x, editor_world.slice_z, editor_world.snapped_mouse.y}
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
                    event.button.y
                );
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const bool shift_select = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    if (!shift_select && pick.hit) {
                        undecedent::Vec3 position = pick.point;
                        if (editor_world.entity_placement == EntityPlacementType::PointLight) {
                            position = add_vec3(position, mul_vec3(pick.normal, 24.0F));
                        } else {
                            position.y += 4.0F;
                        }
                        place_entity(editor_world, position, game_camera.yaw);
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

                        if (vertex_index >= 0) {
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
                            place_entity(
                                editor_world,
                                undecedent::Vec3{editor_world.snapped_mouse.x, editor_world.slice_z, editor_world.snapped_mouse.y}
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
            update_playtest_camera(game_camera, editor_world.runtime_world, playtest_state, dt);
        } else {
            update_game_camera(game_camera, dt);
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
            draw_editor_grid(width, height, editor_camera);
            draw_sector_planes(editor_world, width, height, editor_camera);
            draw_draft_plane(editor_world, width, height, editor_camera);
            draw_point_lights_2d(editor_world, width, height, editor_camera);
            draw_player_spawn_2d(editor_world, width, height, editor_camera);
            draw_scale_indicator(width, height, editor_camera, editor_world.slice_z);
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
                    runtime_wire_overlay_enabled
                );
            } else {
                visible_triangle_count = draw_runtime_world(
                    editor_world.runtime_world,
                    editor_world.runtime_render_cache,
                    width,
                    height,
                    game_camera,
                    runtime_wire_overlay_enabled,
                    false
                );
            }
            if (editor_3d_now) {
                draw_point_lights_3d(editor_world.point_lights, width, height, game_camera);
                draw_player_spawn_3d(editor_world.player_spawn, width, height, game_camera);
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
