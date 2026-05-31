#pragma once

#include "undecedent/geometry.hpp"

#include <cstdint>
#include <filesystem>
#include <set>
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
    WorldLighting world_lighting;
};

struct MapDirtyState {
    std::set<std::uint64_t> sector_ids;
    bool entities = false;
    bool metadata = false;
    bool materials = false;
    bool topology = false;
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
SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    WorldLighting world_lighting,
    const std::filesystem::path& path
);
SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
);
SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    WorldLighting world_lighting,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
);
LoadMapResult load_map_file(const std::filesystem::path& path);

} // namespace undecedent
