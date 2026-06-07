#pragma once

#include "undecedent/game_camera.hpp"
#include "undecedent/physics.hpp"
#include "undecedent/runtime_world.hpp"

namespace undecedent {

struct PlaytestPlayerState {
    float vertical_velocity = 0.0F;
    bool grounded = false;
    bool jump_was_down = false;
};

struct GameControlConfig {
    float move_speed = 180.0F;
    float look_speed = 1.9F;
    float player_radius = 8.0F;
    float player_height = 56.0F;
    float player_eye_height = 48.0F;
    float gravity = 900.0F;
    float jump_velocity = 320.0F;
    float terminal_fall_speed = 900.0F;
    float gravity_drag = 0.10F;
    float ground_probe = 1.0F;
    float mouse_look_sensitivity = 0.0025F;
};

PlayerPhysicsConfig player_physics_config(const GameControlConfig& config);
void update_game_camera_mouse_look(GameCamera& camera, float mouse_dx, float mouse_dy, const GameControlConfig& config);
void update_game_camera(GameCamera& camera, float dt, const GameControlConfig& config);
void update_playtest_camera(
    GameCamera& camera,
    const RuntimeWorld& world,
    PlaytestPlayerState& playtest_state,
    float dt,
    const GameControlConfig& config
);

} // namespace undecedent
