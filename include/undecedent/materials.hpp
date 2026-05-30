#pragma once

namespace undecedent {

struct MaterialColor {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

MaterialColor material_color(int material_id);

} // namespace undecedent
