#pragma once

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

MaterialColor material_color(int material_id);
MaterialProperties material_properties(int material_id);

} // namespace undecedent
