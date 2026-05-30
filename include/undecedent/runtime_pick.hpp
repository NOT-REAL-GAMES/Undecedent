#pragma once

#include "undecedent/editor.hpp"
#include "undecedent/game_camera.hpp"
#include "undecedent/runtime_render.hpp"
#include "undecedent/runtime_world.hpp"

namespace undecedent {

Vec3 camera_forward(const GameCamera& camera);
Vec3 camera_ray_direction(const GameCamera& camera, int width, int height, float screen_x, float screen_y, float fov_y_degrees);
SurfacePick pick_runtime_surface(
    const RuntimeWorld& world,
    const GameCamera& camera,
    int width,
    int height,
    float screen_x,
    float screen_y,
    const GameRenderConfig& config
);

} // namespace undecedent
