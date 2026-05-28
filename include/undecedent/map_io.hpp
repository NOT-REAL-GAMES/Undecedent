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
};

SaveMapResult save_map_file(const std::vector<SectorPlane>& sectors, const std::filesystem::path& path);
LoadMapResult load_map_file(const std::filesystem::path& path);

} // namespace undecedent
