#include "undecedent/texture_image_codec.hpp"

#include <SDL3/SDL.h>
#include <jxl/decode.h>
#include <jxl/encode.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace undecedent {
namespace {

constexpr std::uint32_t kMaxJxlTextureDimension = 8192;
constexpr int kTextureChannels = 4;

bool decode_sdl_image_bytes(
    const std::vector<std::uint8_t>& bytes,
    const std::string& label,
    DecodedTextureImage& image,
    std::string& message
) {
    if (bytes.empty()) {
        message = "Texture payload is empty.";
        return false;
    }

    SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (stream == nullptr) {
        message = "Could not open texture '" + label + "': " + SDL_GetError();
        return false;
    }

    SDL_Surface* loaded = SDL_LoadSurface_IO(stream, true);
    if (loaded == nullptr) {
        message = "Could not load texture '" + label + "': " + SDL_GetError();
        return false;
    }

    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        message = "Could not convert texture '" + label + "': " + SDL_GetError();
        return false;
    }

    if (!SDL_LockSurface(rgba)) {
        message = "Could not lock texture '" + label + "': " + SDL_GetError();
        SDL_DestroySurface(rgba);
        return false;
    }

    image.width = rgba->w;
    image.height = rgba->h;
    image.rgba.resize(
        static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height) *
        static_cast<std::size_t>(kTextureChannels)
    );
    const auto* source_pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    for (int y = 0; y < image.height; ++y) {
        const auto* row = source_pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch));
        auto* destination = image.rgba.data() +
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * kTextureChannels);
        std::copy_n(row, static_cast<std::size_t>(image.width) * kTextureChannels, destination);
    }

    SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    return true;
}

bool decode_jpeg_xl_bytes(
    const std::vector<std::uint8_t>& bytes,
    const std::string& label,
    DecodedTextureImage& image,
    std::string& message
) {
    if (bytes.empty()) {
        message = "JPEG XL texture payload is empty.";
        return false;
    }

    JxlDecoder* decoder = JxlDecoderCreate(nullptr);
    if (decoder == nullptr) {
        message = "Could not create JPEG XL decoder.";
        return false;
    }

    bool ok = false;
    JxlBasicInfo info{};
    const JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    if (JxlDecoderSubscribeEvents(decoder, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(decoder, bytes.data(), bytes.size()) != JXL_DEC_SUCCESS) {
        message = "Could not initialize JPEG XL decoder for '" + label + "'.";
        JxlDecoderDestroy(decoder);
        return false;
    }
    JxlDecoderCloseInput(decoder);

    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(decoder);
        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(decoder, &info) != JXL_DEC_SUCCESS) {
                message = "Could not read JPEG XL texture info for '" + label + "'.";
                break;
            }
            if (info.xsize == 0 || info.ysize == 0 ||
                info.xsize > kMaxJxlTextureDimension || info.ysize > kMaxJxlTextureDimension) {
                message = "JPEG XL texture dimensions are unsupported for '" + label + "'.";
                break;
            }
            image.width = static_cast<int>(info.xsize);
            image.height = static_cast<int>(info.ysize);
            std::size_t buffer_size = 0;
            if (JxlDecoderImageOutBufferSize(decoder, &format, &buffer_size) != JXL_DEC_SUCCESS ||
                buffer_size != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * kTextureChannels) {
                message = "JPEG XL texture buffer size is invalid for '" + label + "'.";
                break;
            }
            image.rgba.resize(buffer_size);
            if (JxlDecoderSetImageOutBuffer(decoder, &format, image.rgba.data(), image.rgba.size()) != JXL_DEC_SUCCESS) {
                message = "Could not set JPEG XL output buffer for '" + label + "'.";
                break;
            }
            continue;
        }
        if (status == JXL_DEC_FULL_IMAGE) {
            ok = true;
            continue;
        }
        if (status == JXL_DEC_SUCCESS) {
            if (!ok) {
                message = "JPEG XL texture did not contain an image for '" + label + "'.";
            }
            break;
        }
        message = "Could not decode JPEG XL texture '" + label + "'.";
        break;
    }

    JxlDecoderDestroy(decoder);
    if (!ok) {
        image = {};
    }
    return ok;
}

float distance_for_jxl_quality(const int quality) {
    const int clamped = std::clamp(quality, 1, 100);
    return std::max(0.0F, static_cast<float>(100 - clamped) / 10.0F);
}

} // namespace

bool decode_texture_image_bytes(
    const MaterialTextureImageCodec codec,
    const std::vector<std::uint8_t>& bytes,
    const std::string& label,
    DecodedTextureImage& image,
    std::string& message
) {
    image = {};
    switch (codec) {
    case MaterialTextureImageCodec::SdlSurfaceImage:
        return decode_sdl_image_bytes(bytes, label, image, message);
    case MaterialTextureImageCodec::JpegXl:
        return decode_jpeg_xl_bytes(bytes, label, image, message);
    default:
        message = "Unsupported texture image codec.";
        return false;
    }
}

bool encode_jpeg_xl_rgba(
    const DecodedTextureImage& image,
    const bool lossless,
    const int quality,
    std::vector<std::uint8_t>& bytes,
    std::string& message
) {
    bytes.clear();
    if (image.width <= 0 || image.height <= 0 || image.width > static_cast<int>(kMaxJxlTextureDimension) ||
        image.height > static_cast<int>(kMaxJxlTextureDimension) ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * kTextureChannels) {
        message = "Invalid RGBA texture image for JPEG XL encoding.";
        return false;
    }

    JxlEncoder* encoder = JxlEncoderCreate(nullptr);
    if (encoder == nullptr) {
        message = "Could not create JPEG XL encoder.";
        return false;
    }

    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = static_cast<std::uint32_t>(image.width);
    basic_info.ysize = static_cast<std::uint32_t>(image.height);
    basic_info.bits_per_sample = 8;
    basic_info.exponent_bits_per_sample = 0;
    basic_info.num_color_channels = 3;
    basic_info.num_extra_channels = 1;
    basic_info.alpha_bits = 8;
    basic_info.uses_original_profile = lossless ? JXL_TRUE : JXL_FALSE;

    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
    JxlEncoderFrameSettings* frame_settings = JxlEncoderFrameSettingsCreate(encoder, nullptr);
    const JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    if (JxlEncoderSetBasicInfo(encoder, &basic_info) != JXL_ENC_SUCCESS ||
        JxlEncoderSetColorEncoding(encoder, &color_encoding) != JXL_ENC_SUCCESS ||
        frame_settings == nullptr) {
        message = "Could not initialize JPEG XL encoder.";
        JxlEncoderDestroy(encoder);
        return false;
    }

    if (lossless) {
        if (JxlEncoderSetFrameLossless(frame_settings, JXL_TRUE) != JXL_ENC_SUCCESS) {
            message = "Could not configure lossless JPEG XL encoding.";
            JxlEncoderDestroy(encoder);
            return false;
        }
    } else if (JxlEncoderSetFrameDistance(frame_settings, distance_for_jxl_quality(quality)) != JXL_ENC_SUCCESS) {
        message = "Could not configure lossy JPEG XL encoding.";
        JxlEncoderDestroy(encoder);
        return false;
    }

    if (JxlEncoderAddImageFrame(frame_settings, &format, image.rgba.data(), image.rgba.size()) != JXL_ENC_SUCCESS) {
        message = "Could not add JPEG XL image frame.";
        JxlEncoderDestroy(encoder);
        return false;
    }
    JxlEncoderCloseInput(encoder);

    bytes.resize(16 * 1024);
    std::uint8_t* next_out = bytes.data();
    std::size_t avail_out = bytes.size();
    for (;;) {
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder, &next_out, &avail_out);
        if (status == JXL_ENC_SUCCESS) {
            bytes.resize(static_cast<std::size_t>(next_out - bytes.data()));
            JxlEncoderDestroy(encoder);
            return !bytes.empty();
        }
        if (status != JXL_ENC_NEED_MORE_OUTPUT) {
            message = "JPEG XL encoding failed.";
            JxlEncoderDestroy(encoder);
            bytes.clear();
            return false;
        }
        const std::size_t used = static_cast<std::size_t>(next_out - bytes.data());
        bytes.resize(bytes.size() * 2);
        next_out = bytes.data() + used;
        avail_out = bytes.size() - used;
    }
}

bool copy_decoded_texture_to_layer(
    const DecodedTextureImage& image,
    const std::string& label,
    const int target_size,
    std::uint8_t* pixels,
    std::string& message
) {
    if (image.width <= 0 || image.height <= 0 || target_size <= 0 || pixels == nullptr ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * kTextureChannels) {
        message = "Invalid decoded texture image for '" + label + "'.";
        return false;
    }

    SDL_Surface* rgba = SDL_CreateSurfaceFrom(
        image.width,
        image.height,
        SDL_PIXELFORMAT_RGBA32,
        const_cast<std::uint8_t*>(image.rgba.data()),
        image.width * kTextureChannels
    );
    if (rgba == nullptr) {
        message = "Could not wrap decoded texture '" + label + "': " + SDL_GetError();
        return false;
    }

    SDL_Surface* source = rgba;
    SDL_Surface* scaled = nullptr;
    if (image.width != target_size || image.height != target_size) {
        scaled = SDL_ScaleSurface(rgba, target_size, target_size, SDL_SCALEMODE_LINEAR);
        if (scaled == nullptr) {
            message = "Could not scale texture '" + label + "': " + SDL_GetError();
            SDL_DestroySurface(rgba);
            return false;
        }
        source = scaled;
    }

    if (!SDL_LockSurface(source)) {
        message = "Could not lock texture '" + label + "': " + SDL_GetError();
        SDL_DestroySurface(rgba);
        if (scaled != nullptr) {
            SDL_DestroySurface(scaled);
        }
        return false;
    }

    const auto* source_pixels = static_cast<const std::uint8_t*>(source->pixels);
    for (int y = 0; y < target_size; ++y) {
        const auto* row = source_pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(source->pitch));
        auto* destination = pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(target_size) * kTextureChannels);
        std::copy_n(row, static_cast<std::size_t>(target_size) * kTextureChannels, destination);
    }

    SDL_UnlockSurface(source);
    SDL_DestroySurface(rgba);
    if (scaled != nullptr) {
        SDL_DestroySurface(scaled);
    }
    return true;
}

} // namespace undecedent
