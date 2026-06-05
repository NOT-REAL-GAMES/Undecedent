#include "undecedent/editor.hpp"

#include "undecedent/csg.hpp"
#include "undecedent/displacement.hpp"
#include "undecedent/editor_slice.hpp"
#include "undecedent/math3d.hpp"
#include "undecedent/runtime_pick.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
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
constexpr float kCloseVertexPixels = 12.0F;
constexpr float kEntityPickPixels = 14.0F;
constexpr float kEntityPickRadius = 12.0F;
constexpr float kGizmoLength = 72.0F;
constexpr float kGizmoPickRadius = 8.0F;
constexpr float kEntityMoveStep = 8.0F;
constexpr std::size_t kEditorHistoryLimit = 128;

void trim_history_stack(std::vector<EditorHistorySnapshot>& stack) {
    if (stack.size() > kEditorHistoryLimit) {
        stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(stack.size() - kEditorHistoryLimit));
    }
}

bool prune_missing_sector_scripts(EditorWorld& editor_world) {
    if (editor_world.scripts.sector_scripts.empty()) {
        return false;
    }

    std::set<std::uint64_t> live_sector_ids;
    for (const SectorPlane& sector : editor_world.sectors) {
        if (sector.id != 0) {
            live_sector_ids.insert(sector.id);
        }
    }

    bool pruned = false;
    for (auto it = editor_world.scripts.sector_scripts.begin();
         it != editor_world.scripts.sector_scripts.end();) {
        if (!live_sector_ids.contains(it->first)) {
            it = editor_world.scripts.sector_scripts.erase(it);
            pruned = true;
        } else {
            ++it;
        }
    }
    return pruned;
}

Vec3 selected_axis_vector(const TranslationGizmoAxis axis) {
    switch (axis) {
    case TranslationGizmoAxis::X: return Vec3{1.0F, 0.0F, 0.0F};
    case TranslationGizmoAxis::Y: return Vec3{0.0F, 1.0F, 0.0F};
    case TranslationGizmoAxis::Z: return Vec3{0.0F, 0.0F, 1.0F};
    case TranslationGizmoAxis::None: break;
    }
    return Vec3{0.0F, 0.0F, 0.0F};
}

float distance_to_ray(const Vec3 point, const Vec3 origin, const Vec3 direction, float& out_t) {
    const Vec3 relative = sub_vec3(point, origin);
    out_t = dot_vec3(relative, direction);
    if (out_t < 0.0F) {
        return std::numeric_limits<float>::max();
    }
    const Vec3 closest = add_vec3(origin, mul_vec3(direction, out_t));
    const Vec3 delta = sub_vec3(point, closest);
    return std::sqrt(dot_vec3(delta, delta));
}

bool closest_ray_axis_t(
    const Vec3 ray_origin,
    const Vec3 ray_direction,
    const Vec3 axis_origin,
    const Vec3 axis_direction,
    float& out_t,
    float& out_distance
) {
    const Vec3 w0 = sub_vec3(ray_origin, axis_origin);
    const float a = dot_vec3(ray_direction, ray_direction);
    const float b = dot_vec3(ray_direction, axis_direction);
    const float c = dot_vec3(axis_direction, axis_direction);
    const float d = dot_vec3(ray_direction, w0);
    const float e = dot_vec3(axis_direction, w0);
    const float denom = (a * c) - (b * b);
    if (std::abs(denom) <= 0.0001F) {
        return false;
    }
    const float ray_t = ((b * e) - (c * d)) / denom;
    const float axis_t = ((a * e) - (b * d)) / denom;
    if (ray_t < 0.0F) {
        return false;
    }
    const Vec3 ray_point = add_vec3(ray_origin, mul_vec3(ray_direction, ray_t));
    const Vec3 axis_point = add_vec3(axis_origin, mul_vec3(axis_direction, axis_t));
    const Vec3 delta = sub_vec3(ray_point, axis_point);
    out_t = axis_t;
    out_distance = std::sqrt(dot_vec3(delta, delta));
    return true;
}

Vec3 normalized_sun_direction(const Vec3 direction) {
    const Vec3 normalized = normalize_vec3(direction);
    if (dot_vec3(normalized, normalized) <= 0.0001F) {
        return WorldLighting{}.sun_direction;
    }
    return normalized;
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

namespace {

bool dragged_ref_matches(
    const std::vector<CommittedVertexRef>& dragged_refs,
    const std::size_t sector_index,
    const std::size_t vertex_index
) {
    return std::any_of(
        dragged_refs.begin(),
        dragged_refs.end(),
        [sector_index, vertex_index](const CommittedVertexRef ref) {
            return ref.sector == sector_index && ref.vertex == vertex_index;
        }
    );
}

bool collapse_duplicate_outer_vertices(
    SectorPlane& sector,
    const std::size_t sector_index,
    const std::vector<CommittedVertexRef>& dragged_refs
) {
    const std::size_t vertex_count = sector.outer.vertices.size();
    if (vertex_count < 2) {
        return false;
    }

    std::vector<bool> keep(vertex_count, true);
    bool changed = false;
    for (std::size_t i = 0; i < vertex_count; ++i) {
        if (!keep[i]) {
            continue;
        }

        std::vector<std::size_t> duplicate_group{i};
        for (std::size_t j = i + 1; j < vertex_count; ++j) {
            if (keep[j] && same_editor_point(sector.outer.vertices[i], sector.outer.vertices[j])) {
                duplicate_group.push_back(j);
            }
        }

        if (duplicate_group.size() <= 1) {
            continue;
        }

        std::size_t kept_index = duplicate_group.front();
        for (const std::size_t index : duplicate_group) {
            if (!dragged_ref_matches(dragged_refs, sector_index, index)) {
                kept_index = index;
                break;
            }
        }

        for (const std::size_t index : duplicate_group) {
            if (index != kept_index) {
                keep[index] = false;
                changed = true;
            }
        }
    }

    if (!changed) {
        return false;
    }

    PolygonLoop welded_outer;
    std::vector<int> welded_wall_materials;
    welded_outer.vertices.reserve(vertex_count);
    welded_wall_materials.reserve(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        if (!keep[i]) {
            continue;
        }
        welded_outer.vertices.push_back(sector.outer.vertices[i]);
        welded_wall_materials.push_back(
            i < sector.wall_materials.size() ? sector.wall_materials[i] : kDefaultMaterialId
        );
    }

    sector.outer = std::move(welded_outer);
    sector.wall_materials = std::move(welded_wall_materials);
    return true;
}

int collapse_drag_duplicate_vertices(EditorWorld& editor_world) {
    int collapsed_vertices = 0;
    for (std::size_t sector_index = 0; sector_index < editor_world.sectors.size(); ++sector_index) {
        SectorPlane& sector = editor_world.sectors[sector_index];
        const std::size_t before = sector.outer.vertices.size();
        if (collapse_duplicate_outer_vertices(sector, sector_index, editor_world.dragged_committed_refs)) {
            const std::size_t after = sector.outer.vertices.size();
            collapsed_vertices += static_cast<int>(before - after);
        }
    }
    return collapsed_vertices;
}

} // namespace

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
    const SurfaceHeightRange floor_range = sector_surface_height_range(sector, SectorSurfaceKind::Floor);
    const SurfaceHeightRange ceiling_range = sector_surface_height_range(sector, SectorSurfaceKind::Ceiling);
    if (editor_world.slice_z >= floor_range.min_height - kGeometryEpsilon &&
        editor_world.slice_z <= ceiling_range.max_height + kGeometryEpsilon) {
        return true;
    }
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

bool same_selected_entity(const SelectedEntityRef a, const SelectedEntityRef b) {
    return a.kind == b.kind && a.point_light_id == b.point_light_id;
}

bool selected_point_light(const EditorWorld& editor_world, PointLight& out_light) {
    if (editor_world.selected_entity.kind != SelectedEntityKind::PointLight ||
        editor_world.selected_entity.point_light_id == 0) {
        return false;
    }
    for (const PointLight& light : point_lights_from_entities(editor_world.entities)) {
        if (light.id == editor_world.selected_entity.point_light_id) {
            out_light = light;
            return true;
        }
    }
    return false;
}

void clear_invalid_entity_selection(EditorWorld& editor_world) {
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::None:
    case SelectedEntityKind::SunLight:
        return;
    case SelectedEntityKind::PlayerSpawn:
        if (!player_spawn_from_entities(editor_world.entities).set) {
            editor_world.selected_entity = {};
        }
        return;
    case SelectedEntityKind::PointLight:
        if (!entity_alive(
                editor_world.entities,
                point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id)
            )) {
            editor_world.selected_entity = {};
        }
        return;
    }
}

void select_entity(EditorWorld& editor_world, SelectedEntityRef entity) {
    ensure_editor_stable_ids(editor_world);
    if (entity.kind == SelectedEntityKind::PlayerSpawn && !player_spawn_from_entities(editor_world.entities).set) {
        entity = {};
    } else if (entity.kind == SelectedEntityKind::PointLight) {
        if (!entity_alive(editor_world.entities, point_light_entity_by_id(editor_world.entities, entity.point_light_id))) {
            entity = {};
        }
    }
    editor_world.selected_entity = entity;
}

void clear_entity_selection(EditorWorld& editor_world) {
    editor_world.selected_entity = {};
}

bool selected_entity_position(const EditorWorld& editor_world, Vec3& out_position) {
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn:
        if (const PlayerSpawn spawn = player_spawn_from_entities(editor_world.entities); spawn.set) {
            out_position = spawn.position;
            return true;
        } else {
            return false;
        }
    case SelectedEntityKind::PointLight:
        if (PointLight light; selected_point_light(editor_world, light)) {
            out_position = light.position;
            return true;
        }
        return false;
    case SelectedEntityKind::None:
    case SelectedEntityKind::SunLight:
        return false;
    }
    return false;
}

bool move_selected_entity(EditorWorld& editor_world, const Vec3 position) {
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn: {
        const EntityHandle entity = player_spawn_entity(editor_world.entities);
        TransformComponent* transform = transform_component(editor_world.entities, entity);
        if (transform == nullptr) {
            return false;
        }
        transform->position = position;
        mark_entities_dirty(editor_world);
        return true;
    }
    case SelectedEntityKind::PointLight:
        if (TransformComponent* transform =
                transform_component(
                    editor_world.entities,
                    point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id)
                )) {
            transform->position = position;
            mark_entities_dirty(editor_world);
            return true;
        }
        return false;
    case SelectedEntityKind::None:
    case SelectedEntityKind::SunLight:
        return false;
    }
    return false;
}

bool delete_selected_entity(EditorWorld& editor_world) {
    switch (editor_world.selected_entity.kind) {
    case SelectedEntityKind::PlayerSpawn: {
        const EntityHandle entity = player_spawn_entity(editor_world.entities);
        if (!entity_alive(editor_world.entities, entity)) {
            return false;
        }
        push_undo_snapshot(editor_world, "delete player spawn");
        destroy_entity(editor_world.entities, entity);
        clear_entity_selection(editor_world);
        mark_entities_dirty(editor_world);
        return true;
    }
    case SelectedEntityKind::PointLight: {
        const EntityHandle entity =
            point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id);
        if (!entity_alive(editor_world.entities, entity)) {
            clear_invalid_entity_selection(editor_world);
            return false;
        }
        push_undo_snapshot(editor_world, "delete point light");
        destroy_entity(editor_world.entities, entity);
        clear_entity_selection(editor_world);
        mark_entities_dirty(editor_world);
        return true;
    }
    case SelectedEntityKind::SunLight:
    case SelectedEntityKind::None:
        return false;
    }
    return false;
}

bool adjust_selected_entity_property(EditorWorld& editor_world, const EntityProperty property, const float delta) {
    const SelectedEntityKind kind = editor_world.selected_entity.kind;
    if (kind == SelectedEntityKind::None) {
        return false;
    }

    push_undo_snapshot(editor_world, "entity property");
    bool changed = true;
    switch (kind) {
    case SelectedEntityKind::PlayerSpawn: {
        const EntityHandle entity = player_spawn_entity(editor_world.entities);
        TransformComponent* transform = transform_component(editor_world.entities, entity);
        if (transform == nullptr) {
            changed = false;
            break;
        }
        switch (property) {
        case EntityProperty::PositionX: transform->position.x += delta; break;
        case EntityProperty::PositionY: transform->position.y += delta; break;
        case EntityProperty::PositionZ: transform->position.z += delta; break;
        case EntityProperty::Yaw: transform->yaw += delta; break;
        default: changed = false; break;
        }
        if (changed) {
            mark_entities_dirty(editor_world);
        }
        break;
    }
    case SelectedEntityKind::PointLight:
        if (const EntityHandle entity =
                point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id);
            entity_alive(editor_world.entities, entity)) {
            TransformComponent* transform = transform_component(editor_world.entities, entity);
            PointLightComponent* light = point_light_component(editor_world.entities, entity);
            if (transform == nullptr || light == nullptr) {
                changed = false;
                break;
            }
            switch (property) {
            case EntityProperty::PositionX: transform->position.x += delta; break;
            case EntityProperty::PositionY: transform->position.y += delta; break;
            case EntityProperty::PositionZ: transform->position.z += delta; break;
            case EntityProperty::ColorR: light->color.x = std::max(0.0F, light->color.x + delta); break;
            case EntityProperty::ColorG: light->color.y = std::max(0.0F, light->color.y + delta); break;
            case EntityProperty::ColorB: light->color.z = std::max(0.0F, light->color.z + delta); break;
            case EntityProperty::Radius: light->radius = std::max(1.0F, light->radius + delta); break;
            case EntityProperty::Intensity: light->intensity = std::max(0.0F, light->intensity + delta); break;
            case EntityProperty::ShadowBias: light->shadow_bias = std::max(0.0F, light->shadow_bias + delta); break;
            default: changed = false; break;
            }
            if (changed) {
                mark_entities_dirty(editor_world);
            }
        } else {
            changed = false;
        }
        break;
    case SelectedEntityKind::SunLight:
        switch (property) {
        case EntityProperty::SunEnabled:
            editor_world.world_lighting.sun_enabled = !editor_world.world_lighting.sun_enabled;
            break;
        case EntityProperty::SunDirectionX:
            editor_world.world_lighting.sun_direction.x += delta;
            editor_world.world_lighting.sun_direction = normalized_sun_direction(editor_world.world_lighting.sun_direction);
            break;
        case EntityProperty::SunDirectionY:
            editor_world.world_lighting.sun_direction.y += delta;
            editor_world.world_lighting.sun_direction = normalized_sun_direction(editor_world.world_lighting.sun_direction);
            break;
        case EntityProperty::SunDirectionZ:
            editor_world.world_lighting.sun_direction.z += delta;
            editor_world.world_lighting.sun_direction = normalized_sun_direction(editor_world.world_lighting.sun_direction);
            break;
        case EntityProperty::ColorR:
            editor_world.world_lighting.sun_color.x = std::max(0.0F, editor_world.world_lighting.sun_color.x + delta);
            break;
        case EntityProperty::ColorG:
            editor_world.world_lighting.sun_color.y = std::max(0.0F, editor_world.world_lighting.sun_color.y + delta);
            break;
        case EntityProperty::ColorB:
            editor_world.world_lighting.sun_color.z = std::max(0.0F, editor_world.world_lighting.sun_color.z + delta);
            break;
        case EntityProperty::Intensity:
            editor_world.world_lighting.sun_intensity = std::max(0.0F, editor_world.world_lighting.sun_intensity + delta);
            break;
        default:
            changed = false;
            break;
        }
        if (changed) {
            editor_world.dirty_metadata = true;
        }
        break;
    case SelectedEntityKind::None:
        changed = false;
        break;
    }

    if (!changed && !editor_world.undo_stack.empty()) {
        editor_world.undo_stack.pop_back();
    }
    return changed;
}

EntityPick pick_editor_entity_2d(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const float player_eye_height
) {
    EntityPick best{};
    best.distance = kEntityPickPixels * kEntityPickPixels;
    const auto consider = [&](const SelectedEntityRef entity, const Vec3 position, const float slice_height) {
        const float visible_band = std::max(1.0F, editor_grid_world_step(camera.zoom) * 0.5F);
        if (std::abs(slice_height - editor_world.slice_z) > visible_band) {
            return;
        }
        const float x = world_to_screen_x(position.x, width, camera);
        const float y = world_to_screen_y(position.z, height, camera);
        const float dx = x - screen_x;
        const float dy = y - screen_y;
        const float distance = (dx * dx) + (dy * dy);
        if (distance <= best.distance) {
            best.hit = true;
            best.entity = entity;
            best.distance = distance;
        }
    };

    if (const PlayerSpawn spawn = player_spawn_from_entities(editor_world.entities); spawn.set) {
        consider(
            SelectedEntityRef{SelectedEntityKind::PlayerSpawn, 0},
            spawn.position,
            spawn.position.y - player_eye_height
        );
    }
    for (const PointLight& light : point_lights_from_entities(editor_world.entities)) {
        consider(SelectedEntityRef{SelectedEntityKind::PointLight, light.id}, light.position, light.position.y);
    }
    return best;
}

EntityPick pick_editor_entity_3d(
    const EditorWorld& editor_world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const GameRenderConfig& config
) {
    EntityPick best{};
    best.distance = std::numeric_limits<float>::max();
    const Vec3 origin{camera.x, camera.y, camera.z};
    const Vec3 direction = camera_ray_direction(camera, width, height, screen_x, screen_y, config.fov_y_degrees);
    const auto consider = [&](const SelectedEntityRef entity, const Vec3 position) {
        float ray_t = 0.0F;
        const float distance = distance_to_ray(position, origin, direction, ray_t);
        const float threshold = std::max(kEntityPickRadius, ray_t * 0.018F);
        if (distance <= threshold && ray_t < best.distance) {
            best.hit = true;
            best.entity = entity;
            best.distance = ray_t;
        }
    };

    if (const PlayerSpawn spawn = player_spawn_from_entities(editor_world.entities); spawn.set) {
        consider(SelectedEntityRef{SelectedEntityKind::PlayerSpawn, 0}, spawn.position);
    }
    for (const PointLight& light : point_lights_from_entities(editor_world.entities)) {
        consider(SelectedEntityRef{SelectedEntityKind::PointLight, light.id}, light.position);
    }
    return best;
}

TranslationGizmoPick pick_translation_gizmo(
    const EditorWorld& editor_world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const GameRenderConfig& config
) {
    Vec3 origin{};
    if (!selected_entity_position(editor_world, origin)) {
        return {};
    }

    const Vec3 ray_origin{camera.x, camera.y, camera.z};
    const Vec3 ray_direction = camera_ray_direction(camera, width, height, screen_x, screen_y, config.fov_y_degrees);
    TranslationGizmoPick best{};
    float best_distance = std::numeric_limits<float>::max();
    for (const TranslationGizmoAxis axis : {TranslationGizmoAxis::X, TranslationGizmoAxis::Y, TranslationGizmoAxis::Z}) {
        const Vec3 axis_direction = selected_axis_vector(axis);
        float axis_t = 0.0F;
        float distance = 0.0F;
        if (!closest_ray_axis_t(ray_origin, ray_direction, origin, axis_direction, axis_t, distance)) {
            continue;
        }
        if (axis_t < 0.0F || axis_t > kGizmoLength) {
            continue;
        }
        if (distance <= kGizmoPickRadius && distance < best_distance) {
            best.hit = true;
            best.axis = axis;
            best.axis_t = axis_t;
            best_distance = distance;
        }
    }
    return best;
}

bool start_translation_gizmo_drag(
    EditorWorld& editor_world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const GameRenderConfig& config
) {
    const TranslationGizmoPick pick = pick_translation_gizmo(editor_world, camera, width, height, screen_x, screen_y, config);
    if (!pick.hit) {
        return false;
    }
    Vec3 position{};
    if (!selected_entity_position(editor_world, position)) {
        return false;
    }
    push_undo_snapshot(editor_world, "entity move");
    editor_world.has_dragged_entity_gizmo = true;
    editor_world.dragged_entity_axis = pick.axis;
    editor_world.dragged_entity_moved = false;
    editor_world.entity_drag_start_axis_t = pick.axis_t;
    editor_world.entity_drag_start_position = position;
    editor_world.entity_drag_current_position = position;
    return true;
}

bool update_translation_gizmo_drag(
    EditorWorld& editor_world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const GameRenderConfig& config,
    const bool fine
) {
    if (!editor_world.has_dragged_entity_gizmo) {
        return false;
    }

    const Vec3 ray_origin{camera.x, camera.y, camera.z};
    const Vec3 ray_direction = camera_ray_direction(camera, width, height, screen_x, screen_y, config.fov_y_degrees);
    const Vec3 axis_direction = selected_axis_vector(editor_world.dragged_entity_axis);
    float axis_t = 0.0F;
    float distance = 0.0F;
    if (!closest_ray_axis_t(
            ray_origin,
            ray_direction,
            editor_world.entity_drag_start_position,
            axis_direction,
            axis_t,
            distance
        )) {
        return false;
    }

    const float step = fine ? 1.0F : kEntityMoveStep;
    const float snapped_delta = std::round((axis_t - editor_world.entity_drag_start_axis_t) / step) * step;
    const Vec3 next_position =
        add_vec3(editor_world.entity_drag_start_position, mul_vec3(axis_direction, snapped_delta));
    if (std::abs(next_position.x - editor_world.entity_drag_current_position.x) <= kGeometryEpsilon &&
        std::abs(next_position.y - editor_world.entity_drag_current_position.y) <= kGeometryEpsilon &&
        std::abs(next_position.z - editor_world.entity_drag_current_position.z) <= kGeometryEpsilon) {
        return true;
    }
    if (move_selected_entity(editor_world, next_position)) {
        editor_world.entity_drag_current_position = next_position;
        editor_world.dragged_entity_moved = true;
    }
    return true;
}

void finish_translation_gizmo_drag(EditorWorld& editor_world) {
    if (!editor_world.has_dragged_entity_gizmo) {
        return;
    }
    if (!editor_world.dragged_entity_moved && !editor_world.undo_stack.empty()) {
        editor_world.undo_stack.pop_back();
    }
    editor_world.has_dragged_entity_gizmo = false;
    editor_world.dragged_entity_moved = false;
    editor_world.dragged_entity_axis = TranslationGizmoAxis::None;
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

void rebuild_runtime_geometry(EditorWorld& editor_world, const bool shadow_geometry_changed) {
    editor_world.runtime_world = build_runtime_world(editor_world.sectors);
    rebuild_runtime_render_cache(editor_world.runtime_render_cache, editor_world.runtime_world);
    if (shadow_geometry_changed) {
        editor_world.runtime_render_cache.shadow_revision = ++editor_world.shadow_revision_counter;
    } else {
        editor_world.runtime_render_cache.shadow_revision = editor_world.shadow_revision_counter;
    }
}

MapDirtyState editor_map_dirty_state(const EditorWorld& editor_world) {
    return MapDirtyState{
        editor_world.dirty_sector_ids,
        editor_world.dirty_entities,
        editor_world.dirty_metadata,
        editor_world.dirty_materials,
        editor_world.dirty_topology,
        editor_world.dirty_scripts,
    };
}

void clear_map_dirty_state(EditorWorld& editor_world) {
    editor_world.dirty_sector_ids.clear();
    editor_world.dirty_entities = false;
    editor_world.dirty_metadata = false;
    editor_world.dirty_materials = false;
    editor_world.dirty_topology = false;
    editor_world.dirty_scripts = false;
}

void mark_sector_dirty(EditorWorld& editor_world, const std::size_t sector_index) {
    if (sector_index >= editor_world.sectors.size()) {
        editor_world.dirty_topology = true;
        return;
    }
    const std::uint64_t id = editor_world.sectors[sector_index].id;
    if (id == 0) {
        editor_world.dirty_topology = true;
        return;
    }
    editor_world.dirty_sector_ids.insert(id);
}

void mark_entities_dirty(EditorWorld& editor_world) {
    editor_world.dirty_entities = true;
}

void mark_topology_dirty(EditorWorld& editor_world) {
    ensure_editor_stable_ids(editor_world);
    if (prune_missing_sector_scripts(editor_world)) {
        editor_world.dirty_scripts = true;
    }
    editor_world.dirty_topology = true;
    editor_world.dirty_sector_ids.clear();
}

void ensure_editor_stable_ids(EditorWorld& editor_world) {
    std::set<std::uint64_t> used;
    std::uint64_t next_id = 1;
    auto ensure_id = [&used, &next_id](std::uint64_t& id) {
        if (id != 0 && !used.contains(id)) {
            used.insert(id);
            next_id = std::max(next_id, id + 1U);
            return false;
        }
        while (next_id == 0 || used.contains(next_id)) {
            ++next_id;
        }
        id = next_id++;
        used.insert(id);
        return true;
    };

    bool assigned = false;
    for (SectorPlane& sector : editor_world.sectors) {
        assigned = ensure_id(sector.id) || assigned;
    }
    EntityHandle selected_point_light_handle;
    if (editor_world.selected_entity.kind == SelectedEntityKind::PointLight) {
        selected_point_light_handle =
            point_light_entity_by_id(editor_world.entities, editor_world.selected_entity.point_light_id);
    }
    assigned = ensure_entity_stable_ids(editor_world.entities, used, next_id) || assigned;
    if (entity_alive(editor_world.entities, selected_point_light_handle)) {
        editor_world.selected_entity.point_light_id =
            entity_stable_id(editor_world.entities, selected_point_light_handle);
    }
    if (assigned) {
        editor_world.dirty_metadata = true;
    }
}

EditorHistorySnapshot make_history_snapshot(const EditorWorld& editor_world) {
    return EditorHistorySnapshot{
        editor_world.sectors,
        editor_world.selected_sectors,
        editor_world.entities,
        editor_world.world_lighting,
        editor_world.scripts,
        editor_world.slice_z,
        editor_world.selected_sector,
        editor_world.selected_entity,
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
    editor_world.entities = snapshot.entities;
    editor_world.world_lighting = snapshot.world_lighting;
    editor_world.scripts = snapshot.scripts;
    editor_world.slice_z = snapshot.slice_z;
    editor_world.selected_sector = snapshot.selected_sector;
    editor_world.selected_entity = snapshot.selected_entity;
    editor_world.slice_scroll_remainder = 0.0F;
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.dragged_draft_vertex = -1;
    editor_world.dragged_draft_vertex_moved = false;
    editor_world.committed_drag_snapshot.clear();
    editor_world.dragged_committed_refs.clear();
    editor_world.has_dragged_committed_vertex = false;
    editor_world.dragged_committed_vertex_moved = false;
    editor_world.has_dragged_entity_gizmo = false;
    editor_world.dragged_entity_moved = false;
    editor_world.dragged_entity_axis = TranslationGizmoAxis::None;
    editor_world.has_hovered_committed_vertex = false;
    editor_world.plane_tool = PlaneToolMode::None;
    clear_selection_outside_slice(editor_world);
    clear_invalid_entity_selection(editor_world);
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
    mark_topology_dirty(editor_world);
    mark_entities_dirty(editor_world);
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
    mark_topology_dirty(editor_world);
    mark_entities_dirty(editor_world);
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
    EntityHandle placed_entity;
    switch (editor_world.entity_placement) {
    case EntityPlacementType::PlayerSpawn: {
        PlayerSpawn spawn;
        spawn.position = position;
        spawn.yaw = yaw;
        spawn.set = true;
        placed_entity = set_player_spawn_entity(editor_world.entities, spawn);
        ensure_editor_stable_ids(editor_world);
        select_entity(editor_world, SelectedEntityRef{SelectedEntityKind::PlayerSpawn, 0});
        std::cout << "Placed player spawn at " << position.x << ", " << position.y << ", " << position.z << '\n';
        break;
    }
    case EntityPlacementType::PointLight: {
        placed_entity = add_point_light_entity(editor_world.entities, default_point_light_at(position));
        ensure_editor_stable_ids(editor_world);
        select_entity(
            editor_world,
            SelectedEntityRef{SelectedEntityKind::PointLight, entity_stable_id(editor_world.entities, placed_entity)}
        );
        std::cout << "Placed point light at " << position.x << ", " << position.y << ", " << position.z << '\n';
        break;
    }
    }
    mark_entities_dirty(editor_world);
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
    PlayerSpawn spawn;
    spawn.position = Vec3{origin.x, origin.y + player_eye_height, origin.z};
    spawn.yaw = yaw;
    spawn.set = true;
    set_player_spawn_entity(editor_world.entities, spawn);
    ensure_editor_stable_ids(editor_world);
    select_entity(editor_world, SelectedEntityRef{SelectedEntityKind::PlayerSpawn, 0});
    mark_entities_dirty(editor_world);
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

    rebuild_runtime_geometry(editor_world, false);
    mark_sector_dirty(editor_world, static_cast<std::size_t>(pick.sector_id));
    return true;
}

bool sculpt_displacement_at_pick(EditorWorld& editor_world, const SurfacePick& pick, const float delta) {
    if (!pick.hit || pick.sector_id < 0 || pick.sector_id >= static_cast<int>(editor_world.sectors.size())) {
        return false;
    }
    SectorSurfaceKind surface{};
    if (pick.surface.kind == RuntimeSurfaceKind::Floor) {
        surface = SectorSurfaceKind::Floor;
    } else if (pick.surface.kind == RuntimeSurfaceKind::Ceiling) {
        surface = SectorSurfaceKind::Ceiling;
    } else {
        return false;
    }

    SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(pick.sector_id)];
    push_undo_snapshot(editor_world, "sculpt displacement");
    const bool changed = sculpt_surface_displacement(
        sector,
        surface,
        Vec2{pick.point.x, pick.point.z},
        editor_world.displacement_brush_radius,
        delta
    );
    if (!changed) {
        editor_world.undo_stack.pop_back();
        return false;
    }
    rebuild_runtime_geometry(editor_world);
    mark_sector_dirty(editor_world, static_cast<std::size_t>(pick.sector_id));
    clear_selection_outside_slice(editor_world);
    return true;
}

void adjust_displacement_brush_radius(EditorWorld& editor_world, const float delta) {
    editor_world.displacement_brush_radius =
        std::clamp(editor_world.displacement_brush_radius + delta, 4.0F, 512.0F);
    std::cout << "Displacement brush radius: "
              << format_world_units(editor_world.displacement_brush_radius)
              << "u\n";
}

int selected_sector_subdivision(const EditorWorld& editor_world) {
    if (editor_world.selected_sector < 0 ||
        editor_world.selected_sector >= static_cast<int>(editor_world.sectors.size())) {
        return 0;
    }

    const SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)];
    int subdivision = 0;
    if (sector.floor_displacement.enabled) {
        subdivision = std::max(subdivision, clamped_displacement_resolution(sector.floor_displacement.resolution));
    }
    if (sector.ceiling_displacement.enabled) {
        subdivision = std::max(subdivision, clamped_displacement_resolution(sector.ceiling_displacement.resolution));
    }
    return subdivision;
}

bool adjust_selected_sector_subdivision(EditorWorld& editor_world, const int delta) {
    if (editor_world.selected_sectors.empty()) {
        std::cout << "Select at least one sector before adjusting subdivision.\n";
        return false;
    }

    struct Target {
        int sector_index = -1;
        int current = 0;
        int next = 0;
    };

    std::vector<Target> targets;
    for (const int sector_index : editor_world.selected_sectors) {
        if (sector_index < 0 || sector_index >= static_cast<int>(editor_world.sectors.size())) {
            continue;
        }
        const SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(sector_index)];
        int current = 0;
        if (sector.floor_displacement.enabled) {
            current = std::max(current, clamped_displacement_resolution(sector.floor_displacement.resolution));
        }
        if (sector.ceiling_displacement.enabled) {
            current = std::max(current, clamped_displacement_resolution(sector.ceiling_displacement.resolution));
        }

        int next = current + delta;
        if (delta > 0 && current == 0) {
            next = 1;
        }
        next = std::clamp(next, 0, kMaxDisplacementResolution);
        if (next != current) {
            targets.push_back(Target{sector_index, current, next});
        }
    }

    if (targets.empty()) {
        std::cout << "Selected sector subdivision is already at the limit.\n";
        return false;
    }

    push_undo_snapshot(editor_world, "sector subdivision");
    for (const Target target : targets) {
        SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(target.sector_index)];
        if (target.next == 0) {
            sector.floor_displacement.enabled = false;
            sector.floor_displacement.samples.clear();
            sector.ceiling_displacement.enabled = false;
            sector.ceiling_displacement.samples.clear();
        } else {
            set_displacement_resolution(sector, SectorSurfaceKind::Floor, target.next);
            set_displacement_resolution(sector, SectorSurfaceKind::Ceiling, target.next);
        }
        mark_sector_dirty(editor_world, static_cast<std::size_t>(target.sector_index));
    }

    rebuild_runtime_geometry(editor_world);
    if (selected_sector_subdivision(editor_world) == 0) {
        editor_world.displacement_sculpt_enabled = false;
    }
    std::cout << "Adjusted sector subdivision for " << targets.size() << " sectors.\n";
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
        mark_topology_dirty(editor_world);
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
        mark_topology_dirty(editor_world);
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
        mark_topology_dirty(editor_world);
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
    mark_topology_dirty(editor_world);
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
    mark_topology_dirty(editor_world);
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
        mark_sector_dirty(editor_world, static_cast<std::size_t>(sector_index));
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
        mark_sector_dirty(editor_world, static_cast<std::size_t>(sector_index));
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
        const int welded_vertices = collapse_drag_duplicate_vertices(editor_world);
        const CsgAddResult rebuild_result = csg_rebuild_sectors(editor_world.sectors);
        if (rebuild_result.ok) {
            EditorHistorySnapshot snapshot = make_history_snapshot(editor_world);
            snapshot.sectors = editor_world.committed_drag_snapshot;
            push_undo_snapshot(editor_world, std::move(snapshot), "vertex move");
            editor_world.sectors = rebuild_result.sectors;
            mark_topology_dirty(editor_world);
            select_single_sector(editor_world, editor_world.selected_sector);
            clear_selection_outside_slice(editor_world);
            rebuild_runtime_geometry(editor_world);
            if (welded_vertices > 0) {
                std::cout << "Merged " << welded_vertices << " duplicate vertices after drag.\n";
            }
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
