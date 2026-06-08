#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace undecedent {

enum class TexturePayloadCompression : std::uint32_t {
    None = 0,
    XzLzma2 = 1,
};

std::uint32_t crc32_bytes(const std::vector<std::uint8_t>& bytes);
bool compress_lzma2_xz(
    const std::vector<std::uint8_t>& input,
    std::vector<std::uint8_t>& output,
    std::string& message
);
bool decompress_lzma2_xz(
    const std::vector<std::uint8_t>& input,
    std::uint64_t expected_size,
    std::vector<std::uint8_t>& output,
    std::string& message
);

} // namespace undecedent
