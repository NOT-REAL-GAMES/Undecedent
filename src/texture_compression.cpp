#include "undecedent/texture_compression.hpp"

#include <lzma.h>

#include <limits>

namespace undecedent {
namespace {

constexpr std::uint64_t kMaxDecompressedTextureBytes = 256ULL * 1024ULL * 1024ULL;

} // namespace

std::uint32_t crc32_bytes(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool compress_lzma2_xz(
    const std::vector<std::uint8_t>& input,
    std::vector<std::uint8_t>& output,
    std::string& message
) {
    output.clear();
    if (input.empty()) {
        message = "Cannot compress empty texture payload.";
        return false;
    }

    const std::size_t bound = lzma_stream_buffer_bound(input.size());
    if (bound == 0 || bound > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        message = "Texture payload is too large to compress.";
        return false;
    }

    output.resize(bound);
    std::size_t out_pos = 0;
    const lzma_ret result = lzma_easy_buffer_encode(
        LZMA_PRESET_DEFAULT,
        LZMA_CHECK_CRC64,
        nullptr,
        input.data(),
        input.size(),
        output.data(),
        &out_pos,
        output.size()
    );
    if (result != LZMA_OK) {
        output.clear();
        message = "LZMA2 compression failed.";
        return false;
    }
    output.resize(out_pos);
    return true;
}

bool decompress_lzma2_xz(
    const std::vector<std::uint8_t>& input,
    const std::uint64_t expected_size,
    std::vector<std::uint8_t>& output,
    std::string& message
) {
    output.clear();
    if (expected_size == 0 || expected_size > kMaxDecompressedTextureBytes) {
        message = "Texture payload decompressed size is invalid.";
        return false;
    }
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        message = "Texture payload decompressed size is too large for this platform.";
        return false;
    }
    if (input.empty()) {
        message = "Compressed texture payload is empty.";
        return false;
    }

    output.resize(static_cast<std::size_t>(expected_size));
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret result = lzma_auto_decoder(&stream, UINT64_MAX, 0);
    if (result != LZMA_OK) {
        output.clear();
        message = "Could not initialize LZMA2 decompression.";
        return false;
    }

    stream.next_in = input.data();
    stream.avail_in = input.size();
    stream.next_out = output.data();
    stream.avail_out = output.size();
    result = lzma_code(&stream, LZMA_FINISH);
    const bool ok = result == LZMA_STREAM_END &&
        stream.avail_in == 0 &&
        stream.avail_out == 0;
    lzma_end(&stream);
    if (!ok) {
        output.clear();
        message = "LZMA2 decompression failed.";
        return false;
    }
    return true;
}

} // namespace undecedent
