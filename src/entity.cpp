#include "undecedent/entity.hpp"

#include <algorithm>

namespace undecedent {
namespace {

constexpr int kMissingComponent = -1;

bool valid_slot_index(const EntityRegistry& registry, const std::uint32_t index) {
    return index != kInvalidEntityIndex && index < registry.slots.size();
}

void rebuild_stable_lookup(EntityRegistry& registry) {
    registry.stable_lookup.clear();
    for (std::uint32_t i = 0; i < registry.slots.size(); ++i) {
        const EntityRegistry::Slot& slot = registry.slots[i];
        if (slot.alive && slot.stable_id != 0) {
            registry.stable_lookup[slot.stable_id] = EntityHandle{i, slot.generation};
        }
    }
}

void ensure_sparse_size(std::vector<int>& sparse, const EntityHandle entity) {
    if (entity.index >= sparse.size()) {
        sparse.resize(static_cast<std::size_t>(entity.index) + 1U, kMissingComponent);
    }
}

template<typename Component>
Component* component_at(
    const EntityRegistry& registry,
    const EntityHandle entity,
    const std::vector<int>& sparse,
    std::vector<Component>& data
) {
    if (!entity_alive(registry, entity) || entity.index >= sparse.size()) {
        return nullptr;
    }
    const int dense_index = sparse[entity.index];
    if (dense_index < 0 || dense_index >= static_cast<int>(data.size())) {
        return nullptr;
    }
    return &data[static_cast<std::size_t>(dense_index)];
}

template<typename Component>
const Component* component_at(
    const EntityRegistry& registry,
    const EntityHandle entity,
    const std::vector<int>& sparse,
    const std::vector<Component>& data
) {
    if (!entity_alive(registry, entity) || entity.index >= sparse.size()) {
        return nullptr;
    }
    const int dense_index = sparse[entity.index];
    if (dense_index < 0 || dense_index >= static_cast<int>(data.size())) {
        return nullptr;
    }
    return &data[static_cast<std::size_t>(dense_index)];
}

template<typename Component>
Component* add_component(
    const EntityRegistry& registry,
    const EntityHandle entity,
    std::vector<EntityHandle>& owners,
    std::vector<Component>& data,
    std::vector<int>& sparse,
    Component component
) {
    if (!entity_alive(registry, entity)) {
        return nullptr;
    }

    ensure_sparse_size(sparse, entity);
    const int existing = sparse[entity.index];
    if (existing >= 0 && existing < static_cast<int>(data.size())) {
        data[static_cast<std::size_t>(existing)] = component;
        return &data[static_cast<std::size_t>(existing)];
    }

    sparse[entity.index] = static_cast<int>(data.size());
    owners.push_back(entity);
    data.push_back(component);
    return &data.back();
}

template<typename Component>
void remove_component(
    const EntityHandle entity,
    std::vector<EntityHandle>& owners,
    std::vector<Component>& data,
    std::vector<int>& sparse
) {
    if (entity.index >= sparse.size()) {
        return;
    }
    const int dense_index = sparse[entity.index];
    if (dense_index < 0 || dense_index >= static_cast<int>(data.size())) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(dense_index);
    const std::size_t last = data.size() - 1U;
    if (index != last) {
        data[index] = data[last];
        owners[index] = owners[last];
        sparse[owners[index].index] = static_cast<int>(index);
    }

    data.pop_back();
    owners.pop_back();
    sparse[entity.index] = kMissingComponent;
}

PointLight authored_point_light(
    const std::uint64_t id,
    const TransformComponent& transform,
    const PointLightComponent& component
) {
    PointLight light;
    light.id = id;
    light.position = transform.position;
    light.color = component.color;
    light.radius = component.radius;
    light.intensity = component.intensity;
    light.shadow_bias = component.shadow_bias;
    return light;
}

PointLightComponent point_light_component_from_authored(PointLight light) {
    PointLightComponent component;
    component.color = light.color;
    component.radius = light.radius;
    component.intensity = light.intensity;
    component.shadow_bias = light.shadow_bias;
    return component;
}

} // namespace

bool operator==(const EntityHandle a, const EntityHandle b) {
    return a.index == b.index && a.generation == b.generation;
}

bool operator!=(const EntityHandle a, const EntityHandle b) {
    return !(a == b);
}

EntityHandle create_entity(EntityRegistry& registry, const std::uint64_t stable_id) {
    EntityHandle entity;
    if (!registry.free_indices.empty()) {
        entity.index = registry.free_indices.back();
        registry.free_indices.pop_back();
        EntityRegistry::Slot& slot = registry.slots[entity.index];
        slot.alive = true;
        slot.stable_id = stable_id;
        entity.generation = slot.generation;
    } else {
        entity.index = static_cast<std::uint32_t>(registry.slots.size());
        entity.generation = 1;
        registry.slots.push_back(EntityRegistry::Slot{stable_id, entity.generation, true});
    }

    if (stable_id != 0) {
        registry.stable_lookup[stable_id] = entity;
    }
    return entity;
}

bool destroy_entity(EntityRegistry& registry, const EntityHandle entity) {
    if (!entity_alive(registry, entity)) {
        return false;
    }

    remove_component(entity, registry.transform_owners, registry.transforms, registry.transform_sparse);
    remove_component(entity, registry.player_spawn_owners, registry.player_spawns, registry.player_spawn_sparse);
    remove_component(entity, registry.point_light_owners, registry.point_lights, registry.point_light_sparse);
    remove_component(entity, registry.name_owners, registry.names, registry.name_sparse);
    remove_component(entity, registry.script_owners, registry.scripts, registry.script_sparse);

    EntityRegistry::Slot& slot = registry.slots[entity.index];
    if (slot.stable_id != 0) {
        registry.stable_lookup.erase(slot.stable_id);
    }
    slot.alive = false;
    slot.stable_id = 0;
    ++slot.generation;
    registry.free_indices.push_back(entity.index);
    return true;
}

bool entity_alive(const EntityRegistry& registry, const EntityHandle entity) {
    return valid_slot_index(registry, entity.index) &&
        registry.slots[entity.index].alive &&
        registry.slots[entity.index].generation == entity.generation;
}

std::uint64_t entity_stable_id(const EntityRegistry& registry, const EntityHandle entity) {
    if (!entity_alive(registry, entity)) {
        return 0;
    }
    return registry.slots[entity.index].stable_id;
}

EntityHandle find_entity_by_stable_id(const EntityRegistry& registry, const std::uint64_t stable_id) {
    if (stable_id == 0) {
        return {};
    }
    const auto found = registry.stable_lookup.find(stable_id);
    if (found == registry.stable_lookup.end()) {
        return {};
    }
    return entity_alive(registry, found->second) ? found->second : EntityHandle{};
}

bool ensure_entity_stable_ids(EntityRegistry& registry, std::set<std::uint64_t>& used, std::uint64_t& next_id) {
    bool assigned = false;
    for (EntityRegistry::Slot& slot : registry.slots) {
        if (!slot.alive) {
            continue;
        }

        if (slot.stable_id != 0 && !used.contains(slot.stable_id)) {
            used.insert(slot.stable_id);
            next_id = std::max(next_id, slot.stable_id + 1U);
            continue;
        }

        while (next_id == 0 || used.contains(next_id)) {
            ++next_id;
        }
        slot.stable_id = next_id++;
        used.insert(slot.stable_id);
        assigned = true;
    }
    rebuild_stable_lookup(registry);
    return assigned;
}

std::uint64_t bit_for_id(const std::uint32_t id) {
    return id < 64U ? (1ULL << id) : 0ULL;
}

TransformComponent* add_transform(EntityRegistry& registry, const EntityHandle entity, const TransformComponent component) {
    return add_component(registry, entity, registry.transform_owners, registry.transforms, registry.transform_sparse, component);
}

TransformComponent* transform_component(EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.transform_sparse, registry.transforms);
}

const TransformComponent* transform_component(const EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.transform_sparse, registry.transforms);
}

PlayerSpawnComponent* add_player_spawn(EntityRegistry& registry, const EntityHandle entity, const PlayerSpawnComponent component) {
    return add_component(registry, entity, registry.player_spawn_owners, registry.player_spawns, registry.player_spawn_sparse, component);
}

PlayerSpawnComponent* player_spawn_component(EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.player_spawn_sparse, registry.player_spawns);
}

const PlayerSpawnComponent* player_spawn_component(const EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.player_spawn_sparse, registry.player_spawns);
}

PointLightComponent* add_point_light(EntityRegistry& registry, const EntityHandle entity, const PointLightComponent component) {
    return add_component(registry, entity, registry.point_light_owners, registry.point_lights, registry.point_light_sparse, component);
}

PointLightComponent* point_light_component(EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.point_light_sparse, registry.point_lights);
}

const PointLightComponent* point_light_component(const EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.point_light_sparse, registry.point_lights);
}

NameComponent* add_name(EntityRegistry& registry, const EntityHandle entity, const NameComponent component) {
    return add_component(registry, entity, registry.name_owners, registry.names, registry.name_sparse, component);
}

NameComponent* name_component(EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.name_sparse, registry.names);
}

const NameComponent* name_component(const EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.name_sparse, registry.names);
}

ScriptComponent* add_script(EntityRegistry& registry, const EntityHandle entity, const ScriptComponent component) {
    return add_component(registry, entity, registry.script_owners, registry.scripts, registry.script_sparse, component);
}

ScriptComponent* script_component(EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.script_sparse, registry.scripts);
}

const ScriptComponent* script_component(const EntityRegistry& registry, const EntityHandle entity) {
    return component_at(registry, entity, registry.script_sparse, registry.scripts);
}

EntityHandle player_spawn_entity(const EntityRegistry& registry) {
    for (const EntityHandle entity : registry.player_spawn_owners) {
        if (entity_alive(registry, entity) && transform_component(registry, entity) != nullptr) {
            return entity;
        }
    }
    return {};
}

EntityHandle point_light_entity_by_id(const EntityRegistry& registry, const std::uint64_t stable_id) {
    const EntityHandle entity = find_entity_by_stable_id(registry, stable_id);
    return point_light_component(registry, entity) != nullptr && transform_component(registry, entity) != nullptr
        ? entity
        : EntityHandle{};
}

std::uint32_t entity_class_type(const EntityRegistry& registry, const EntityHandle entity) {
    if (!entity_alive(registry, entity)) {
        return 0;
    }
    if (point_light_component(registry, entity) != nullptr) {
        return kEntityClassPointLight;
    }
    if (player_spawn_component(registry, entity) != nullptr) {
        return kEntityClassPlayerSpawn;
    }
    return kEntityClassEntity;
}

std::uint64_t entity_class_ancestry(const EntityRegistry& registry, const EntityHandle entity) {
    const std::uint32_t class_id = entity_class_type(registry, entity);
    if (class_id == 0) {
        return 0;
    }
    return bit_for_id(kEntityClassEntity) | bit_for_id(class_id);
}

std::uint64_t entity_component_mask(const EntityRegistry& registry, const EntityHandle entity) {
    if (!entity_alive(registry, entity)) {
        return 0;
    }

    std::uint64_t mask = 0;
    if (transform_component(registry, entity) != nullptr) {
        mask |= bit_for_id(kComponentTransform);
    }
    if (player_spawn_component(registry, entity) != nullptr) {
        mask |= bit_for_id(kComponentPlayerSpawn);
    }
    if (point_light_component(registry, entity) != nullptr) {
        mask |= bit_for_id(kComponentPointLight);
    }
    if (name_component(registry, entity) != nullptr) {
        mask |= bit_for_id(kComponentName);
    }
    if (script_component(registry, entity) != nullptr) {
        mask |= bit_for_id(kComponentScript);
    }
    return mask;
}

std::uint32_t entity_class_type(const EntityRegistry& registry, const std::uint64_t stable_id) {
    return entity_class_type(registry, find_entity_by_stable_id(registry, stable_id));
}

std::uint64_t entity_class_ancestry(const EntityRegistry& registry, const std::uint64_t stable_id) {
    return entity_class_ancestry(registry, find_entity_by_stable_id(registry, stable_id));
}

std::uint64_t entity_component_mask(const EntityRegistry& registry, const std::uint64_t stable_id) {
    return entity_component_mask(registry, find_entity_by_stable_id(registry, stable_id));
}

bool entity_is_a(const EntityRegistry& registry, const std::uint64_t stable_id, const std::uint32_t class_id) {
    return (entity_class_ancestry(registry, stable_id) & bit_for_id(class_id)) != 0;
}

bool entity_has_component(const EntityRegistry& registry, const std::uint64_t stable_id, const std::uint32_t component_id) {
    return (entity_component_mask(registry, stable_id) & bit_for_id(component_id)) != 0;
}

PlayerSpawn player_spawn_from_entities(const EntityRegistry& registry) {
    const EntityHandle entity = player_spawn_entity(registry);
    const TransformComponent* transform = transform_component(registry, entity);
    if (transform == nullptr || player_spawn_component(registry, entity) == nullptr) {
        return {};
    }

    PlayerSpawn spawn;
    spawn.id = entity_stable_id(registry, entity);
    spawn.position = transform->position;
    spawn.yaw = transform->yaw;
    spawn.set = true;
    return spawn;
}

std::vector<PointLight> point_lights_from_entities(const EntityRegistry& registry) {
    std::vector<PointLight> lights;
    lights.reserve(registry.point_light_owners.size());
    for (const EntityHandle entity : registry.point_light_owners) {
        const TransformComponent* transform = transform_component(registry, entity);
        const PointLightComponent* light = point_light_component(registry, entity);
        if (transform == nullptr || light == nullptr) {
            continue;
        }
        lights.push_back(authored_point_light(entity_stable_id(registry, entity), *transform, *light));
    }
    return lights;
}

EntityRegistry entity_registry_from_authored_entities(PlayerSpawn player_spawn, const std::vector<PointLight>& point_lights) {
    EntityRegistry registry;
    if (player_spawn.set) {
        set_player_spawn_entity(registry, player_spawn);
    }
    for (const PointLight& light : point_lights) {
        add_point_light_entity(registry, light);
    }
    return registry;
}

EntityHandle set_player_spawn_entity(EntityRegistry& registry, PlayerSpawn player_spawn) {
    EntityHandle entity = player_spawn_entity(registry);
    if (!entity_alive(registry, entity)) {
        entity = create_entity(registry, player_spawn.id);
        add_player_spawn(registry, entity, PlayerSpawnComponent{true});
    } else if (player_spawn.id != 0 && entity_stable_id(registry, entity) != player_spawn.id) {
        registry.slots[entity.index].stable_id = player_spawn.id;
        rebuild_stable_lookup(registry);
    }
    add_transform(registry, entity, TransformComponent{player_spawn.position, player_spawn.yaw});
    add_name(registry, entity, NameComponent{"player_spawn"});
    return entity;
}

EntityHandle add_point_light_entity(EntityRegistry& registry, PointLight point_light) {
    EntityHandle entity = create_entity(registry, point_light.id);
    add_transform(registry, entity, TransformComponent{point_light.position, 0.0F});
    add_point_light(registry, entity, point_light_component_from_authored(point_light));
    add_name(registry, entity, NameComponent{"point_light"});
    return entity;
}

bool update_point_light_entity(EntityRegistry& registry, const std::uint64_t stable_id, PointLight point_light) {
    const EntityHandle entity = point_light_entity_by_id(registry, stable_id);
    if (!entity_alive(registry, entity)) {
        return false;
    }
    point_light.id = stable_id;
    add_transform(registry, entity, TransformComponent{point_light.position, 0.0F});
    add_point_light(registry, entity, point_light_component_from_authored(point_light));
    return true;
}

} // namespace undecedent
