#include "undecedent/game_control.hpp"

#include "undecedent/triangulator.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace undecedent {
namespace {

void update_game_camera_look(GameCamera& camera, const float dt, const GameControlConfig& config) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    if (keys[SDL_SCANCODE_LEFT]) {
        camera.yaw += config.look_speed * dt;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        camera.yaw -= config.look_speed * dt;
    }
    if (keys[SDL_SCANCODE_UP]) {
        camera.pitch = std::min(camera.pitch + config.look_speed * dt, 1.45F);
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        camera.pitch = std::max(camera.pitch - config.look_speed * dt, -1.45F);
    }
}

} // namespace

PlayerPhysicsConfig player_physics_config(const GameControlConfig& config) {
    return PlayerPhysicsConfig{config.player_radius, config.player_height, config.player_eye_height};
}

void update_game_camera_mouse_look(
    GameCamera& camera,
    const float mouse_dx,
    const float mouse_dy,
    const GameControlConfig& config
) {
    camera.yaw -= mouse_dx * config.mouse_look_sensitivity;
    camera.pitch = std::clamp(
        camera.pitch - (mouse_dy * config.mouse_look_sensitivity),
        -1.45F,
        1.45F
    );
}

void update_game_camera(GameCamera& camera, const float dt, const GameControlConfig& config) {
    update_game_camera_look(camera, dt, config);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    const float forward_flat = std::cos(camera.pitch);
    const float forward_x = -std::sin(camera.yaw) * forward_flat;
    const float forward_y = std::sin(camera.pitch);
    const float forward_z = -std::cos(camera.yaw) * forward_flat;
    const float strafe_x = std::cos(camera.yaw);
    const float strafe_z = -std::sin(camera.yaw);
    const float step = config.move_speed * dt;

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
    const RuntimeWorld& world,
    PlaytestPlayerState& playtest_state,
    const float dt,
    const GameControlConfig& config
) {
    update_game_camera_look(camera, dt, config);
    const bool* keys = SDL_GetKeyboardState(nullptr);

    const float forward_x = -std::sin(camera.yaw);
    const float forward_z = -std::cos(camera.yaw);
    const float strafe_x = std::cos(camera.yaw);
    const float strafe_z = -std::sin(camera.yaw);
    const float step = config.move_speed * dt;

    Vec3 delta{};
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

    const Vec3 eye{camera.x, camera.y, camera.z};
    const PlayerPhysicsConfig physics_config = player_physics_config(config);
    const bool fits_now = player_fits_at(world, eye, physics_config);
    const bool grounded_now = fits_now &&
        !player_fits_at(world, Vec3{eye.x, eye.y - config.ground_probe, eye.z}, physics_config);
    const bool jump_down = keys[SDL_SCANCODE_SPACE];
    const bool jump_pressed = jump_down && !playtest_state.jump_was_down;
    playtest_state.jump_was_down = jump_down;
    playtest_state.grounded = grounded_now;

    if (jump_pressed && playtest_state.grounded) {
        playtest_state.vertical_velocity = config.jump_velocity;
        playtest_state.grounded = false;
    } else if (playtest_state.grounded && playtest_state.vertical_velocity < 0.0F) {
        playtest_state.vertical_velocity = 0.0F;
    }

    playtest_state.vertical_velocity -= config.gravity * dt;
    playtest_state.vertical_velocity *= std::exp(-config.gravity_drag * dt);
    playtest_state.vertical_velocity = std::max(playtest_state.vertical_velocity, -config.terminal_fall_speed);
    delta.y = playtest_state.vertical_velocity * dt;

    PlayerPhysicsState state{
        Vec3{camera.x, camera.y, camera.z},
        -1,
    };
    const float old_y = state.position.y;
    state = move_player(world, state, delta, physics_config);
    camera.x = state.position.x;
    camera.y = state.position.y;
    camera.z = state.position.z;

    const bool blocked_vertically = std::abs(camera.y - old_y) <= kGeometryEpsilon &&
        std::abs(delta.y) > kGeometryEpsilon;
    if (blocked_vertically) {
        if (delta.y < 0.0F) {
            playtest_state.grounded = true;
        }
        playtest_state.vertical_velocity = 0.0F;
    }
}

} // namespace undecedent
