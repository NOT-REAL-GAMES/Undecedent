#include "undecedent/geometry.hpp"
#include "undecedent/materials.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>
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
        assert(slot.specular == 0.04F);
        for (int channel_index = 0; channel_index < kMaterialTextureChannelCount; ++channel_index) {
            assert(material_texture_source(
                slot,
                static_cast<MaterialTextureChannel>(channel_index)
            ).path.empty());
        }
        assert(finite_unit(slot.base_color.r));
        assert(finite_unit(slot.base_color.g));
        assert(finite_unit(slot.base_color.b));
    }

    set_material_texture_path(library, 3, "textures/brick.png");
    assert(material_texture_source(material_slot(library, 3), MaterialTextureChannel::Albedo).path == "textures/brick.png");
    assert(material_texture_source(material_slot(library, 3), MaterialTextureChannel::Albedo).bytes.empty());

    set_material_texture(
        library,
        3,
        "textures/brick.png",
        "brick.png",
        std::vector<std::uint8_t>{1, 2, 3, 4}
    );
    const MaterialTextureSource albedo = material_texture_source(material_slot(library, 3), MaterialTextureChannel::Albedo);
    assert(albedo.path == "textures/brick.png");
    assert(albedo.name == "brick.png");
    assert(albedo.bytes == std::vector<std::uint8_t>({1, 2, 3, 4}));

    clear_material_texture_path(library, 3);
    const MaterialTextureSource cleared = material_texture_source(material_slot(library, 3), MaterialTextureChannel::Albedo);
    assert(cleared.path.empty());
    assert(cleared.name.empty());
    assert(cleared.bytes.empty());

    set_material_texture_path(library, 3, MaterialTextureChannel::Normal, "textures/brick_n.png");
    const MaterialSlot normal_slot = material_slot(library, 3);
    assert(material_texture_source(normal_slot, MaterialTextureChannel::Albedo).path.empty());
    assert(material_texture_source(normal_slot, MaterialTextureChannel::Normal).path == "textures/brick_n.png");
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

void test_material_library_normalization_preserves_valid_texture_fields() {
    MaterialLibrary library = default_material_library();
    MaterialSlot& edited = library.slots[2];
    edited.roughness = 0.5F;
    edited.metallic = 0.25F;
    edited.specular = 0.08F;
    edited.uv_scale = 32.0F;
    MaterialTextureSource& normal = material_texture_source(edited, MaterialTextureChannel::Normal);
    normal.path = "textures/wall_n.png";
    normal.name = "wall_n.png";
    normal.bytes = std::vector<std::uint8_t>{7, 8, 9};
    normal.codec = MaterialTextureImageCodec::JpegXl;
    normal.storage_mode = MaterialTextureStorageMode::JpegXlLossless;
    normal.jxl_quality = 77;

    const MaterialSlot slot = material_slot(normalized_material_library(std::move(library)), 2);
    assert(slot.roughness == 0.5F);
    assert(slot.metallic == 0.25F);
    assert(slot.specular == 0.08F);
    assert(slot.uv_scale == 32.0F);
    const MaterialTextureSource source = material_texture_source(slot, MaterialTextureChannel::Normal);
    assert(source.path == "textures/wall_n.png");
    assert(source.name == "wall_n.png");
    assert(source.bytes == std::vector<std::uint8_t>({7, 8, 9}));
    assert(source.codec == MaterialTextureImageCodec::JpegXl);
    assert(source.storage_mode == MaterialTextureStorageMode::JpegXlLossless);
    assert(source.jxl_quality == 77);
}

} // namespace

int main() {
    test_material_properties_are_valid();
    test_invalid_material_ids_clamp_to_default();
    test_material_library_defaults_and_texture_paths();
    test_material_library_normalization();
    test_material_library_normalization_preserves_valid_texture_fields();
    return 0;
}
