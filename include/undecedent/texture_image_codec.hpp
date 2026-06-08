#pragma once

#include "undecedent/materials.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace undecedent {

struct DecodedTextureImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

bool decode_texture_image_bytes(
    MaterialTextureImageCodec codec,
    const std::vector<std::uint8_t>& bytes,
    const std::string& label,
    DecodedTextureImage& image,
    std::string& message
);

bool encode_jpeg_xl_rgba(
    const DecodedTextureImage& image,
    bool lossless,
    int quality,
    std::vector<std::uint8_t>& bytes,
    std::string& message
);

bool copy_decoded_texture_to_layer(
    const DecodedTextureImage& image,
    const std::string& label,
    int target_size,
    std::uint8_t* pixels,
    std::string& message
);

} // namespace undecedent
