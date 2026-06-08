#include "undecedent/material_texture.hpp"

#include "undecedent/texture_compression.hpp"
#include "undecedent/texture_image_codec.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace undecedent {
namespace {

constexpr int kMaterialTextureSize = 512;
constexpr int kMaterialTextureChannels = 4;

void fill_fallback_layer(std::uint8_t* pixels) {
    for (int y = 0; y < kMaterialTextureSize; ++y) {
        for (int x = 0; x < kMaterialTextureSize; ++x) {
            const int offset = ((y * kMaterialTextureSize) + x) * kMaterialTextureChannels;
            pixels[offset + 0] = 255;
            pixels[offset + 1] = 255;
            pixels[offset + 2] = 255;
            pixels[offset + 3] = 255;
        }
    }
}

bool copy_surface_layer(SDL_Surface* loaded, const std::string& label, std::uint8_t* pixels) {
    if (loaded == nullptr) {
        return false;
    }

    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        std::cout << "Could not convert material texture '" << label << "': " << SDL_GetError() << '\n';
        return false;
    }

    SDL_Surface* source = rgba;
    SDL_Surface* scaled = nullptr;
    if (rgba->w != kMaterialTextureSize || rgba->h != kMaterialTextureSize) {
        scaled = SDL_ScaleSurface(rgba, kMaterialTextureSize, kMaterialTextureSize, SDL_SCALEMODE_LINEAR);
        if (scaled == nullptr) {
            std::cout << "Could not scale material texture '" << label << "': " << SDL_GetError() << '\n';
            SDL_DestroySurface(rgba);
            return false;
        }
        source = scaled;
    }

    if (!SDL_LockSurface(source)) {
        std::cout << "Could not lock material texture '" << label << "': " << SDL_GetError() << '\n';
        SDL_DestroySurface(rgba);
        if (scaled != nullptr) {
            SDL_DestroySurface(scaled);
        }
        return false;
    }

    const auto* source_pixels = static_cast<const std::uint8_t*>(source->pixels);
    for (int y = 0; y < kMaterialTextureSize; ++y) {
        const auto* row = source_pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(source->pitch));
        auto* destination = pixels + (static_cast<std::size_t>(y) * kMaterialTextureSize * kMaterialTextureChannels);
        std::copy_n(row, kMaterialTextureSize * kMaterialTextureChannels, destination);
    }

    SDL_UnlockSurface(source);
    SDL_DestroySurface(rgba);
    if (scaled != nullptr) {
        SDL_DestroySurface(scaled);
    }
    return true;
}

bool load_surface_layer(const std::string& path, std::uint8_t* pixels) {
    if (path.empty()) {
        return false;
    }

    SDL_Surface* loaded = SDL_LoadSurface(path.c_str());
    if (loaded == nullptr) {
        std::cout << "Could not load material texture '" << path << "': " << SDL_GetError() << '\n';
        return false;
    }
    return copy_surface_layer(loaded, path, pixels);
}

bool load_decoded_layer(
    const MaterialTextureImageCodec codec,
    const std::vector<std::uint8_t>& bytes,
    const std::string& label,
    std::uint8_t* pixels
) {
    DecodedTextureImage image;
    std::string message;
    if (!decode_texture_image_bytes(codec, bytes, label, image, message)) {
        std::cout << message << '\n';
        return false;
    }
    if (!copy_decoded_texture_to_layer(image, label, kMaterialTextureSize, pixels, message)) {
        std::cout << message << '\n';
        return false;
    }
    return true;
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !bytes.empty();
}

bool load_embedded_texture_layer(const MaterialSlot& slot, std::uint8_t* pixels) {
    if (slot.albedo_texture_bytes.empty()) {
        return false;
    }

    return load_decoded_layer(
        slot.albedo_texture_codec,
        slot.albedo_texture_bytes,
        slot.albedo_texture_name,
        pixels
    );
}

bool material_libraries_match(const MaterialLibrary& a, const MaterialLibrary& b) {
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialSlot& left = a.slots[static_cast<std::size_t>(i)];
        const MaterialSlot& right = b.slots[static_cast<std::size_t>(i)];
        if (left.albedo_texture_path != right.albedo_texture_path ||
            left.albedo_texture_name != right.albedo_texture_name ||
            left.albedo_texture_codec != right.albedo_texture_codec ||
            left.texture_storage_mode != right.texture_storage_mode ||
            left.jxl_quality != right.jxl_quality ||
            left.albedo_texture_bytes.size() != right.albedo_texture_bytes.size() ||
            crc32_bytes(left.albedo_texture_bytes) != crc32_bytes(right.albedo_texture_bytes)) {
            return false;
        }
    }
    return true;
}

} // namespace

void mark_material_textures_dirty(MaterialTextureArray& textures) {
    textures.dirty = true;
}

bool ensure_material_texture_array(MaterialTextureArray& textures, const MaterialLibrary& library) {
    if (glGenTextures == nullptr || glBindTexture == nullptr || glTexImage3D == nullptr) {
        return false;
    }

    const MaterialLibrary normalized = normalized_material_library(library);
    if (textures.texture != 0 && !textures.dirty && material_libraries_match(textures.uploaded_library, normalized)) {
        return true;
    }

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kMaterialTextureSize) *
        static_cast<std::size_t>(kMaterialTextureSize) *
        static_cast<std::size_t>(kMaterialTextureChannels) *
        static_cast<std::size_t>(kMaterialCount)
    );

    const std::filesystem::path base = std::filesystem::current_path();
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialSlot& slot = normalized.slots[static_cast<std::size_t>(i)];
        auto* layer = pixels.data() +
            (static_cast<std::size_t>(i) * kMaterialTextureSize * kMaterialTextureSize * kMaterialTextureChannels);
        if (load_embedded_texture_layer(slot, layer)) {
            continue;
        }

        if (slot.albedo_texture_path.empty()) {
            fill_fallback_layer(layer);
            continue;
        }

        std::filesystem::path texture_path = slot.albedo_texture_path;
        if (texture_path.is_relative()) {
            texture_path = base / texture_path;
        }
        const MaterialTextureImageCodec path_codec = material_texture_codec_for_path(texture_path);
        if (path_codec == MaterialTextureImageCodec::JpegXl) {
            std::vector<std::uint8_t> bytes;
            if (!read_file_bytes(texture_path, bytes) ||
                !load_decoded_layer(MaterialTextureImageCodec::JpegXl, bytes, texture_path.string(), layer)) {
                fill_fallback_layer(layer);
            }
        } else if (!load_surface_layer(texture_path.string(), layer)) {
            fill_fallback_layer(layer);
        }
    }

    if (textures.texture == 0) {
        glGenTextures(1, &textures.texture);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures.texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_RGBA8,
        kMaterialTextureSize,
        kMaterialTextureSize,
        kMaterialCount,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    textures.uploaded_library = normalized;
    textures.dirty = false;
    return true;
}

void destroy_material_texture_array(MaterialTextureArray& textures) {
    if (glDeleteTextures != nullptr && textures.texture != 0) {
        glDeleteTextures(1, &textures.texture);
    }
    textures = {};
}

} // namespace undecedent
