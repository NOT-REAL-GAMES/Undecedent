#include "undecedent/geometry.hpp"
#include "undecedent/materials.hpp"

#include <cassert>
#include <cmath>

using namespace undecedent;

namespace {

bool finite_unit(const float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

void test_material_properties_are_valid() {
    for (int material_id = 0; material_id < kMaterialCount; ++material_id) {
        const MaterialProperties material = material_properties(material_id);
        assert(finite_unit(material.base_color.r));
        assert(finite_unit(material.base_color.g));
        assert(finite_unit(material.base_color.b));
        assert(std::isfinite(material.roughness));
        assert(material.roughness >= 0.04F);
        assert(material.roughness <= 1.0F);
        assert(finite_unit(material.metallic));
        assert(finite_unit(material.specular));
    }
}

void test_invalid_material_ids_clamp_to_default() {
    const MaterialProperties default_material = material_properties(0);
    const MaterialProperties low_material = material_properties(-1);
    const MaterialProperties high_material = material_properties(kMaterialCount);
    assert(low_material.base_color.r == default_material.base_color.r);
    assert(low_material.base_color.g == default_material.base_color.g);
    assert(low_material.base_color.b == default_material.base_color.b);
    assert(low_material.roughness == default_material.roughness);
    assert(low_material.metallic == default_material.metallic);
    assert(low_material.specular == default_material.specular);
    assert(high_material.base_color.r == default_material.base_color.r);
    assert(high_material.base_color.g == default_material.base_color.g);
    assert(high_material.base_color.b == default_material.base_color.b);
    assert(high_material.roughness == default_material.roughness);
    assert(high_material.metallic == default_material.metallic);
    assert(high_material.specular == default_material.specular);
}

} // namespace

int main() {
    test_material_properties_are_valid();
    test_invalid_material_ids_clamp_to_default();
    return 0;
}
