#include "undecedent/material_texture.hpp"

#include "undecedent/texture_image_codec.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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

std::uint8_t byte_from_unit(const float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

void fill_solid_layer(std::uint8_t* pixels, const std::uint8_t r, const std::uint8_t g, const std::uint8_t b) {
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

void fill_fallback_layer(const MaterialSlot& slot, const MaterialTextureChannel channel, std::uint8_t* pixels) {
    switch (channel) {
    case MaterialTextureChannel::Normal:
        fill_solid_layer(pixels, 128, 128, 255);
        break;
    case MaterialTextureChannel::Smoothness:
        fill_solid_layer(pixels, byte_from_unit(1.0F - slot.roughness), 255, 255);
        break;
    case MaterialTextureChannel::AmbientOcclusion:
        fill_solid_layer(pixels, 255, 255, 255);
        break;
    case MaterialTextureChannel::Metallic:
        fill_solid_layer(pixels, byte_from_unit(slot.metallic), 255, 255);
        break;
    case MaterialTextureChannel::Albedo:
    default:
        fill_solid_layer(pixels, 255, 255, 255);
        break;
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

bool load_embedded_texture_layer(const MaterialTextureSource& source, std::uint8_t* pixels) {
    if (source.bytes.empty()) {
        return false;
    }

    return load_decoded_layer(
        source.codec,
        source.bytes,
        source.name,
        pixels
    );
}

} // namespace

GLuint material_texture_array_id(const MaterialTextureArray& textures, const MaterialTextureChannel channel) {
    return textures.textures[static_cast<std::size_t>(material_texture_channel_index(channel))];
}

void mark_material_textures_dirty(MaterialTextureArray& textures) {
    textures.dirty = true;
}

bool ensure_material_texture_array(MaterialTextureArray& textures, const MaterialLibrary& library) {
    if (glGenTextures == nullptr || glBindTexture == nullptr || glTexImage3D == nullptr) {
        return false;
    }

    const bool has_all_textures = std::all_of(
        textures.textures.begin(),
        textures.textures.end(),
        [](const GLuint texture) { return texture != 0; }
    );
    if (has_all_textures && !textures.dirty) {
        return true;
    }

    const MaterialLibrary normalized = normalized_material_library(library);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kMaterialTextureSize) *
        static_cast<std::size_t>(kMaterialTextureSize) *
        static_cast<std::size_t>(kMaterialTextureChannels) *
        static_cast<std::size_t>(kMaterialCount)
    );

    const std::filesystem::path base = std::filesystem::current_path();
    for (int channel_index = 0; channel_index < kMaterialTextureChannelCount; ++channel_index) {
        const auto channel = static_cast<MaterialTextureChannel>(channel_index);
        for (int i = 0; i < kMaterialCount; ++i) {
            const MaterialSlot& slot = normalized.slots[static_cast<std::size_t>(i)];
            const MaterialTextureSource& source = material_texture_source(slot, channel);
            auto* layer = pixels.data() +
                (static_cast<std::size_t>(i) * kMaterialTextureSize * kMaterialTextureSize * kMaterialTextureChannels);
            if (load_embedded_texture_layer(source, layer)) {
                continue;
            }

            if (source.path.empty()) {
                fill_fallback_layer(slot, channel, layer);
                continue;
            }

            std::filesystem::path texture_path = source.path;
            if (texture_path.is_relative()) {
                texture_path = base / texture_path;
            }
            const MaterialTextureImageCodec path_codec = material_texture_codec_for_path(texture_path);
            if (path_codec == MaterialTextureImageCodec::JpegXl) {
                std::vector<std::uint8_t> bytes;
                if (!read_file_bytes(texture_path, bytes) ||
                    !load_decoded_layer(MaterialTextureImageCodec::JpegXl, bytes, texture_path.string(), layer)) {
                    fill_fallback_layer(slot, channel, layer);
                }
            } else if (!load_surface_layer(texture_path.string(), layer)) {
                fill_fallback_layer(slot, channel, layer);
            }
        }

        GLuint& texture = textures.textures[static_cast<std::size_t>(channel_index)];
        if (texture == 0) {
            glGenTextures(1, &texture);
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            channel == MaterialTextureChannel::Albedo ? GL_SRGB8_ALPHA8 : GL_RGBA8,
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
    }

    textures.uploaded_library = normalized;
    textures.dirty = false;
    return true;
}

void destroy_material_texture_array(MaterialTextureArray& textures) {
    if (glDeleteTextures != nullptr) {
        for (GLuint texture : textures.textures) {
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
    }
    textures = {};
}

} // namespace undecedent
