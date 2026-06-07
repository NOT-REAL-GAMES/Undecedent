#include "undecedent/material_texture.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace undecedent {
namespace {

constexpr int kMaterialTextureSize = 512;
constexpr int kMaterialTextureChannels = 4;

std::uint8_t to_byte(const float value) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)
    );
}

void fill_fallback_layer(std::uint8_t* pixels, const MaterialColor color) {
    const std::uint8_t r = to_byte(color.r);
    const std::uint8_t g = to_byte(color.g);
    const std::uint8_t b = to_byte(color.b);
    for (int y = 0; y < kMaterialTextureSize; ++y) {
        for (int x = 0; x < kMaterialTextureSize; ++x) {
            const int offset = ((y * kMaterialTextureSize) + x) * kMaterialTextureChannels;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = 255;
        }
    }
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

    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        std::cout << "Could not convert material texture '" << path << "': " << SDL_GetError() << '\n';
        return false;
    }

    SDL_Surface* source = rgba;
    SDL_Surface* scaled = nullptr;
    if (rgba->w != kMaterialTextureSize || rgba->h != kMaterialTextureSize) {
        scaled = SDL_ScaleSurface(rgba, kMaterialTextureSize, kMaterialTextureSize, SDL_SCALEMODE_LINEAR);
        if (scaled == nullptr) {
            std::cout << "Could not scale material texture '" << path << "': " << SDL_GetError() << '\n';
            SDL_DestroySurface(rgba);
            return false;
        }
        source = scaled;
    }

    if (!SDL_LockSurface(source)) {
        std::cout << "Could not lock material texture '" << path << "': " << SDL_GetError() << '\n';
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

bool material_libraries_match(const MaterialLibrary& a, const MaterialLibrary& b) {
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialSlot& left = a.slots[static_cast<std::size_t>(i)];
        const MaterialSlot& right = b.slots[static_cast<std::size_t>(i)];
        if (left.albedo_texture_path != right.albedo_texture_path ||
            left.base_color.r != right.base_color.r ||
            left.base_color.g != right.base_color.g ||
            left.base_color.b != right.base_color.b) {
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
        std::filesystem::path texture_path = slot.albedo_texture_path;
        if (!texture_path.empty() && texture_path.is_relative()) {
            texture_path = base / texture_path;
        }
        if (!load_surface_layer(texture_path.string(), layer)) {
            fill_fallback_layer(layer, slot.base_color);
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
