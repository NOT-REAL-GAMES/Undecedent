#pragma once

#include "undecedent/game_camera.hpp"
#include "undecedent/runtime_world.hpp"

#include <vector>

namespace undecedent {

struct GameRenderConfig;

std::vector<int> visible_sectors_from_camera(
    const RuntimeWorld& world,
    const GameCamera& camera,
    const GameRenderConfig& config,
    int width,
    int height
);

} // namespace undecedent
