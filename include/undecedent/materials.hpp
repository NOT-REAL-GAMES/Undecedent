#pragma once

#include "undecedent/geometry.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

enum class MaterialTextureImageCodec : std::uint32_t {
    SdlSurfaceImage = 0,
    JpegXl = 1,
};

enum class MaterialTextureStorageMode : std::uint32_t {
    SourceBytes = 0,
    JpegXlLossless = 1,
    JpegXlLossy = 2,
};

struct MaterialSlot {
    MaterialColor base_color;
    float roughness = 0.72F;
    float metallic = 0.0F;
    float specular = 0.04F;
    float uv_scale = 64.0F;
    std::string albedo_texture_path;
    std::string albedo_texture_name;
    std::vector<std::uint8_t> albedo_texture_bytes;
    MaterialTextureImageCodec albedo_texture_codec = MaterialTextureImageCodec::SdlSurfaceImage;
    MaterialTextureStorageMode texture_storage_mode = MaterialTextureStorageMode::SourceBytes;
    int jxl_quality = 80;
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
MaterialTextureImageCodec material_texture_codec_for_path(const std::filesystem::path& path);
void set_material_texture_path(MaterialLibrary& library, int material_id, std::filesystem::path path);
void set_material_texture(
    MaterialLibrary& library,
    int material_id,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
);
void clear_material_texture_path(MaterialLibrary& library, int material_id);

} // namespace undecedent
