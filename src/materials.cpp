#include "undecedent/materials.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace undecedent {
namespace {

constexpr float kDefaultUvScale = 64.0F;

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

float clamp_unit(const float value, const float fallback) {
    if (value < 0.0F || value > 1.0F) {
        return fallback;
    }
    return value;
}

float clamp_roughness(const float value, const float fallback) {
    if (value < 0.04F || value > 1.0F) {
        return fallback;
    }
    return value;
}

MaterialLibrary build_default_material_library() {
    MaterialLibrary library;
    for (int i = 0; i < kMaterialCount; ++i) {
        MaterialSlot& slot = library.slots[static_cast<std::size_t>(i)];
        slot.base_color = kMaterialPalette[static_cast<std::size_t>(i)];
        slot.roughness = kMaterialRoughness[static_cast<std::size_t>(i)];
        slot.metallic = 0.0F;
        slot.specular = 0.04F;
        slot.uv_scale = kDefaultUvScale;
    }
    return library;
}

} // namespace

MaterialColor material_color(const int material_id) {
    return material_color(default_material_library(), material_id);
}

MaterialProperties material_properties(const int material_id) {
    return material_properties(default_material_library(), material_id);
}

const MaterialLibrary& default_material_library() {
    static const MaterialLibrary library = build_default_material_library();
    return library;
}

MaterialColor material_color(const MaterialLibrary& library, const int material_id) {
    return material_slot(library, material_id).base_color;
}

MaterialProperties material_properties(const MaterialLibrary& library, const int material_id) {
    const MaterialSlot slot = material_slot(library, material_id);
    return MaterialProperties{
        slot.base_color,
        slot.roughness,
        slot.metallic,
        slot.specular,
    };
}

MaterialSlot material_slot(const MaterialLibrary& library, const int material_id) {
    const int clamped = clamped_material_id(material_id);
    return normalized_material_library(library).slots[static_cast<std::size_t>(clamped)];
}

MaterialLibrary normalized_material_library(MaterialLibrary library) {
    const MaterialLibrary& defaults = default_material_library();
    for (int i = 0; i < kMaterialCount; ++i) {
        MaterialSlot& slot = library.slots[static_cast<std::size_t>(i)];
        const MaterialSlot& fallback = defaults.slots[static_cast<std::size_t>(i)];
        slot.base_color.r = clamp_unit(slot.base_color.r, fallback.base_color.r);
        slot.base_color.g = clamp_unit(slot.base_color.g, fallback.base_color.g);
        slot.base_color.b = clamp_unit(slot.base_color.b, fallback.base_color.b);
        slot.roughness = clamp_roughness(slot.roughness, fallback.roughness);
        slot.metallic = clamp_unit(slot.metallic, fallback.metallic);
        slot.specular = clamp_unit(slot.specular, fallback.specular);
        if (slot.uv_scale <= 0.0F) {
            slot.uv_scale = fallback.uv_scale;
        }
        if (slot.albedo_texture_codec != MaterialTextureImageCodec::SdlSurfaceImage &&
            slot.albedo_texture_codec != MaterialTextureImageCodec::JpegXl) {
            slot.albedo_texture_codec = MaterialTextureImageCodec::SdlSurfaceImage;
        }
        if (slot.texture_storage_mode != MaterialTextureStorageMode::SourceBytes &&
            slot.texture_storage_mode != MaterialTextureStorageMode::JpegXlLossless &&
            slot.texture_storage_mode != MaterialTextureStorageMode::JpegXlLossy) {
            slot.texture_storage_mode = MaterialTextureStorageMode::SourceBytes;
        }
        if (slot.jxl_quality < 1 || slot.jxl_quality > 100) {
            slot.jxl_quality = 80;
        }
    }
    return library;
}

MaterialTextureImageCodec material_texture_codec_for_path(const std::filesystem::path& path) {
    std::string extension = path.extension().generic_string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".jxl"
        ? MaterialTextureImageCodec::JpegXl
        : MaterialTextureImageCodec::SdlSurfaceImage;
}

void set_material_texture_path(MaterialLibrary& library, const int material_id, std::filesystem::path path) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    slot.albedo_texture_path = path.lexically_normal().generic_string();
    slot.albedo_texture_name.clear();
    slot.albedo_texture_bytes.clear();
    slot.albedo_texture_codec = material_texture_codec_for_path(path);
    slot.texture_storage_mode = MaterialTextureStorageMode::SourceBytes;
    slot.jxl_quality = 80;
}

void set_material_texture(
    MaterialLibrary& library,
    const int material_id,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    slot.albedo_texture_path = path.lexically_normal().generic_string();
    slot.albedo_texture_name = std::move(name);
    slot.albedo_texture_bytes = std::move(bytes);
    slot.albedo_texture_codec = material_texture_codec_for_path(path);
}

void clear_material_texture_path(MaterialLibrary& library, const int material_id) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    slot.albedo_texture_path.clear();
    slot.albedo_texture_name.clear();
    slot.albedo_texture_bytes.clear();
    slot.albedo_texture_codec = MaterialTextureImageCodec::SdlSurfaceImage;
    slot.texture_storage_mode = MaterialTextureStorageMode::SourceBytes;
    slot.jxl_quality = 80;
}

} // namespace undecedent
