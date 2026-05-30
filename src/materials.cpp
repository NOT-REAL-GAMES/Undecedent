#include "undecedent/materials.hpp"

#include "undecedent/geometry.hpp"

#include <array>
#include <cstddef>

namespace undecedent {
namespace {

const std::array<MaterialColor, kMaterialCount> kMaterialPalette{{
    {0.24F, 0.58F, 0.52F},
    {0.78F, 0.36F, 0.32F},
    {0.35F, 0.58F, 0.90F},
    {0.88F, 0.72F, 0.30F},
    {0.58F, 0.42F, 0.78F},
    {0.36F, 0.72F, 0.40F},
    {0.82F, 0.82F, 0.78F},
    {0.16F, 0.18F, 0.22F},
}};

const std::array<float, kMaterialCount> kMaterialRoughness{{
    0.72F,
    0.68F,
    0.56F,
    0.48F,
    0.64F,
    0.74F,
    0.36F,
    0.86F,
}};

} // namespace

MaterialColor material_color(const int material_id) {
    return kMaterialPalette[static_cast<std::size_t>(clamped_material_id(material_id))];
}

MaterialProperties material_properties(const int material_id) {
    const int clamped = clamped_material_id(material_id);
    return MaterialProperties{
        kMaterialPalette[static_cast<std::size_t>(clamped)],
        kMaterialRoughness[static_cast<std::size_t>(clamped)],
        0.0F,
        0.04F,
    };
}

} // namespace undecedent
