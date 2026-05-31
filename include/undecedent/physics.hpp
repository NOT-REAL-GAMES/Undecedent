#pragma once

#include "undecedent/runtime_world.hpp"

namespace undecedent {

struct PlayerPhysicsConfig {
    float radius = 8.0F;
    float height = 56.0F;
    float eye_height = 48.0F;
    float max_step_height = 18.0F;
    float foot_contact_radius = 2.0F;
};

struct PlayerPhysicsState {
    Vec3 position;
    int sector_id = -1;
};

bool player_fits_at(const RuntimeWorld& world, Vec3 eye_position, PlayerPhysicsConfig config = {});
PlayerPhysicsState move_player(
    const RuntimeWorld& world,
    PlayerPhysicsState state,
    Vec3 delta,
    PlayerPhysicsConfig config = {}
);

} // namespace undecedent
