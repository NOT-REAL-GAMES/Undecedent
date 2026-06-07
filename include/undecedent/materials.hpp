#pragma once

#include "undecedent/geometry.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace undecedent {

struct MaterialColor {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

struct MaterialProperties {
    MaterialColor base_color;
    float roughness = 0.72F;
    float metallic = 0.0F;
    float specular = 0.04F;
};

struct MaterialSlot {
    MaterialColor base_color;
    float roughness = 0.72F;
    float metallic = 0.0F;
    float specular = 0.04F;
    float uv_scale = 64.0F;
    std::string albedo_texture_path;
};

struct MaterialLibrary {
    std::array<MaterialSlot, kMaterialCount> slots{};
};

MaterialColor material_color(int material_id);
MaterialProperties material_properties(int material_id);
const MaterialLibrary& default_material_library();
MaterialColor material_color(const MaterialLibrary& library, int material_id);
MaterialProperties material_properties(const MaterialLibrary& library, int material_id);
MaterialSlot material_slot(const MaterialLibrary& library, int material_id);
MaterialLibrary normalized_material_library(MaterialLibrary library);
void set_material_texture_path(MaterialLibrary& library, int material_id, std::filesystem::path path);
void clear_material_texture_path(MaterialLibrary& library, int material_id);

} // namespace undecedent
