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
    const int clamped = clamped_material_id(material_id);
    const MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped)];
    const MaterialSlot& fallback = default_material_library().slots[static_cast<std::size_t>(clamped)];
    return MaterialColor{
        clamp_unit(slot.base_color.r, fallback.base_color.r),
        clamp_unit(slot.base_color.g, fallback.base_color.g),
        clamp_unit(slot.base_color.b, fallback.base_color.b),
    };
}

MaterialProperties material_properties(const MaterialLibrary& library, const int material_id) {
    const int clamped = clamped_material_id(material_id);
    const MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped)];
    const MaterialSlot& fallback = default_material_library().slots[static_cast<std::size_t>(clamped)];
    return MaterialProperties{
        MaterialColor{
            clamp_unit(slot.base_color.r, fallback.base_color.r),
            clamp_unit(slot.base_color.g, fallback.base_color.g),
            clamp_unit(slot.base_color.b, fallback.base_color.b),
        },
        clamp_roughness(slot.roughness, fallback.roughness),
        clamp_unit(slot.metallic, fallback.metallic),
        clamp_unit(slot.specular, fallback.specular),
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
        for (MaterialTextureSource& source : slot.textures) {
            if (source.codec != MaterialTextureImageCodec::SdlSurfaceImage &&
                source.codec != MaterialTextureImageCodec::JpegXl) {
                source.codec = MaterialTextureImageCodec::SdlSurfaceImage;
            }
            if (source.storage_mode != MaterialTextureStorageMode::SourceBytes &&
                source.storage_mode != MaterialTextureStorageMode::JpegXlLossless &&
                source.storage_mode != MaterialTextureStorageMode::JpegXlLossy) {
                source.storage_mode = MaterialTextureStorageMode::SourceBytes;
            }
            if (source.jxl_quality < 1 || source.jxl_quality > 100) {
                source.jxl_quality = 80;
            }
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

int material_texture_channel_index(const MaterialTextureChannel channel) {
    const int index = static_cast<int>(channel);
    if (index < 0 || index >= kMaterialTextureChannelCount) {
        return 0;
    }
    return index;
}

const char* material_texture_channel_label(const MaterialTextureChannel channel) {
    switch (channel) {
    case MaterialTextureChannel::Normal: return "Normal";
    case MaterialTextureChannel::Smoothness: return "Smoothness";
    case MaterialTextureChannel::AmbientOcclusion: return "Ambient Occlusion";
    case MaterialTextureChannel::Metallic: return "Metallic";
    case MaterialTextureChannel::Albedo:
    default:
        return "Albedo";
    }
}

const char* material_texture_channel_short_label(const MaterialTextureChannel channel) {
    switch (channel) {
    case MaterialTextureChannel::Normal: return "NRM";
    case MaterialTextureChannel::Smoothness: return "SMO";
    case MaterialTextureChannel::AmbientOcclusion: return "AO";
    case MaterialTextureChannel::Metallic: return "MET";
    case MaterialTextureChannel::Albedo:
    default:
        return "ALB";
    }
}

MaterialTextureSource& material_texture_source(MaterialSlot& slot, const MaterialTextureChannel channel) {
    return slot.textures[static_cast<std::size_t>(material_texture_channel_index(channel))];
}

const MaterialTextureSource& material_texture_source(const MaterialSlot& slot, const MaterialTextureChannel channel) {
    return slot.textures[static_cast<std::size_t>(material_texture_channel_index(channel))];
}

bool material_texture_source_has_texture(const MaterialTextureSource& source) {
    return !source.path.empty() || !source.bytes.empty();
}

void set_material_texture_path(
    MaterialLibrary& library,
    const int material_id,
    const MaterialTextureChannel channel,
    std::filesystem::path path
) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    MaterialTextureSource& source = material_texture_source(slot, channel);
    source.path = path.lexically_normal().generic_string();
    source.name.clear();
    source.bytes.clear();
    source.codec = material_texture_codec_for_path(path);
    source.storage_mode = MaterialTextureStorageMode::SourceBytes;
    source.jxl_quality = 80;
}

void set_material_texture_path(MaterialLibrary& library, const int material_id, std::filesystem::path path) {
    set_material_texture_path(library, material_id, MaterialTextureChannel::Albedo, std::move(path));
}

void set_material_texture(
    MaterialLibrary& library,
    const int material_id,
    const MaterialTextureChannel channel,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    MaterialTextureSource& source = material_texture_source(slot, channel);
    source.path = path.lexically_normal().generic_string();
    source.name = std::move(name);
    source.bytes = std::move(bytes);
    source.codec = material_texture_codec_for_path(path);
}

void set_material_texture(
    MaterialLibrary& library,
    const int material_id,
    std::filesystem::path path,
    std::string name,
    std::vector<std::uint8_t> bytes
) {
    set_material_texture(
        library,
        material_id,
        MaterialTextureChannel::Albedo,
        std::move(path),
        std::move(name),
        std::move(bytes)
    );
}

void clear_material_texture_path(
    MaterialLibrary& library,
    const int material_id,
    const MaterialTextureChannel channel
) {
    MaterialSlot& slot = library.slots[static_cast<std::size_t>(clamped_material_id(material_id))];
    MaterialTextureSource& source = material_texture_source(slot, channel);
    source.path.clear();
    source.name.clear();
    source.bytes.clear();
    source.codec = MaterialTextureImageCodec::SdlSurfaceImage;
    source.storage_mode = MaterialTextureStorageMode::SourceBytes;
    source.jxl_quality = 80;
}

void clear_material_texture_path(MaterialLibrary& library, const int material_id) {
    clear_material_texture_path(library, material_id, MaterialTextureChannel::Albedo);
}

} // namespace undecedent
