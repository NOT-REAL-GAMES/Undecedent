#include "undecedent/geometry.hpp"
#include "undecedent/materials.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

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

void test_material_library_defaults_and_texture_paths() {
    MaterialLibrary library = default_material_library();
    for (int material_id = 0; material_id < kMaterialCount; ++material_id) {
        const MaterialSlot slot = material_slot(library, material_id);
        assert(slot.uv_scale == 64.0F);
        assert(slot.albedo_texture_path.empty());
        assert(finite_unit(slot.base_color.r));
        assert(finite_unit(slot.base_color.g));
        assert(finite_unit(slot.base_color.b));
    }

    set_material_texture_path(library, 3, "textures/brick.png");
    assert(material_slot(library, 3).albedo_texture_path == "textures/brick.png");
    assert(material_slot(library, 3).albedo_texture_bytes.empty());

    set_material_texture(
        library,
        3,
        "textures/brick.png",
        "brick.png",
        std::vector<std::uint8_t>{1, 2, 3, 4}
    );
    assert(material_slot(library, 3).albedo_texture_path == "textures/brick.png");
    assert(material_slot(library, 3).albedo_texture_name == "brick.png");
    assert(material_slot(library, 3).albedo_texture_bytes == std::vector<std::uint8_t>({1, 2, 3, 4}));

    clear_material_texture_path(library, 3);
    assert(material_slot(library, 3).albedo_texture_path.empty());
    assert(material_slot(library, 3).albedo_texture_name.empty());
    assert(material_slot(library, 3).albedo_texture_bytes.empty());
}

void test_material_library_normalization() {
    MaterialLibrary library = default_material_library();
    library.slots[1].roughness = -2.0F;
    library.slots[1].metallic = 9.0F;
    library.slots[1].specular = -1.0F;
    library.slots[1].uv_scale = 0.0F;
    const MaterialLibrary normalized = normalized_material_library(std::move(library));
    const MaterialSlot fallback = material_slot(default_material_library(), 1);
    const MaterialSlot slot = material_slot(normalized, 1);
    assert(slot.roughness == fallback.roughness);
    assert(slot.metallic == fallback.metallic);
    assert(slot.specular == fallback.specular);
    assert(slot.uv_scale == fallback.uv_scale);
}

} // namespace

int main() {
    test_material_properties_are_valid();
    test_invalid_material_ids_clamp_to_default();
    test_material_library_defaults_and_texture_paths();
    test_material_library_normalization();
    return 0;
}
