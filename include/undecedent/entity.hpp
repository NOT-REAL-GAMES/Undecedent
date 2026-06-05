#pragma once

#include "undecedent/geometry.hpp"

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace undecedent {

constexpr std::uint32_t kInvalidEntityIndex = std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t kEntityClassEntity = 1;
constexpr std::uint32_t kEntityClassPlayerSpawn = 2;
constexpr std::uint32_t kEntityClassPointLight = 3;

constexpr std::uint32_t kComponentTransform = 1;
constexpr std::uint32_t kComponentPlayerSpawn = 2;
constexpr std::uint32_t kComponentPointLight = 3;
constexpr std::uint32_t kComponentName = 4;
constexpr std::uint32_t kComponentScript = 5;

struct EntityHandle {
    std::uint32_t index = kInvalidEntityIndex;
    std::uint32_t generation = 0;
};

struct TransformComponent {
    Vec3 position{0.0F, 0.0F, 0.0F};
    float yaw = 0.0F;
};

struct PlayerSpawnComponent {
    bool set = true;
};

struct PointLightComponent {
    Vec3 color{1.0F, 0.86F, 0.62F};
    float radius = 384.0F;
    float intensity = 1.5F;
    float shadow_bias = 2.0F;
};

struct NameComponent {
    std::string name;
};

struct ScriptComponent {
    std::uint64_t script_id = 0;
    bool enabled = true;
};

struct EntityRegistry {
    struct Slot {
        std::uint64_t stable_id = 0;
        std::uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_indices;
    std::unordered_map<std::uint64_t, EntityHandle> stable_lookup;

    std::vector<EntityHandle> transform_owners;
    std::vector<TransformComponent> transforms;
    std::vector<int> transform_sparse;

    std::vector<EntityHandle> player_spawn_owners;
    std::vector<PlayerSpawnComponent> player_spawns;
    std::vector<int> player_spawn_sparse;

    std::vector<EntityHandle> point_light_owners;
    std::vector<PointLightComponent> point_lights;
    std::vector<int> point_light_sparse;

    std::vector<EntityHandle> name_owners;
    std::vector<NameComponent> names;
    std::vector<int> name_sparse;

    std::vector<EntityHandle> script_owners;
    std::vector<ScriptComponent> scripts;
    std::vector<int> script_sparse;
};

bool operator==(EntityHandle a, EntityHandle b);
bool operator!=(EntityHandle a, EntityHandle b);

EntityHandle create_entity(EntityRegistry& registry, std::uint64_t stable_id = 0);
bool destroy_entity(EntityRegistry& registry, EntityHandle entity);
bool entity_alive(const EntityRegistry& registry, EntityHandle entity);
std::uint64_t entity_stable_id(const EntityRegistry& registry, EntityHandle entity);
EntityHandle find_entity_by_stable_id(const EntityRegistry& registry, std::uint64_t stable_id);
bool ensure_entity_stable_ids(EntityRegistry& registry, std::set<std::uint64_t>& used, std::uint64_t& next_id);

TransformComponent* add_transform(EntityRegistry& registry, EntityHandle entity, TransformComponent component = {});
TransformComponent* transform_component(EntityRegistry& registry, EntityHandle entity);
const TransformComponent* transform_component(const EntityRegistry& registry, EntityHandle entity);

PlayerSpawnComponent* add_player_spawn(EntityRegistry& registry, EntityHandle entity, PlayerSpawnComponent component = {});
PlayerSpawnComponent* player_spawn_component(EntityRegistry& registry, EntityHandle entity);
const PlayerSpawnComponent* player_spawn_component(const EntityRegistry& registry, EntityHandle entity);

PointLightComponent* add_point_light(EntityRegistry& registry, EntityHandle entity, PointLightComponent component = {});
PointLightComponent* point_light_component(EntityRegistry& registry, EntityHandle entity);
const PointLightComponent* point_light_component(const EntityRegistry& registry, EntityHandle entity);

NameComponent* add_name(EntityRegistry& registry, EntityHandle entity, NameComponent component = {});
NameComponent* name_component(EntityRegistry& registry, EntityHandle entity);
const NameComponent* name_component(const EntityRegistry& registry, EntityHandle entity);

ScriptComponent* add_script(EntityRegistry& registry, EntityHandle entity, ScriptComponent component = {});
ScriptComponent* script_component(EntityRegistry& registry, EntityHandle entity);
const ScriptComponent* script_component(const EntityRegistry& registry, EntityHandle entity);

EntityHandle player_spawn_entity(const EntityRegistry& registry);
EntityHandle point_light_entity_by_id(const EntityRegistry& registry, std::uint64_t stable_id);
std::uint32_t entity_class_type(const EntityRegistry& registry, EntityHandle entity);
std::uint64_t entity_class_ancestry(const EntityRegistry& registry, EntityHandle entity);
std::uint64_t entity_component_mask(const EntityRegistry& registry, EntityHandle entity);
std::uint32_t entity_class_type(const EntityRegistry& registry, std::uint64_t stable_id);
std::uint64_t entity_class_ancestry(const EntityRegistry& registry, std::uint64_t stable_id);
std::uint64_t entity_component_mask(const EntityRegistry& registry, std::uint64_t stable_id);
bool entity_is_a(const EntityRegistry& registry, std::uint64_t stable_id, std::uint32_t class_id);
bool entity_has_component(const EntityRegistry& registry, std::uint64_t stable_id, std::uint32_t component_id);
PlayerSpawn player_spawn_from_entities(const EntityRegistry& registry);
std::vector<PointLight> point_lights_from_entities(const EntityRegistry& registry);
EntityRegistry entity_registry_from_authored_entities(PlayerSpawn player_spawn, const std::vector<PointLight>& point_lights);
EntityHandle set_player_spawn_entity(EntityRegistry& registry, PlayerSpawn player_spawn);
EntityHandle add_point_light_entity(EntityRegistry& registry, PointLight point_light);
bool update_point_light_entity(EntityRegistry& registry, std::uint64_t stable_id, PointLight point_light);

} // namespace undecedent
