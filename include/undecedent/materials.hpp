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
    // Perceptual roughness in [0.04, 1.0].
    float roughness = 0.72F;
    // Metallic blend in [0.0, 1.0].
    float metallic = 0.0F;
    // Dielectric F0 reflectance. The default 0.04 matches common non-metal surfaces.
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

enum class MaterialTextureChannel : std::uint32_t {
    Albedo = 0,
    Normal = 1,
    Smoothness = 2,
    AmbientOcclusion = 3,
    Metallic = 4,
};

inline constexpr int kMaterialTextureChannelCount = 5;

struct MaterialTextureSource {
    std::string path;
    std::string name;
    std::vector<std::uint8_t> bytes;
    MaterialTextureImageCodec codec = MaterialTextureImageCodec::SdlSurfaceImage;
    MaterialTextureStorageMode storage_mode = MaterialTextureStorageMode::SourceBytes;
    int jxl_quality = 80;
};

struct MaterialSlot {
    MaterialColor base_color;
    // Perceptual roughness in [0.04, 1.0].
    float roughness = 0.72F;
    // Metallic blend in [0.0, 1.0].
    float metallic = 0.0F;
    // Dielectric F0 reflectance. Metals use base_color through the GGX F0 mix.
    float specular = 0.04F;
    float uv_scale = 64.0F;
    std::array<MaterialTextureSource, kMaterialTextureChannelCount> textures{};
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
int material_texture_channel_index(MaterialTextureChannel channel);
const char* material_texture_channel_label(MaterialTextureChannel channel);
const char* material_texture_channel_short_label(MaterialTextureChannel channel);
MaterialTextureSource& material_texture_source(MaterialSlot& slot, MaterialTextureChannel channel);
const MaterialTextureSource& material_texture_source(const MaterialSlot& slot, MaterialTextureChannel channel);
bool material_texture_source_has_texture(const MaterialTextureSource& source);
void set_material_texture_path(
    MaterialLibrary& library,
    int material_id,
    MaterialTextureChannel channel,
    std::filesystem::path path
);
void set_material_texture_path(MaterialLibrary& library, int material_id, std::filesystem::path path);
void set_material_texture(
    MaterialLibrary& library,
    int material_id,
    MaterialTextureChannel channel,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
);
void set_material_texture(
    MaterialLibrary& library,
    int material_id,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
);
void clear_material_texture_path(MaterialLibrary& library, int material_id, MaterialTextureChannel channel);
void clear_material_texture_path(MaterialLibrary& library, int material_id);

} // namespace undecedent
