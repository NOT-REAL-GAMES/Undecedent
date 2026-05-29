#pragma once

#include "undecedent/geometry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace undecedent {

struct SaveMapResult {
    bool ok = false;
    std::string message;
};

struct LoadMapResult {
    bool ok = false;
    std::string message;
    std::vector<SectorPlane> sectors;
    PlayerSpawn player_spawn;
    std::vector<PointLight> point_lights;
};

SaveMapResult save_map_file(const std::vector<SectorPlane>& sectors, const std::filesystem::path& path);
SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    PlayerSpawn player_spawn,
    const std::filesystem::path& path
);
SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const std::filesystem::path& path
);
LoadMapResult load_map_file(const std::filesystem::path& path);

} // namespace undecedent
