#pragma once

#include "undecedent/deferred_renderer.hpp"
#include "undecedent/game_camera.hpp"
#include "undecedent/geometry.hpp"
#include "undecedent/runtime_render_cache.hpp"
#include "undecedent/runtime_world.hpp"

#include <vector>

namespace undecedent {

struct GameRenderConfig {
    float near_plane = 1.0F;
    float far_plane = 20000.0F;
    float fov_y_degrees = 70.0F;
    float player_eye_height = 48.0F;
    float player_height = 56.0F;
    float player_radius = 8.0F;
};

void set_game_projection(int width, int height, const GameCamera& camera, const GameRenderConfig& config);

void draw_player_spawn_3d(
    const PlayerSpawn& spawn,
    int width,
    int height,
    const GameCamera& camera,
    const GameRenderConfig& config
);

void draw_point_lights_3d(
    const std::vector<PointLight>& point_lights,
    int width,
    int height,
    const GameCamera& camera,
    const GameRenderConfig& config
);

int draw_runtime_world(
    const RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    int width,
    int height,
    const GameCamera& camera,
    bool draw_wire_overlay,
    bool filter_connected_visibility,
    const GameRenderConfig& config
);

int draw_deferred_runtime_world(
    DeferredRenderer& renderer,
    const RuntimeWorld& world,
    const RuntimeRenderCache& render_cache,
    const std::vector<PointLight>& point_lights,
    const WorldLighting& world_lighting,
    int width,
    int height,
    const GameCamera& camera,
    bool draw_wire_overlay,
    const GameRenderConfig& config
);

} // namespace undecedent
