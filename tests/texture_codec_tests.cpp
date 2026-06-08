#include "undecedent/texture_compression.hpp"
#include "undecedent/texture_image_codec.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

undecedent::DecodedTextureImage test_image() {
    undecedent::DecodedTextureImage image;
    image.width = 4;
    image.height = 4;
    image.rgba.resize(4U * 4U * 4U);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4U + static_cast<std::size_t>(x)) * 4U;
            image.rgba[offset + 0] = static_cast<std::uint8_t>(x * 60);
            image.rgba[offset + 1] = static_cast<std::uint8_t>(y * 60);
            image.rgba[offset + 2] = static_cast<std::uint8_t>((x + y) * 24);
            image.rgba[offset + 3] = 255;
        }
    }
    return image;
}

} // namespace

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    {
        std::vector<std::uint8_t> source(4096, 42);
        std::vector<std::uint8_t> compressed;
        std::vector<std::uint8_t> restored;
        std::string message;
        expect(undecedent::compress_lzma2_xz(source, compressed, message), "LZMA2 compression should succeed");
        expect(compressed.size() < source.size(), "LZMA2 should compress repeated bytes");
        expect(undecedent::decompress_lzma2_xz(compressed, source.size(), restored, message), "LZMA2 decompression should succeed");
        expect(restored == source, "LZMA2 round-trip should restore exact bytes");
    }

    {
        std::vector<std::uint8_t> source(1024, 7);
        std::vector<std::uint8_t> compressed;
        std::vector<std::uint8_t> restored;
        std::string message;
        expect(undecedent::compress_lzma2_xz(source, compressed, message), "LZMA2 compression should succeed before truncation");
        compressed.resize(compressed.size() / 2);
        expect(!undecedent::decompress_lzma2_xz(compressed, source.size(), restored, message), "Truncated LZMA2 should fail");
    }

    {
        const undecedent::DecodedTextureImage source = test_image();
        std::vector<std::uint8_t> encoded;
        undecedent::DecodedTextureImage decoded;
        std::string message;
        expect(undecedent::encode_jpeg_xl_rgba(source, true, 100, encoded, message), "Lossless JXL encode should succeed");
        expect(undecedent::decode_texture_image_bytes(
            undecedent::MaterialTextureImageCodec::JpegXl,
            encoded,
            "lossless-test.jxl",
            decoded,
            message
        ), "Lossless JXL decode should succeed");
        expect(decoded.width == source.width, "Lossless JXL width should round-trip");
        expect(decoded.height == source.height, "Lossless JXL height should round-trip");
        expect(decoded.rgba == source.rgba, "Lossless JXL pixels should round-trip exactly");
    }

    {
        const undecedent::DecodedTextureImage source = test_image();
        std::vector<std::uint8_t> encoded;
        undecedent::DecodedTextureImage decoded;
        std::string message;
        expect(undecedent::encode_jpeg_xl_rgba(source, false, 80, encoded, message), "Lossy JXL encode should succeed");
        expect(undecedent::decode_texture_image_bytes(
            undecedent::MaterialTextureImageCodec::JpegXl,
            encoded,
            "lossy-test.jxl",
            decoded,
            message
        ), "Lossy JXL decode should succeed");
        expect(decoded.width == source.width, "Lossy JXL width should round-trip");
        expect(decoded.height == source.height, "Lossy JXL height should round-trip");
        expect(decoded.rgba.size() == source.rgba.size(), "Lossy JXL decoded pixels should be RGBA");
    }

    return 0;
}
