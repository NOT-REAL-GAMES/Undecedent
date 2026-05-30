#include "undecedent/editor.hpp"

#include "undecedent/csg.hpp"
#include "undecedent/editor_slice.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <utility>

namespace undecedent {
namespace {

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
constexpr float kSectorMinHeight = 8.0F;
constexpr float kCloseVertexPixels = 12.0F;
constexpr std::size_t kEditorHistoryLimit = 128;

void trim_history_stack(std::vector<EditorHistorySnapshot>& stack) {
    if (stack.size() > kEditorHistoryLimit) {
        stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(stack.size() - kEditorHistoryLimit));
    }
}

} // namespace

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

Vec2 snap_to_grid(const Vec2 point, const float grid_step) {
    return Vec2{
        std::round(point.x / grid_step) * grid_step,
        std::round(point.y / grid_step) * grid_step,
    };
}

std::string format_world_units(const float value) {
    const float rounded = std::round(value);
    if (std::abs(value - rounded) <= 0.001F) {
        return std::to_string(static_cast<int>(rounded));
    }

    std::ostringstream stream;
    stream << std::fixed;
    stream.precision(2);
    stream << value;
    return stream.str();
}

PolygonLoop draft_loop(const EditorWorld& editor_world) {
    return PolygonLoop{editor_world.draft_vertices};
}

bool rebuild_sector(SectorPlane& sector) {
    const TriangulationResult result = triangulate_polygon(sector.outer, sector.holes);
    sector.status = result.status;
    sector.status_message = result.message;
    sector.triangles = result.triangles;
    return result.status == TriangulationStatus::Ok;
}

bool point_in_triangle(const Vec2 p, const Vec2 a, const Vec2 b, const Vec2 c) {
    const auto cross2 = [](const Vec2 p0, const Vec2 p1, const Vec2 p2) {
        return ((p1.x - p0.x) * (p2.y - p0.y)) - ((p1.y - p0.y) * (p2.x - p0.x));
    };
    const float ab = cross2(a, b, p);
    const float bc = cross2(b, c, p);
    const float ca = cross2(c, a, p);
    return (ab >= -kGeometryEpsilon && bc >= -kGeometryEpsilon && ca >= -kGeometryEpsilon) ||
        (ab <= kGeometryEpsilon && bc <= kGeometryEpsilon && ca <= kGeometryEpsilon);
}

float distance_squared(const Vec2 a, const Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool same_editor_point(const Vec2 a, const Vec2 b) {
    return distance_squared(a, b) <= (kGeometryEpsilon * kGeometryEpsilon);
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

bool sector_visible_in_slice(const EditorWorld& editor_world, const SectorPlane& sector) {
    return sector_intersects_z_slice(sector, editor_world.slice_z);
}

bool sector_on_active_floor(const EditorWorld& editor_world, const SectorPlane& sector) {
    return sector_floor_matches_slice(sector, editor_world.slice_z);
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

bool draft_contains_point(const EditorWorld& editor_world, const Vec2 point) {
    return std::any_of(
        editor_world.draft_vertices.begin(),
        editor_world.draft_vertices.end(),
        [point](const Vec2 vertex) {
            return same_editor_point(vertex, point);
        }
    );
}

bool draft_contains_point_except(const EditorWorld& editor_world, const Vec2 point, const int ignored_index) {
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
        const Vec2 vertex = editor_world.draft_vertices[i];
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
    Vec2& out_vertex
) {
    bool found = false;
    float nearest_distance = kCloseVertexPixels * kCloseVertexPixels;

    for (const SectorPlane& sector : editor_world.sectors) {
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (const Vec2 vertex : sector.outer.vertices) {
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

std::vector<CommittedVertexRef> matching_committed_vertices(const EditorWorld& editor_world, const Vec2 point) {
    std::vector<CommittedVertexRef> refs;
    for (std::size_t sector_index = 0; sector_index < editor_world.sectors.size(); ++sector_index) {
        const SectorPlane& sector = editor_world.sectors[sector_index];
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

void move_dragged_committed_vertices(EditorWorld& editor_world, const Vec2 point) {
    for (const CommittedVertexRef ref : editor_world.dragged_committed_refs) {
        if (ref.sector >= editor_world.sectors.size()) {
            continue;
        }
        SectorPlane& sector = editor_world.sectors[ref.sector];
        if (ref.vertex >= sector.outer.vertices.size()) {
            continue;
        }
        sector.outer.vertices[ref.vertex] = point;
        rebuild_sector(sector);
    }

    editor_world.dragged_committed_vertex = point;
}

void update_snapped_mouse(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    const Vec2 world{
        screen_to_world_x(mouse_x, width, camera),
        screen_to_world_y(mouse_y, height, camera),
    };
    editor_world.snapped_mouse = snap_to_grid(world, editor_grid_world_step(camera.zoom));
}

void rebuild_runtime_geometry(EditorWorld& editor_world) {
    editor_world.runtime_world = build_runtime_world(editor_world.sectors);
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

PointLight default_point_light_at(const Vec3 position) {
    PointLight light;
    light.position = position;
    return light;
}

void place_entity(EditorWorld& editor_world, const Vec3 position, const float yaw) {
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

void place_entity_at_origin(
    EditorWorld& editor_world,
    const Vec3 origin,
    const float yaw,
    const float player_eye_height
) {
    if (editor_world.entity_placement != EntityPlacementType::PlayerSpawn) {
        place_entity(editor_world, origin, yaw);
        return;
    }

    push_undo_snapshot(editor_world, "place entity");
    editor_world.player_spawn.position = Vec3{origin.x, origin.y + player_eye_height, origin.z};
    editor_world.player_spawn.yaw = yaw;
    editor_world.player_spawn.set = true;
    std::cout << "Placed player spawn origin at " << origin.x << ", " << origin.y << ", " << origin.z << '\n';
}

const char* entity_placement_label(const EntityPlacementType type) {
    switch (type) {
    case EntityPlacementType::PlayerSpawn: return "PLAYER SPAWN";
    case EntityPlacementType::PointLight: return "POINT LIGHT";
    }
    return "ENTITY";
}

void normalize_sector_materials(SectorPlane& sector) {
    sector.floor_material = clamped_material_id(sector.floor_material);
    sector.ceiling_material = clamped_material_id(sector.ceiling_material);
    sector.wall_materials.resize(sector.outer.vertices.size(), kDefaultMaterialId);
    for (int& material : sector.wall_materials) {
        material = clamped_material_id(material);
    }
    sector.hole_wall_materials.resize(sector.holes.size());
    for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
        sector.hole_wall_materials[hole_index].resize(
            sector.holes[hole_index].vertices.size(),
            kDefaultMaterialId
        );
        for (int& material : sector.hole_wall_materials[hole_index]) {
            material = clamped_material_id(material);
        }
    }
}

bool apply_material_to_surface(EditorWorld& editor_world, const SurfacePick& pick, const int material_id) {
    if (!pick.hit || pick.sector_id < 0 || pick.sector_id >= static_cast<int>(editor_world.sectors.size())) {
        return false;
    }

    SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(pick.sector_id)];
    normalize_sector_materials(sector);
    const int clamped = clamped_material_id(material_id);
    bool changed = false;
    switch (pick.surface.kind) {
    case RuntimeSurfaceKind::Floor:
        changed = sector.floor_material != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.floor_material = clamped;
        break;
    case RuntimeSurfaceKind::Ceiling:
        changed = sector.ceiling_material != clamped;
        if (!changed) {
            return true;
        }
        push_undo_snapshot(editor_world, "material assignment");
        sector.ceiling_material = clamped;
        break;
    case RuntimeSurfaceKind::Wall:
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
    case RuntimeSurfaceKind::HoleWall:
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

TriangulationResult draft_preview_result(const EditorWorld& editor_world) {
    if (editor_world.plane_tool == PlaneToolMode::None) {
        return {};
    }

    if (editor_world.dragged_draft_vertex >= 0) {
        return editor_world.draft_result;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawKnife) {
        if (editor_world.draft_vertices.empty()) {
            return TriangulationResult{
                TriangulationStatus::NotEnoughVertices,
                "Knife cut needs two points.",
                {}
            };
        }

        if (same_editor_point(editor_world.draft_vertices.front(), editor_world.snapped_mouse)) {
            return TriangulationResult{
                TriangulationStatus::DuplicateVertex,
                "Knife cut needs two distinct points.",
                {}
            };
        }

        return TriangulationResult{TriangulationStatus::Ok, {}, {}};
    }

    std::vector<Vec2> preview_vertices = editor_world.draft_vertices;
    const bool closes_loop =
        preview_vertices.size() >= 3 &&
        same_editor_point(editor_world.snapped_mouse, preview_vertices.front());

    if (!closes_loop) {
        if (draft_contains_point(editor_world, editor_world.snapped_mouse)) {
            return TriangulationResult{
                TriangulationStatus::DuplicateVertex,
                "Preview vertex duplicates an existing vertex.",
                {}
            };
        }

        preview_vertices.push_back(editor_world.snapped_mouse);
    }

    if (preview_vertices.size() < 3) {
        return TriangulationResult{
            TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
    }

    const PolygonLoop preview_loop{preview_vertices};
    if (editor_world.plane_tool == PlaneToolMode::DrawOuter ||
        editor_world.plane_tool == PlaneToolMode::DrawHole) {
        return triangulate_polygon(preview_loop);
    }

    return {};
}

void refresh_draft(EditorWorld& editor_world) {
    if (editor_world.plane_tool == PlaneToolMode::DrawKnife) {
        if (editor_world.draft_vertices.size() < 2) {
            editor_world.draft_result = TriangulationResult{
                TriangulationStatus::NotEnoughVertices,
                "Knife cut needs two points.",
                {}
            };
            return;
        }

        if (same_editor_point(editor_world.draft_vertices[0], editor_world.draft_vertices[1])) {
            editor_world.draft_result = TriangulationResult{
                TriangulationStatus::DuplicateVertex,
                "Knife cut needs two distinct points.",
                {}
            };
            return;
        }

        editor_world.draft_result = TriangulationResult{TriangulationStatus::Ok, {}, {}};
        return;
    }

    if (editor_world.draft_vertices.size() < 3) {
        editor_world.draft_result = TriangulationResult{
            TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
        return;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter ||
        editor_world.plane_tool == PlaneToolMode::DrawHole) {
        editor_world.draft_result = triangulate_polygon(draft_loop(editor_world));
        return;
    }

    editor_world.draft_result = TriangulationResult{
        TriangulationStatus::TriangulationFailed,
        "No active plane tool.",
        {}
    };
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
        [&editor_world](const SectorPlane& sector) {
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

void start_knife_tool(EditorWorld& editor_world) {
    const bool has_active_floor_sector = std::any_of(
        editor_world.sectors.begin(),
        editor_world.sectors.end(),
        [&editor_world](const SectorPlane& sector) {
            return sector_on_active_floor(editor_world, sector);
        }
    );
    if (!has_active_floor_sector) {
        std::cout << "Create a sector on this Z slice before using the knife tool.\n";
        return;
    }

    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.plane_tool = PlaneToolMode::DrawKnife;
    std::cout << "Knife tool: click two points to split sectors\n";
}

bool commit_plane_tool(EditorWorld& editor_world) {
    refresh_draft(editor_world);
    if (editor_world.draft_result.status != TriangulationStatus::Ok) {
        std::cout << "Cannot commit loop: " << editor_world.draft_result.message << '\n';
        return false;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        const CsgAddResult csg_result =
            csg_add_sector_at_floor(
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
        const CsgAddResult csg_result =
            csg_subtract_sector_at_floor(
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

    if (editor_world.plane_tool == PlaneToolMode::DrawKnife) {
        const CsgAddResult csg_result =
            csg_split_sectors_by_line_at_floor(
                editor_world.sectors,
                editor_world.draft_vertices[0],
                editor_world.draft_vertices[1],
                editor_world.slice_z
            );
        if (!csg_result.ok) {
            std::cout << "Cannot commit knife cut: " << csg_result.message << '\n';
            return false;
        }

        push_undo_snapshot(editor_world, "knife cut");
        editor_world.sectors = csg_result.sectors;
        clear_sector_selection(editor_world);
        clear_selection_outside_slice(editor_world);
        rebuild_runtime_geometry(editor_world);
        cancel_plane_tool(editor_world);
        std::cout << "Committed knife cut; sectors: " << editor_world.sectors.size() << '\n';
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

    const CsgAddResult merge_result = csg_merge_sectors(editor_world.sectors, selected);
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

    const CsgAddResult delete_result = csg_delete_sectors(editor_world.sectors, selected);
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

        const SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
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
        SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
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
        SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
        sector.floor_height += delta;
    }

    rebuild_runtime_geometry(editor_world);
    clear_selection_outside_slice(editor_world);
    std::cout << "Adjusted sector floor by " << delta << " for " << targets.size() << " sectors.\n";
    return true;
}

void finish_committed_vertex_drag(EditorWorld& editor_world) {
    if (!editor_world.has_dragged_committed_vertex) {
        return;
    }

    if (editor_world.dragged_committed_vertex_moved) {
        const CsgAddResult rebuild_result = csg_rebuild_sectors(editor_world.sectors);
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

int sector_at_point(const EditorWorld& editor_world, const Vec2 point) {
    for (int i = static_cast<int>(editor_world.sectors.size()) - 1; i >= 0; --i) {
        const SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(i)];
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (const Triangle& triangle : sector.triangles) {
            if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
                return i;
            }
        }
    }
    return -1;
}

bool point_in_sector(const SectorPlane& sector, const Vec2 point) {
    for (const Triangle& triangle : sector.triangles) {
        if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
            return true;
        }
    }
    return false;
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

} // namespace undecedent
