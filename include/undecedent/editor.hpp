#pragma once

#include "undecedent/entity.hpp"
#include "undecedent/geometry.hpp"
#include "undecedent/game_camera.hpp"
#include "undecedent/map_io.hpp"
#include "undecedent/runtime_render.hpp"
#include "undecedent/runtime_render_cache.hpp"
#include "undecedent/runtime_world.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace undecedent {

struct EditorCamera {
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;
    float target_x = 0.0F;
    float target_y = 0.0F;
    float target_zoom = 1.0F;
    bool panning = false;
};

enum class PlaneToolMode {
    None,
    DrawOuter,
    DrawHole,
    DrawKnife,
};

enum class EntityPlacementType {
    PlayerSpawn,
    PointLight,
};

enum class SelectedEntityKind {
    None,
    PlayerSpawn,
    PointLight,
    SunLight,
};

enum class TranslationGizmoAxis {
    None,
    X,
    Y,
    Z,
};

enum class EntityProperty {
    PositionX,
    PositionY,
    PositionZ,
    Yaw,
    ColorR,
    ColorG,
    ColorB,
    Radius,
    Intensity,
    ShadowBias,
    SunDirectionX,
    SunDirectionY,
    SunDirectionZ,
    SunEnabled,
};

struct SelectedEntityRef {
    SelectedEntityKind kind = SelectedEntityKind::None;
    std::uint64_t point_light_id = 0;
};

struct EntityPick {
    bool hit = false;
    SelectedEntityRef entity;
    float distance = 0.0F;
};

struct TranslationGizmoPick {
    bool hit = false;
    TranslationGizmoAxis axis = TranslationGizmoAxis::None;
    float axis_t = 0.0F;
};

struct SurfacePick {
    bool hit = false;
    int sector_id = -1;
    int material_id = kDefaultMaterialId;
    float distance = 0.0F;
    Vec3 point;
    Vec3 normal;
    RuntimeSurfaceRef surface;
};

struct CommittedVertexRef {
    std::size_t sector = 0;
    std::size_t vertex = 0;
};

struct EditorHistorySnapshot {
    std::vector<SectorPlane> sectors;
    std::set<int> selected_sectors;
    EntityRegistry entities;
    WorldLighting world_lighting;
    MaterialLibrary material_library;
    ScriptStore scripts;
    float slice_z = 0.0F;
    int selected_sector = -1;
    SelectedEntityRef selected_entity;
};

struct ScriptEditorDraft {
    ScriptTargetRef target;
    std::string source;
    std::string status;
    std::vector<std::string> undo_stack;
    std::vector<std::string> redo_stack;
    int caret = 0;
    int selection_anchor = -1;
    int scroll_line = 0;
    bool dirty = false;
    bool read_only = false;
    bool compile_error = false;
};

struct ScriptEditorState {
    bool open = false;
    ScriptTargetRef current_target;
    std::vector<ScriptEditorDraft> drafts;
};

struct EditorWorld {
    std::vector<SectorPlane> sectors;
    std::vector<Vec2> draft_vertices;
    std::vector<SectorPlane> committed_drag_snapshot;
    std::vector<CommittedVertexRef> dragged_committed_refs;
    std::set<int> selected_sectors;
    std::vector<EditorHistorySnapshot> undo_stack;
    std::vector<EditorHistorySnapshot> redo_stack;
    std::set<std::uint64_t> dirty_sector_ids;
    EntityRegistry entities;
    WorldLighting world_lighting;
    MaterialLibrary material_library;
    ScriptStore scripts;
    ScriptEditorState script_editor;
    RuntimeWorld runtime_world;
    RuntimeRenderCache runtime_render_cache;
    TriangulationResult draft_result;
    SelectedEntityRef selected_entity;
    Vec2 snapped_mouse;
    Vec2 hovered_committed_vertex;
    Vec2 dragged_committed_vertex;
    Vec3 entity_drag_start_position;
    Vec3 entity_drag_current_position;
    float slice_z = 0.0F;
    float slice_scroll_remainder = 0.0F;
    float entity_drag_start_axis_t = 0.0F;
    std::uint64_t shadow_revision_counter = 1;
    int dragged_draft_vertex = -1;
    int selected_sector = -1;
    EntityPlacementType entity_placement = EntityPlacementType::PlayerSpawn;
    PlaneToolMode plane_tool = PlaneToolMode::None;
    bool entity_dropdown_open = false;
    bool displacement_sculpt_enabled = false;
    float displacement_brush_radius = 64.0F;
    bool has_hovered_committed_vertex = false;
    bool has_dragged_committed_vertex = false;
    bool has_dragged_entity_gizmo = false;
    bool dragged_draft_vertex_moved = false;
    bool dragged_committed_vertex_moved = false;
    bool dragged_entity_moved = false;
    TranslationGizmoAxis dragged_entity_axis = TranslationGizmoAxis::None;
    bool dirty_entities = false;
    bool dirty_metadata = false;
    bool dirty_materials = false;
    bool dirty_topology = false;
    bool dirty_scripts = false;
};

float screen_to_world_x(float screen_x, int width, const EditorCamera& camera);
float screen_to_world_y(float screen_y, int height, const EditorCamera& camera);
float world_to_screen_x(float world_x, int width, const EditorCamera& camera);
float world_to_screen_y(float world_y, int height, const EditorCamera& camera);
float world_to_ndc_x(float world_x, int width, const EditorCamera& camera);
float world_to_ndc_y(float world_y, int height, const EditorCamera& camera);
float editor_grid_world_step(float zoom);
Vec2 snap_to_grid(Vec2 point, float grid_step);
std::string format_world_units(float value);

PolygonLoop draft_loop(const EditorWorld& editor_world);
bool rebuild_sector(SectorPlane& sector);
bool point_in_triangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c);
float distance_squared(Vec2 a, Vec2 b);
bool same_editor_point(Vec2 a, Vec2 b);

bool is_sector_selected(const EditorWorld& editor_world, int sector_index);
void clear_sector_selection(EditorWorld& editor_world);
void select_single_sector(EditorWorld& editor_world, int sector_index);
void toggle_sector_selection(EditorWorld& editor_world, int sector_index);
std::vector<int> selected_sector_indices(const EditorWorld& editor_world);
bool sector_visible_in_slice(const EditorWorld& editor_world, const SectorPlane& sector);
bool sector_on_active_floor(const EditorWorld& editor_world, const SectorPlane& sector);
void clear_selection_outside_slice(EditorWorld& editor_world);

bool same_selected_entity(SelectedEntityRef a, SelectedEntityRef b);
void select_entity(EditorWorld& editor_world, SelectedEntityRef entity);
void clear_entity_selection(EditorWorld& editor_world);
bool selected_point_light(const EditorWorld& editor_world, PointLight& out_light);
bool selected_entity_position(const EditorWorld& editor_world, Vec3& out_position);
bool move_selected_entity(EditorWorld& editor_world, Vec3 position);
bool delete_selected_entity(EditorWorld& editor_world);
bool adjust_selected_entity_property(EditorWorld& editor_world, EntityProperty property, float delta);
void clear_invalid_entity_selection(EditorWorld& editor_world);
EntityPick pick_editor_entity_2d(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    float player_eye_height
);
EntityPick pick_editor_entity_3d(
    const EditorWorld& editor_world,
    const GameCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    const GameRenderConfig& config
);
TranslationGizmoPick pick_translation_gizmo(
    const EditorWorld& editor_world,
    const GameCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    const GameRenderConfig& config
);
bool start_translation_gizmo_drag(
    EditorWorld& editor_world,
    const GameCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    const GameRenderConfig& config
);
bool update_translation_gizmo_drag(
    EditorWorld& editor_world,
    const GameCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    const GameRenderConfig& config,
    bool fine
);
void finish_translation_gizmo_drag(EditorWorld& editor_world);

bool draft_contains_point(const EditorWorld& editor_world, Vec2 point);
bool draft_contains_point_except(const EditorWorld& editor_world, Vec2 point, int ignored_index);
int draft_vertex_at_screen(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y
);
bool committed_vertex_at_screen(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    Vec2& out_vertex
);
std::vector<CommittedVertexRef> matching_committed_vertices(const EditorWorld& editor_world, Vec2 point);
bool is_dragged_committed_ref(const EditorWorld& editor_world, std::size_t sector_index, std::size_t vertex_index);
void update_committed_vertex_hover(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    int width,
    int height,
    float mouse_x,
    float mouse_y
);
void move_dragged_committed_vertices(EditorWorld& editor_world, Vec2 point);
void update_snapped_mouse(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    int width,
    int height,
    float mouse_x,
    float mouse_y
);

void rebuild_runtime_geometry(EditorWorld& editor_world, bool shadow_geometry_changed = true);
MapDirtyState editor_map_dirty_state(const EditorWorld& editor_world);
void clear_map_dirty_state(EditorWorld& editor_world);
void mark_sector_dirty(EditorWorld& editor_world, std::size_t sector_index);
void mark_entities_dirty(EditorWorld& editor_world);
void mark_topology_dirty(EditorWorld& editor_world);
void ensure_editor_stable_ids(EditorWorld& editor_world);
EditorHistorySnapshot make_history_snapshot(const EditorWorld& editor_world);
void push_undo_snapshot(EditorWorld& editor_world, EditorHistorySnapshot snapshot, const char* label);
void push_undo_snapshot(EditorWorld& editor_world, const char* label);
void restore_history_snapshot(EditorWorld& editor_world, const EditorHistorySnapshot& snapshot);
bool undo_editor_action(EditorWorld& editor_world);
bool redo_editor_action(EditorWorld& editor_world);

PointLight default_point_light_at(Vec3 position);
void place_entity(EditorWorld& editor_world, Vec3 position, float yaw = 0.0F);
void place_entity_at_origin(
    EditorWorld& editor_world,
    Vec3 origin,
    float yaw = 0.0F,
    float player_eye_height = 48.0F
);
const char* entity_placement_label(EntityPlacementType type);
void normalize_sector_materials(SectorPlane& sector);
bool apply_material_to_surface(EditorWorld& editor_world, const SurfacePick& pick, int material_id);
bool sculpt_displacement_at_pick(EditorWorld& editor_world, const SurfacePick& pick, float delta);
void adjust_displacement_brush_radius(EditorWorld& editor_world, float delta);
int selected_sector_subdivision(const EditorWorld& editor_world);
bool adjust_selected_sector_subdivision(EditorWorld& editor_world, int delta);

TriangulationResult draft_preview_result(const EditorWorld& editor_world);
void refresh_draft(EditorWorld& editor_world);
void cancel_plane_tool(EditorWorld& editor_world);
void start_outer_plane(EditorWorld& editor_world);
void start_hole_plane(EditorWorld& editor_world);
void start_knife_tool(EditorWorld& editor_world);
bool commit_plane_tool(EditorWorld& editor_world);
bool merge_selected_sectors(EditorWorld& editor_world);
bool delete_selected_sectors(EditorWorld& editor_world);
bool adjust_selected_sector_heights(EditorWorld& editor_world, float delta);
bool adjust_selected_sector_floor_heights(EditorWorld& editor_world, float delta);
void finish_committed_vertex_drag(EditorWorld& editor_world);
int sector_at_point(const EditorWorld& editor_world, Vec2 point);
bool point_in_sector(const SectorPlane& sector, Vec2 point);

float editor_scroll_zoom_delta(float scroll_y);
void apply_editor_slice_scroll(EditorWorld& editor_world, const EditorCamera& camera, float scroll_y);
void apply_editor_scroll_zoom(
    EditorCamera& camera,
    float scroll_y,
    float mouse_x,
    float mouse_y,
    int width,
    int height
);
void update_editor_camera(EditorCamera& camera, float dt);

} // namespace undecedent
