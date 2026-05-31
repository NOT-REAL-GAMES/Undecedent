#include "undecedent/editor.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

undecedent::PointLight point_light(const std::uint64_t id, const float x) {
    undecedent::PointLight light;
    light.id = id;
    light.position = undecedent::Vec3{x, 64.0F, 0.0F};
    light.radius = 64.0F;
    light.intensity = 1.0F;
    return light;
}

} // namespace

int main() {
    {
        const undecedent::PointLight light = undecedent::default_point_light_at(undecedent::Vec3{1.0F, 2.0F, 3.0F});
        expect(light.shadow_bias == 2.0F, "default point light shadow bias should be 2 world units");
    }

    {
        undecedent::EditorWorld world;
        world.point_lights.push_back(point_light(10, 0.0F));
        world.point_lights.push_back(point_light(20, 32.0F));
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::PointLight, 20});
        std::reverse(world.point_lights.begin(), world.point_lights.end());
        const undecedent::PointLight* selected = undecedent::selected_point_light(world);
        expect(selected != nullptr, "selected point light should resolve after vector reorder");
        expect(selected->id == 20, "selected point light should resolve by stable id");
        expect(undecedent::delete_selected_entity(world), "delete selected point light should succeed");
        expect(world.selected_entity.kind == undecedent::SelectedEntityKind::None, "deleting selected light clears selection");
        expect(world.point_lights.size() == 1 && world.point_lights.front().id == 10, "deleting selected light preserves other lights");
    }

    {
        undecedent::EditorWorld world;
        world.point_lights.push_back(point_light(10, 0.0F));
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::PointLight, 10});
        expect(undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::Radius, -1000.0F), "radius edit should apply");
        expect(world.point_lights.front().radius == 1.0F, "point light radius clamps above zero");
        expect(undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::Intensity, -1000.0F), "intensity edit should apply");
        expect(world.point_lights.front().intensity == 0.0F, "point light intensity clamps non-negative");
        expect(undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::ShadowBias, -1000.0F), "shadow bias edit should apply");
        expect(world.point_lights.front().shadow_bias == 0.0F, "point light shadow bias clamps non-negative");
        expect(undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::ColorR, -1000.0F), "color edit should apply");
        expect(world.point_lights.front().color.x == 0.0F, "point light color clamps non-negative");
    }

    {
        undecedent::EditorWorld world;
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::SunLight, 0});
        world.world_lighting.sun_direction = undecedent::Vec3{0.0F, 0.0F, 0.0F};
        expect(
            undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::SunDirectionX, 0.0F),
            "sun direction edit should apply"
        );
        const float length_sq =
            (world.world_lighting.sun_direction.x * world.world_lighting.sun_direction.x) +
            (world.world_lighting.sun_direction.y * world.world_lighting.sun_direction.y) +
            (world.world_lighting.sun_direction.z * world.world_lighting.sun_direction.z);
        expect(length_sq > 0.99F && length_sq < 1.01F, "sun direction should normalize with fallback");
    }

    {
        undecedent::EditorWorld world;
        world.player_spawn.set = true;
        world.player_spawn.id = 42;
        world.player_spawn.position = undecedent::Vec3{1.0F, 2.0F, 3.0F};
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::PlayerSpawn, 0});
        expect(
            undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::PositionX, 8.0F),
            "spawn position edit should apply"
        );
        expect(world.player_spawn.position.x == 9.0F, "spawn position should change");
        expect(undecedent::undo_editor_action(world), "undo should restore spawn edit");
        expect(world.player_spawn.position.x == 1.0F, "undo restores spawn position");
        expect(world.selected_entity.kind == undecedent::SelectedEntityKind::PlayerSpawn, "undo restores selected entity");
        expect(undecedent::redo_editor_action(world), "redo should reapply spawn edit");
        expect(world.player_spawn.position.x == 9.0F, "redo reapplies spawn position");
    }

    {
        undecedent::EditorWorld world;
        world.point_lights.push_back(point_light(10, 0.0F));
        world.point_lights.front().shadow_bias = 3.0F;
        undecedent::select_entity(world, undecedent::SelectedEntityRef{undecedent::SelectedEntityKind::PointLight, 10});
        expect(
            undecedent::adjust_selected_entity_property(world, undecedent::EntityProperty::ShadowBias, 0.5F),
            "light shadow bias edit should apply"
        );
        expect(world.point_lights.front().shadow_bias == 3.5F, "shadow bias should change");
        expect(undecedent::undo_editor_action(world), "undo should restore light shadow bias");
        expect(world.point_lights.front().shadow_bias == 3.0F, "undo restores light shadow bias");
        expect(world.selected_entity.kind == undecedent::SelectedEntityKind::PointLight, "undo restores selected point light");
        expect(undecedent::redo_editor_action(world), "redo should reapply light shadow bias");
        expect(world.point_lights.front().shadow_bias == 3.5F, "redo reapplies light shadow bias");
    }

    return EXIT_SUCCESS;
}
