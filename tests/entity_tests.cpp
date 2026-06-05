#include "undecedent/entity.hpp"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    {
        undecedent::EntityRegistry registry;
        const undecedent::EntityHandle entity = undecedent::create_entity(registry, 10);
        expect(undecedent::entity_alive(registry, entity), "created entity should be alive");
        expect(undecedent::entity_stable_id(registry, entity) == 10, "stable id should be stored");
        expect(undecedent::find_entity_by_stable_id(registry, 10) == entity, "stable id lookup should resolve entity");

        undecedent::add_transform(registry, entity, undecedent::TransformComponent{undecedent::Vec3{1, 2, 3}, 0.5F});
        const undecedent::TransformComponent* transform = undecedent::transform_component(registry, entity);
        expect(transform != nullptr && transform->position.y == 2.0F, "transform component should be readable");
    }

    {
        undecedent::EntityRegistry registry;
        const undecedent::EntityHandle a = undecedent::create_entity(registry, 1);
        const undecedent::EntityHandle b = undecedent::create_entity(registry, 2);
        undecedent::add_point_light(registry, a, {});
        undecedent::add_point_light(registry, b, {});
        expect(registry.point_lights.size() == 2, "point light components should be dense");
        expect(undecedent::destroy_entity(registry, a), "destroy should succeed");
        expect(!undecedent::entity_alive(registry, a), "destroyed handle should be invalid");
        expect(registry.point_lights.size() == 1, "destroy should compact dense component arrays");
        expect(undecedent::point_light_component(registry, b) != nullptr, "remaining component should still resolve");
        const undecedent::EntityHandle reused = undecedent::create_entity(registry, 3);
        expect(reused.index == a.index, "registry should reuse freed slots");
        expect(reused.generation != a.generation, "reused slot should advance generation");
    }

    {
        undecedent::EntityRegistry registry;
        undecedent::PlayerSpawn spawn;
        spawn.id = 5;
        spawn.set = true;
        spawn.position = undecedent::Vec3{4, 48, 6};
        spawn.yaw = 1.25F;
        undecedent::set_player_spawn_entity(registry, spawn);
        const undecedent::PlayerSpawn restored = undecedent::player_spawn_from_entities(registry);
        expect(restored.set && restored.id == 5, "player spawn should round-trip through ECS");
        expect(restored.position.z == 6.0F && restored.yaw == 1.25F, "player spawn transform should round-trip");
    }

    {
        undecedent::EntityRegistry registry;
        undecedent::PointLight light;
        light.id = 9;
        light.position = undecedent::Vec3{1, 64, 2};
        light.color = undecedent::Vec3{0.25F, 0.5F, 0.75F};
        light.radius = 128.0F;
        light.intensity = 3.0F;
        light.shadow_bias = 4.0F;
        undecedent::add_point_light_entity(registry, light);
        const std::vector<undecedent::PointLight> lights = undecedent::point_lights_from_entities(registry);
        expect(lights.size() == 1 && lights.front().id == 9, "point light should round-trip through ECS");
        expect(lights.front().shadow_bias == 4.0F, "point light scalar properties should round-trip");
    }

    {
        undecedent::EntityRegistry registry;
        undecedent::PlayerSpawn spawn;
        spawn.id = 12;
        spawn.set = true;
        spawn.position = undecedent::Vec3{0, 48, 0};
        const undecedent::EntityHandle spawn_entity = undecedent::set_player_spawn_entity(registry, spawn);

        undecedent::PointLight light;
        light.id = 13;
        const undecedent::EntityHandle light_entity = undecedent::add_point_light_entity(registry, light);
        undecedent::add_script(registry, light_entity, undecedent::ScriptComponent{13, true});

        expect(
            undecedent::entity_class_type(registry, spawn_entity) == undecedent::kEntityClassPlayerSpawn,
            "player spawn should report its concrete class"
        );
        expect(
            undecedent::entity_class_type(registry, light_entity) == undecedent::kEntityClassPointLight,
            "point light should report its concrete class"
        );
        expect(undecedent::entity_is_a(registry, 13, undecedent::kEntityClassEntity), "point light should inherit entity");
        expect(undecedent::entity_is_a(registry, 13, undecedent::kEntityClassPointLight), "point light should inherit itself");
        expect(
            undecedent::entity_has_component(registry, 13, undecedent::kComponentTransform),
            "point light should expose transform component"
        );
        expect(
            undecedent::entity_has_component(registry, 13, undecedent::kComponentPointLight),
            "point light should expose point light component"
        );
        expect(
            undecedent::entity_has_component(registry, 13, undecedent::kComponentScript),
            "script component should appear in component mask"
        );
        expect(
            !undecedent::entity_has_component(registry, 13, undecedent::kComponentPlayerSpawn),
            "point light should not expose player spawn component"
        );
    }

    {
        undecedent::EntityRegistry registry;
        undecedent::create_entity(registry, 1);
        undecedent::create_entity(registry, 1);
        std::set<std::uint64_t> used{1};
        std::uint64_t next_id = 1;
        expect(undecedent::ensure_entity_stable_ids(registry, used, next_id), "conflicting ids should be reassigned");
        expect(undecedent::find_entity_by_stable_id(registry, 2).index != undecedent::kInvalidEntityIndex, "new id should be discoverable");
        expect(undecedent::find_entity_by_stable_id(registry, 3).index != undecedent::kInvalidEntityIndex, "second new id should be discoverable");
    }

    return EXIT_SUCCESS;
}
