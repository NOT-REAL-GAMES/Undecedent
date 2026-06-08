#include "undecedent/map_io.hpp"

#include "undecedent/displacement.hpp"
#include "undecedent/texture_image_codec.hpp"

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using undecedent::PolygonLoop;
using undecedent::SectorPlane;
using undecedent::Vec2;

PolygonLoop loop(std::initializer_list<Vec2> vertices) {
    return PolygonLoop{std::vector<Vec2>(vertices)};
}

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::filesystem::path test_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> generated_jxl_bytes() {
    undecedent::DecodedTextureImage image;
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 0, 255,
    };
    std::vector<std::uint8_t> bytes;
    std::string message;
    expect(
        undecedent::encode_jpeg_xl_rgba(image, true, 100, bytes, message),
        "test helper should encode JPEG XL bytes"
    );
    return bytes;
}

bool file_starts_with(const std::filesystem::path& path, const std::string& prefix) {
    std::ifstream input(path, std::ios::binary);
    std::string bytes(prefix.size(), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return bytes == prefix;
}

bool has_displacement_sample_near(
    const undecedent::SectorSurfaceDisplacement& displacement,
    const Vec2 position,
    const float offset
) {
    for (const undecedent::SectorDisplacementSample& sample : displacement.samples) {
        if (std::abs(sample.position.x - position.x) <= 0.001F &&
            std::abs(sample.position.y - position.y) <= 0.001F &&
            std::abs(sample.offset - offset) <= 0.001F) {
            return true;
        }
    }
    return false;
}

void corrupt_last_byte(const std::filesystem::path& path) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    expect(size > 0, "file should be non-empty before corruption");
    file.seekg(size - 1);
    char byte = 0;
    file.read(&byte, 1);
    byte = static_cast<char>(byte ^ 0x7F);
    file.seekp(size - 1);
    file.write(&byte, 1);
}

std::uint32_t read_u32(const std::vector<unsigned char>& bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::uint64_t read_u64(const std::vector<unsigned char>& bytes, const std::size_t offset) {
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(shift / 8)]) << shift;
    }
    return value;
}

std::uint32_t read_payload_u32(const std::string& payload, const std::size_t offset) {
    const std::vector<unsigned char> bytes(payload.begin(), payload.end());
    return read_u32(bytes, offset);
}

std::uint64_t read_payload_u64(const std::string& payload, const std::size_t offset) {
    const std::vector<unsigned char> bytes(payload.begin(), payload.end());
    return read_u64(bytes, offset);
}

std::uint32_t checksum_bytes(const std::string& text) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const unsigned char byte : text) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

std::size_t chunk_count_for(const std::filesystem::path& path, const std::string& chunk_type) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    expect(bytes.size() >= 32, "chunked file should contain a header before count");
    const std::uint32_t chunk_count = read_u32(bytes, 12);
    constexpr std::size_t directory_offset = 32;
    constexpr std::size_t entry_size = 56;
    std::size_t matches = 0;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const std::size_t entry = directory_offset + (static_cast<std::size_t>(i) * entry_size);
        const std::string type(reinterpret_cast<const char*>(&bytes[entry]), 4);
        if (type == chunk_type) {
            ++matches;
        }
    }
    return matches;
}

void write_u32(std::vector<unsigned char>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<unsigned char>((value >> shift) & 0xFFU);
    }
}

void write_u64(std::vector<unsigned char>& bytes, const std::size_t offset, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes[offset + static_cast<std::size_t>(shift / 8)] =
            static_cast<unsigned char>((value >> shift) & 0xFFU);
    }
}

void set_chunk_id(
    const std::filesystem::path& path,
    const std::string& chunk_type,
    const std::uint64_t old_id,
    const std::uint64_t new_id
) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    expect(bytes.size() >= 32, "chunked file should contain a header before id mutation");
    const std::uint32_t chunk_count = read_u32(bytes, 12);
    constexpr std::size_t directory_offset = 32;
    constexpr std::size_t entry_size = 56;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const std::size_t entry = directory_offset + (static_cast<std::size_t>(i) * entry_size);
        const std::string type(reinterpret_cast<const char*>(&bytes[entry]), 4);
        const std::uint64_t id = read_u64(bytes, entry + 8);
        if (type == chunk_type && id == old_id) {
            write_u64(bytes, entry + 8, new_id);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return;
        }
    }
    expect(false, "set_chunk_id helper could not find requested chunk");
}

std::string chunk_payload(const std::filesystem::path& path, const std::string& chunk_type, const std::uint64_t chunk_id) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    expect(bytes.size() >= 32, "chunked file should contain a header");
    const std::uint32_t chunk_count = read_u32(bytes, 12);
    constexpr std::size_t directory_offset = 32;
    constexpr std::size_t entry_size = 56;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const std::size_t entry = directory_offset + (static_cast<std::size_t>(i) * entry_size);
        const std::string type(reinterpret_cast<const char*>(&bytes[entry]), 4);
        const std::uint64_t id = read_u64(bytes, entry + 8);
        if (type != chunk_type || id != chunk_id) {
            continue;
        }
        const std::uint64_t offset = read_u64(bytes, entry + 16);
        const std::uint64_t size = read_u64(bytes, entry + 24);
        return std::string(
            reinterpret_cast<const char*>(&bytes[static_cast<std::size_t>(offset)]),
            static_cast<std::size_t>(size)
        );
    }
    return {};
}

void replace_last_chunk_payload(
    const std::filesystem::path& path,
    const std::string& chunk_type,
    const std::uint64_t chunk_id,
    const std::string& payload
) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    expect(bytes.size() >= 32, "chunked file should contain a header before replacement");
    const std::uint32_t chunk_count = read_u32(bytes, 12);
    constexpr std::size_t directory_offset = 32;
    constexpr std::size_t entry_size = 56;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const std::size_t entry = directory_offset + (static_cast<std::size_t>(i) * entry_size);
        const std::string type(reinterpret_cast<const char*>(&bytes[entry]), 4);
        const std::uint64_t id = read_u64(bytes, entry + 8);
        if (type != chunk_type || id != chunk_id) {
            continue;
        }
        const std::uint64_t offset = read_u64(bytes, entry + 16);
        const std::uint64_t size = read_u64(bytes, entry + 24);
        expect(offset + size == bytes.size(), "replacement helper expects the target chunk to be last");
        bytes.resize(static_cast<std::size_t>(offset));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        write_u64(bytes, entry + 24, static_cast<std::uint64_t>(payload.size()));
        write_u32(bytes, entry + 32, checksum_bytes(payload));
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return;
    }
    expect(false, "replacement helper could not find requested chunk");
}

std::string sector_chunk_payload(const std::filesystem::path& path, const std::uint64_t sector_id) {
    return chunk_payload(path, "SECT", sector_id);
}

} // namespace

int main() {
    {
        const std::filesystem::path path = test_path("undecedent_map_io_rectangle.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "rectangle save should succeed");
        expect(file_starts_with(path, std::string("UDMAP3\0\0", 8)), "new saves should use chunked v3 magic");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "rectangle load should succeed");
        expect(loaded.sectors.size() == 1, "rectangle load should contain one sector");
        expect(loaded.sectors.front().id != 0, "loaded chunked sector should have a stable id");
        expect(loaded.sectors.front().height == 96.0F, "default height should round-trip");
        expect(!loaded.sectors.front().triangles.empty(), "loaded sector should rebuild triangles");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_hole_height.udmap");
        SectorPlane sector;
        sector.floor_height = 24.0F;
        sector.height = 144.0F;
        sector.outer = loop({{0, 0}, {20, 0}, {20, 20}, {0, 20}});
        sector.floor_material = 2;
        sector.ceiling_material = 3;
        sector.wall_materials = {4, 5, 6, 7};
        sector.holes.push_back(loop({{6, 6}, {14, 6}, {14, 14}, {6, 14}}));
        sector.hole_wall_materials = {{1, 2, 3, 4}};
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "sector with hole save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "sector with hole load should succeed");
        expect(loaded.sectors.size() == 1, "sector with hole load should contain one sector");
        expect(loaded.sectors.front().floor_height == 24.0F, "non-default floor should round-trip");
        expect(loaded.sectors.front().height == 144.0F, "non-default height should round-trip");
        expect(loaded.sectors.front().holes.size() == 1, "hole should round-trip");
        expect(loaded.sectors.front().floor_material == 2, "floor material should round-trip");
        expect(loaded.sectors.front().ceiling_material == 3, "ceiling material should round-trip");
        expect(loaded.sectors.front().wall_materials[1] == 5, "wall material should round-trip");
        expect(loaded.sectors.front().hole_wall_materials[0][2] == 3, "hole wall material should round-trip");
        expect(!loaded.sectors.front().triangles.empty(), "sector with hole should rebuild triangles");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_script_chunk.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
        const undecedent::ScriptCompileResult compiled =
            undecedent::compile_script("function on_map_start() { print(1); }");
        expect(compiled.ok, "script should compile before map save");
        undecedent::ScriptStore scripts;
        undecedent::set_global_script(scripts, compiled.program);

        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, scripts, path);
        expect(saved.ok, "script-bearing map save should succeed");
        expect(!chunk_payload(path, "SCRP", 0).empty(), "script-bearing save should include SCRP chunk");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "script-bearing map load should succeed");
        expect(loaded.scripts.has_global_script, "script chunk should restore global script");
        expect(!loaded.scripts.global_script.instructions.empty(), "script chunk should restore bytecode");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_sector_script_chunk.udmap");
        SectorPlane sector;
        sector.id = 88;
        sector.outer = loop({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
        const undecedent::ScriptCompileResult compiled =
            undecedent::compile_script("function on_sector_enter() { print(8); }");
        expect(compiled.ok, "sector script should compile before map save");
        undecedent::ScriptStore scripts;
        undecedent::set_sector_script(scripts, sector.id, compiled.program);

        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, scripts, path);
        expect(saved.ok, "sector-script map save should succeed");
        expect(chunk_payload(path, "SCRP", 0).find("sector_scripts 1") != std::string::npos,
            "script chunk should include sector script count");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "sector-script map load should succeed");
        expect(loaded.scripts.sector_scripts.contains(88), "script chunk should restore sector script");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_missing_sector_script.udmap");
        const undecedent::ScriptCompileResult compiled =
            undecedent::compile_script("function on_sector_enter() { print(8); }");
        expect(compiled.ok, "missing-sector script should compile before map save");
        undecedent::ScriptStore scripts;
        undecedent::set_sector_script(scripts, 999, compiled.program);

        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({}, {}, {}, {}, scripts, path);
        expect(saved.ok, "missing-sector script setup save should succeed");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "sector script referencing missing sector should reject");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_dirty_script_chunk.udmap");
        SectorPlane sector;
        sector.id = 77;
        sector.outer = loop({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
        undecedent::ScriptStore scripts;
        const undecedent::ScriptCompileResult first =
            undecedent::compile_script("function on_map_start() { print(1); }");
        expect(first.ok, "initial dirty script should compile");
        undecedent::set_global_script(scripts, first.program);
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, scripts, path);
        expect(saved.ok, "dirty script setup save should succeed");
        const std::string sector_before = sector_chunk_payload(path, 77);
        const std::string script_before = chunk_payload(path, "SCRP", 0);

        const undecedent::ScriptCompileResult second =
            undecedent::compile_script("function on_map_start() { print(2); }");
        expect(second.ok, "replacement dirty script should compile");
        undecedent::set_global_script(scripts, second.program);
        undecedent::set_sector_script(scripts, 77, second.program);
        undecedent::MapDirtyState dirty;
        dirty.scripts = true;
        const undecedent::SaveMapResult dirty_saved =
            undecedent::save_map_file_dirty({sector}, {}, {}, {}, scripts, dirty, path);
        expect(dirty_saved.ok, "dirty script save should succeed");
        expect(sector_chunk_payload(path, 77) == sector_before, "dirty script save should preserve sector chunk");
        expect(chunk_payload(path, "SCRP", 0) != script_before, "dirty script save should rewrite script chunk");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok && loaded.scripts.has_global_script, "dirty script map should load scripts");
        expect(loaded.scripts.sector_scripts.contains(77), "dirty script map should load sector scripts");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_displacement.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        sector.floor_displacement.enabled = true;
        sector.floor_displacement.resolution = 1;
        sector.floor_displacement.samples = {
            {{0, 0}, 0.0F},
            {{10, 0}, 8.0F},
            {{10, 10}, 8.0F},
            {{0, 10}, 0.0F},
        };
        sector.ceiling_displacement.enabled = true;
        sector.ceiling_displacement.resolution = 1;
        sector.ceiling_displacement.samples = {
            {{0, 0}, 0.0F},
            {{10, 0}, -4.0F},
            {{10, 10}, -4.0F},
            {{0, 10}, 0.0F},
        };
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "sector displacement save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "sector displacement load should succeed");
        expect(loaded.sectors.front().floor_displacement.enabled, "floor displacement should round-trip as enabled");
        expect(loaded.sectors.front().ceiling_displacement.enabled, "ceiling displacement should round-trip as enabled");
        expect(!loaded.sectors.front().floor_displacement.samples.empty(), "floor displacement samples should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_sparse_displacement.udmap");
        SectorPlane sector;
        sector.id = 300;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        sector.floor_displacement.enabled = true;
        sector.floor_displacement.resolution = 4;
        sector.floor_displacement.samples = {
            {{10, 0}, 8.0F},
        };
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "sparse displacement save should succeed");
        const std::string payload = sector_chunk_payload(path, 300);
        expect(
            payload.find("surface_displacement_sparse floor 4") != std::string::npos,
            "mostly flat displacement should save sparsely"
        );
        expect(
            payload.find("surface_displacement_sample") == std::string::npos,
            "sparse displacement should avoid dense sample position records"
        );

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "sparse displacement load should succeed");
        expect(loaded.sectors.front().floor_displacement.enabled, "sparse displacement should load enabled");
        expect(
            has_displacement_sample_near(loaded.sectors.front().floor_displacement, Vec2{10, 0}, 8.0F),
            "sparse displacement offset should round-trip at generated sample point"
        );
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_adjacency.udmap");
        SectorPlane left;
        left.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        SectorPlane right;
        right.outer = loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}});
        const undecedent::SaveMapResult saved = undecedent::save_map_file({left, right}, path);
        expect(saved.ok, "adjacent sectors save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "adjacent sectors load should succeed");
        expect(loaded.sectors.size() == 2, "adjacent load should contain two sectors");
        expect(loaded.sectors[0].edge_neighbors[1] == 1, "left shared edge should neighbor right sector");
        expect(loaded.sectors[1].edge_neighbors[3] == 0, "right shared edge should neighbor left sector");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_player_spawn.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        undecedent::PlayerSpawn spawn;
        spawn.position = undecedent::Vec3{5.0F, 48.0F, 6.0F};
        spawn.yaw = 1.25F;
        spawn.set = true;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, spawn, path);
        expect(saved.ok, "player spawn save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "player spawn load should succeed");
        expect(loaded.player_spawn.set, "player spawn should round-trip as set");
        expect(loaded.player_spawn.position.x == 5.0F, "player spawn x should round-trip");
        expect(loaded.player_spawn.position.y == 48.0F, "player spawn y should round-trip");
        expect(loaded.player_spawn.position.z == 6.0F, "player spawn z should round-trip");
        expect(loaded.player_spawn.yaw == 1.25F, "player spawn yaw should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_point_light.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        undecedent::PlayerSpawn spawn;
        spawn.set = true;
        spawn.position = undecedent::Vec3{5.0F, 48.0F, 5.0F};
        undecedent::PointLight light;
        light.position = undecedent::Vec3{4.0F, 64.0F, 6.0F};
        light.color = undecedent::Vec3{0.75F, 0.5F, 0.25F};
        light.radius = 256.0F;
        light.intensity = 2.25F;
        light.shadow_bias = 3.5F;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, spawn, {light}, path);
        expect(saved.ok, "point light save should succeed");
        expect(chunk_payload(path, "ENTY", 0).find("entities 2") != std::string::npos, "point light save should write entity v2");
        expect(chunk_payload(path, "ENTY", 0).find("3.5") != std::string::npos, "point light save should write shadow bias");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "point light load should succeed");
        expect(loaded.point_lights.size() == 1, "point light should round-trip");
        expect(loaded.point_lights.front().position.x == 4.0F, "point light x should round-trip");
        expect(loaded.point_lights.front().position.y == 64.0F, "point light y should round-trip");
        expect(loaded.point_lights.front().position.z == 6.0F, "point light z should round-trip");
        expect(loaded.point_lights.front().color.x == 0.75F, "point light color should round-trip");
        expect(loaded.point_lights.front().radius == 256.0F, "point light radius should round-trip");
        expect(loaded.point_lights.front().intensity == 2.25F, "point light intensity should round-trip");
        expect(loaded.point_lights.front().shadow_bias == 3.5F, "point light shadow bias should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_point_light_v1_entities.udmap");
        undecedent::PointLight light;
        light.id = 33;
        light.position = undecedent::Vec3{1.0F, 2.0F, 3.0F};
        light.radius = 128.0F;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({}, undecedent::PlayerSpawn{}, {light}, path);
        expect(saved.ok, "v1 entity compatibility setup save should succeed");
        replace_last_chunk_payload(path, "ENTY", 0,
            "entities 1\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 33 1 2 3 1 0.859999955 0.620000005 128 1.5\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "v1 entity chunk should load");
        expect(loaded.point_lights.size() == 1, "v1 entity light should load");
        expect(loaded.point_lights.front().shadow_bias == 2.0F, "v1 entity light should default shadow bias");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_world_lighting.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        undecedent::PlayerSpawn spawn;
        undecedent::WorldLighting lighting;
        lighting.sun_enabled = true;
        lighting.sun_direction = undecedent::Vec3{0.0F, -2.0F, 0.0F};
        lighting.sun_color = undecedent::Vec3{0.8F, 0.7F, 0.6F};
        lighting.sun_intensity = 1.25F;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, spawn, {}, lighting, path);
        expect(saved.ok, "world lighting save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "world lighting load should succeed");
        expect(loaded.world_lighting.sun_enabled, "sun enabled flag should round-trip");
        expect(std::abs(loaded.world_lighting.sun_direction.x - 0.0F) <= 0.001F, "sun direction x should normalize");
        expect(std::abs(loaded.world_lighting.sun_direction.y + 1.0F) <= 0.001F, "sun direction y should normalize");
        expect(std::abs(loaded.world_lighting.sun_direction.z - 0.0F) <= 0.001F, "sun direction z should normalize");
        expect(loaded.world_lighting.sun_color.x == 0.8F, "sun color should round-trip");
        expect(loaded.world_lighting.sun_intensity == 1.25F, "sun intensity should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_dirty_lighting.udmap");
        SectorPlane sector;
        sector.id = 400;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        undecedent::PlayerSpawn spawn;
        undecedent::PointLight light;
        light.id = 401;
        undecedent::WorldLighting lighting;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, spawn, {light}, lighting, path);
        expect(saved.ok, "dirty lighting setup save should succeed");
        const std::string sector_before = sector_chunk_payload(path, 400);
        const std::string entities_before = chunk_payload(path, "ENTY", 0);
        const std::string lighting_before = chunk_payload(path, "LITE", 0);
        lighting.sun_intensity = 2.0F;
        undecedent::MapDirtyState dirty;
        dirty.metadata = true;
        const undecedent::SaveMapResult dirty_saved =
            undecedent::save_map_file_dirty({sector}, spawn, {light}, lighting, dirty, path);
        expect(dirty_saved.ok, "dirty lighting save should succeed");
        expect(sector_chunk_payload(path, 400) == sector_before, "dirty lighting save should preserve sector chunk");
        expect(chunk_payload(path, "ENTY", 0) == entities_before, "dirty lighting save should preserve entity chunk");
        expect(chunk_payload(path, "LITE", 0) != lighting_before, "dirty lighting save should rewrite lighting chunk");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "dirty lighting map should load");
        expect(loaded.world_lighting.sun_intensity == 2.0F, "dirty lighting intensity should load");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_stacked.udmap");
        SectorPlane lower;
        lower.floor_height = 0.0F;
        lower.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        SectorPlane upper;
        upper.floor_height = 96.0F;
        upper.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        const undecedent::SaveMapResult saved = undecedent::save_map_file({lower, upper}, path);
        expect(saved.ok, "stacked sectors save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "stacked sectors load should succeed");
        expect(loaded.sectors.size() == 2, "stacked load should contain two sectors");
        expect(loaded.sectors[0].floor_height == 0.0F, "lower floor should round-trip");
        expect(loaded.sectors[1].floor_height == 96.0F, "upper floor should round-trip");
        expect(loaded.sectors[0].edge_neighbors[0] == -1, "stacked sectors should not become adjacent");
        expect(loaded.sectors[1].edge_neighbors[0] == -1, "upper stacked sector should not become adjacent");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_magic.udmap");
        write_text(path, "NOPE 1\nsectors 0\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "bad magic should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_legacy_no_floor.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "sectors 1\n"
            "sector\n"
            "height 96\n"
            "outer 4\n"
            "v 0 0\n"
            "v 10 0\n"
            "v 10 10\n"
            "v 0 10\n"
            "holes 0\n"
            "endsector\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "legacy map without floor should load");
        expect(!loaded.player_spawn.set, "legacy map without player spawn should keep spawn unset");
        expect(loaded.point_lights.empty(), "legacy map without point lights should load an empty light list");
        expect(loaded.world_lighting.sun_enabled, "legacy map should load default sun enabled");
        expect(loaded.world_lighting.sun_intensity > 0.0F, "legacy map should load default sun intensity");
        expect(loaded.sectors.front().floor_height == 0.0F, "legacy map floor should default to zero");
        expect(loaded.sectors.front().floor_material == 0, "legacy map floor material should default to zero");
        expect(loaded.sectors.front().wall_materials.size() == 4, "legacy map wall materials should be generated");
        const undecedent::SaveMapResult resaved =
            undecedent::save_map_file(loaded.sectors, loaded.player_spawn, loaded.point_lights, path);
        expect(resaved.ok, "legacy-loaded map should resave");
        expect(file_starts_with(path, std::string("UDMAP3\0\0", 8)), "legacy maps should resave as chunked v3");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_checksum.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "checksum test save should succeed");
        corrupt_last_byte(path);
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "corrupted chunk payload should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_dirty_sector.udmap");
        SectorPlane left;
        left.id = 100;
        left.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        SectorPlane right;
        right.id = 200;
        right.outer = loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}});
        right.floor_material = 3;
        undecedent::PlayerSpawn spawn;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({left, right}, spawn, {}, path);
        expect(saved.ok, "dirty save setup should succeed");
        const std::string right_before = sector_chunk_payload(path, 200);
        left.floor_material = 5;
        undecedent::MapDirtyState dirty;
        dirty.sector_ids.insert(100);
        const undecedent::SaveMapResult dirty_saved =
            undecedent::save_map_file_dirty({left, right}, spawn, {}, dirty, path);
        expect(dirty_saved.ok, "dirty sector save should succeed");
        expect(sector_chunk_payload(path, 200) == right_before, "unchanged sector chunk should be preserved");
        expect(sector_chunk_payload(path, 100).find("materials 5 0") != std::string::npos, "dirty sector chunk should be rewritten");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "dirty-saved map should load");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_light_radius.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 1 2 3 1 1 1 -4 1\n"
            "sectors 0\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "negative point light radius should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_sparse_displacement.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "sectors 1\n"
            "sector\n"
            "height 96\n"
            "outer 4\n"
            "v 0 0\n"
            "v 10 0\n"
            "v 10 10\n"
            "v 0 10\n"
            "holes 0\n"
            "surface_displacement_sparse floor 1 4 2\n"
            "surface_displacement_offset 0 4\n"
            "surface_displacement_offset 0 5\n"
            "endsector\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "duplicate sparse displacement sample index should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_light_intensity.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 1 2 3 1 1 1 4 -1\n"
            "sectors 0\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "negative point light intensity should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_light_shadow_bias.udmap");
        undecedent::PointLight light;
        light.id = 77;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({}, undecedent::PlayerSpawn{}, {light}, path);
        expect(saved.ok, "bad shadow bias setup save should succeed");
        replace_last_chunk_payload(path, "ENTY", 0,
            "entities 2\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 77 0 64 0 1 0.859999955 0.620000005 384 1.5 -0.25\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "negative point light shadow bias should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_light_float.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 1 2 nope 1 1 1 4 1\n"
            "sectors 0\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "malformed point light float should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_bad_light_shadow_bias_float.udmap");
        undecedent::PointLight light;
        light.id = 78;
        const undecedent::SaveMapResult saved = undecedent::save_map_file({}, undecedent::PlayerSpawn{}, {light}, path);
        expect(saved.ok, "bad shadow bias float setup save should succeed");
        replace_last_chunk_payload(path, "ENTY", 0,
            "entities 2\n"
            "player_spawn unset\n"
            "point_lights 1\n"
            "point_light 78 0 64 0 1 0.859999955 0.620000005 384 1.5 nope\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "malformed point light shadow bias should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_materials_v2.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        materials.slots[2].base_color = undecedent::MaterialColor{0.2F, 0.3F, 0.4F};
        materials.slots[2].roughness = 0.55F;
        materials.slots[2].metallic = 0.25F;
        materials.slots[2].specular = 0.15F;
        materials.slots[2].uv_scale = 128.0F;
        undecedent::material_texture_source(
            materials.slots[2],
            undecedent::MaterialTextureChannel::Albedo
        ).path = "textures/test_albedo.png";
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "materials v2 map should save");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "materials v2 map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 2);
        expect(std::abs(slot.base_color.r - 0.2F) <= 0.001F, "material color should round-trip");
        expect(std::abs(slot.roughness - 0.55F) <= 0.001F, "material roughness should round-trip");
        expect(std::abs(slot.metallic - 0.25F) <= 0.001F, "material metallic should round-trip");
        expect(std::abs(slot.specular - 0.15F) <= 0.001F, "material specular should round-trip");
        expect(std::abs(slot.uv_scale - 128.0F) <= 0.001F, "material UV scale should round-trip");
        expect(
            undecedent::material_texture_source(slot, undecedent::MaterialTextureChannel::Albedo).path ==
                "textures/test_albedo.png",
            "texture path should round-trip"
        );
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_embedded.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(
            materials,
            2,
            "textures/test_albedo.png",
            "test_albedo.png",
            std::vector<std::uint8_t>{0x89, 'P', 'N', 'G', 1, 2, 3}
        );
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "embedded material texture map should save");
        const std::string material_texture_payload = chunk_payload(path, "MTEX", 2);
        expect(!material_texture_payload.empty(), "embedded material texture should write MTEX chunk");
        expect(read_payload_u32(material_texture_payload, 0) == 3, "embedded texture should write MTEX v3");
        expect(read_payload_u32(material_texture_payload, 4) == 0, "source texture should store SDL image codec");
        expect(read_payload_u32(material_texture_payload, 8) == 0, "tiny texture should stay uncompressed");
        expect(read_payload_u64(material_texture_payload, 16) == 7, "MTEX v3 should record uncompressed byte count");
        expect(read_payload_u64(material_texture_payload, 24) == 7, "MTEX v3 should record stored byte count");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "embedded material texture map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 2);
        const undecedent::MaterialTextureSource& albedo =
            undecedent::material_texture_source(slot, undecedent::MaterialTextureChannel::Albedo);
        expect(albedo.path == "textures/test_albedo.png", "embedded texture path should round-trip");
        expect(albedo.name == "test_albedo.png", "embedded texture name should round-trip");
        expect(albedo.bytes == std::vector<std::uint8_t>({0x89, 'P', 'N', 'G', 1, 2, 3}),
            "embedded texture bytes should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_channels.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        for (int channel_index = 0; channel_index < undecedent::kMaterialTextureChannelCount; ++channel_index) {
            const auto channel = static_cast<undecedent::MaterialTextureChannel>(channel_index);
            const std::string short_label = undecedent::material_texture_channel_short_label(channel);
            undecedent::set_material_texture(
                materials,
                2,
                channel,
                "textures/channel_" + short_label + ".bin",
                "channel_" + short_label + ".bin",
                std::vector<std::uint8_t>{
                    static_cast<std::uint8_t>(10 + channel_index),
                    static_cast<std::uint8_t>(20 + channel_index)
                }
            );
        }

        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "multi-channel material texture map should save");
        expect(chunk_count_for(path, "MTEX") == undecedent::kMaterialTextureChannelCount,
            "multi-channel material texture map should write one texture chunk per channel");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "multi-channel material texture map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 2);
        for (int channel_index = 0; channel_index < undecedent::kMaterialTextureChannelCount; ++channel_index) {
            const auto channel = static_cast<undecedent::MaterialTextureChannel>(channel_index);
            const std::string short_label = undecedent::material_texture_channel_short_label(channel);
            const undecedent::MaterialTextureSource& source =
                undecedent::material_texture_source(slot, channel);
            expect(source.path == "textures/channel_" + short_label + ".bin",
                "multi-channel texture path should round-trip");
            expect(source.name == "channel_" + short_label + ".bin",
                "multi-channel texture name should round-trip");
            expect(source.bytes == std::vector<std::uint8_t>({
                    static_cast<std::uint8_t>(10 + channel_index),
                    static_cast<std::uint8_t>(20 + channel_index)
                }),
                "multi-channel texture bytes should round-trip");
        }
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_compressed.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        std::vector<std::uint8_t> repeated_bytes(4096, 42);
        undecedent::set_material_texture(materials, 2, "textures/repeated.bin", "repeated.bin", repeated_bytes);
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "compressed material texture map should save");
        const std::string material_texture_payload = chunk_payload(path, "MTEX", 2);
        expect(read_payload_u32(material_texture_payload, 0) == 3, "compressed texture should write MTEX v3");
        expect(read_payload_u32(material_texture_payload, 8) == 1, "repeated texture bytes should use LZMA2");
        expect(read_payload_u64(material_texture_payload, 16) == repeated_bytes.size(),
            "compressed MTEX should record original byte count");
        expect(read_payload_u64(material_texture_payload, 24) < repeated_bytes.size(),
            "compressed MTEX should store fewer bytes");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "compressed material texture map should load");
        expect(
            undecedent::material_texture_source(
                undecedent::material_slot(loaded.material_library, 2),
                undecedent::MaterialTextureChannel::Albedo
            ).bytes == repeated_bytes,
            "compressed material texture bytes should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_jxl_lossless.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(
            materials,
            5,
            "textures/generated.jxl",
            "generated.jxl",
            generated_jxl_bytes()
        );
        undecedent::material_texture_source(
            materials.slots[5],
            undecedent::MaterialTextureChannel::Albedo
        ).storage_mode = undecedent::MaterialTextureStorageMode::JpegXlLossless;
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "JXL-lossless material texture map should save");
        const std::string material_texture_payload = chunk_payload(path, "MTEX", 5);
        expect(read_payload_u32(material_texture_payload, 0) == 3, "JXL texture should write MTEX v3");
        expect(read_payload_u32(material_texture_payload, 4) == 1, "JXL texture should store JPEG XL image codec");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "JXL material texture map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 5);
        const undecedent::MaterialTextureSource& jxl =
            undecedent::material_texture_source(slot, undecedent::MaterialTextureChannel::Albedo);
        expect(jxl.codec == undecedent::MaterialTextureImageCodec::JpegXl,
            "JXL material texture codec should round-trip");
        undecedent::DecodedTextureImage decoded;
        std::string decode_message;
        expect(
            undecedent::decode_texture_image_bytes(jxl.codec, jxl.bytes, jxl.name, decoded, decode_message),
            "JXL material texture bytes should decode after load"
        );
        expect(decoded.width == 2 && decoded.height == 2, "JXL material texture dimensions should round-trip");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_path_migration.udmap");
        const std::filesystem::path texture_path = path.parent_path() / "undecedent_material_source.bin";
        write_bytes(texture_path, std::vector<std::uint8_t>{10, 20, 30, 40});
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::material_texture_source(
            materials.slots[3],
            undecedent::MaterialTextureChannel::Albedo
        ).path = texture_path.filename().generic_string();
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "path-only material texture map should save");
        expect(!chunk_payload(path, "MTEX", 3).empty(), "path-only material texture should embed when source exists");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "path-migrated material texture map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 3);
        const undecedent::MaterialTextureSource& migrated =
            undecedent::material_texture_source(slot, undecedent::MaterialTextureChannel::Albedo);
        expect(migrated.name == texture_path.filename().generic_string(), "path-migrated texture should store source name");
        expect(migrated.bytes == std::vector<std::uint8_t>({10, 20, 30, 40}),
            "path-migrated texture should store source bytes");
        std::filesystem::remove(path);
        std::filesystem::remove(texture_path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_missing_source.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::material_texture_source(
            materials.slots[4],
            undecedent::MaterialTextureChannel::Albedo
        ).path = "missing_texture.png";
        const undecedent::SaveMapResult saved =
            undecedent::save_map_file({sector}, {}, {}, {}, materials, path);
        expect(saved.ok, "missing path-only material texture should not fail save");
        expect(saved.message.find("Warning:") != std::string::npos, "missing path-only material texture should warn");
        expect(chunk_count_for(path, "MTEX") == 0, "missing path-only material texture should not write MTEX");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "missing path-only material texture map should load");
        expect(
            undecedent::material_texture_source(
                undecedent::material_slot(loaded.material_library, 4),
                undecedent::MaterialTextureChannel::Albedo
            ).path == "missing_texture.png",
            "missing path-only material texture should retain fallback path");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_dirty.udmap");
        SectorPlane sector;
        sector.id = 500;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 1, "first.bin", "first.bin", std::vector<std::uint8_t>{1});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "dirty material texture setup should save");
        undecedent::set_material_texture(materials, 1, "second.bin", "second.bin", std::vector<std::uint8_t>{2, 3});
        undecedent::MapDirtyState dirty;
        dirty.materials = true;
        const undecedent::SaveMapResult dirty_saved =
            undecedent::save_map_file_dirty({sector}, {}, {}, {}, materials, dirty, path);
        expect(dirty_saved.ok, "dirty material texture save should succeed");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "dirty material texture map should load");
        const undecedent::MaterialSlot slot = undecedent::material_slot(loaded.material_library, 1);
        const undecedent::MaterialTextureSource& dirty_albedo =
            undecedent::material_texture_source(slot, undecedent::MaterialTextureChannel::Albedo);
        expect(dirty_albedo.name == "second.bin", "dirty material texture save should rewrite name");
        expect(dirty_albedo.bytes == std::vector<std::uint8_t>({2, 3}),
            "dirty material texture save should rewrite bytes");

        undecedent::clear_material_texture_path(materials, 1);
        const undecedent::SaveMapResult clear_saved =
            undecedent::save_map_file_dirty({sector}, {}, {}, {}, materials, dirty, path);
        expect(clear_saved.ok, "dirty material texture clear should save");
        expect(chunk_count_for(path, "MTEX") == 0, "dirty material texture clear should remove MTEX chunks");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_bad_id.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 2, "bad.bin", "bad.bin", std::vector<std::uint8_t>{1});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "bad material texture id setup should save");
        set_chunk_id(path, "MTEX", 2, 99);
        expect(!undecedent::load_map_file(path).ok, "out-of-range material texture chunk id should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_duplicate.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 1, "one.bin", "one.bin", std::vector<std::uint8_t>{1});
        undecedent::set_material_texture(materials, 2, "two.bin", "two.bin", std::vector<std::uint8_t>{2});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "duplicate material texture setup should save");
        set_chunk_id(path, "MTEX", 2, 1);
        expect(!undecedent::load_map_file(path).ok, "duplicate material texture chunk should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_truncated.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 2, "truncated.bin", "truncated.bin", std::vector<std::uint8_t>{1, 2, 3});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "truncated material texture setup should save");
        replace_last_chunk_payload(path, "MTEX", 2, std::string(4, '\0'));
        expect(!undecedent::load_map_file(path).ok, "truncated material texture chunk should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_bad_codec.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 2, "bad.bin", "bad.bin", std::vector<std::uint8_t>{1, 2, 3});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "bad material texture codec setup should save");
        std::string payload = chunk_payload(path, "MTEX", 2);
        payload[4] = static_cast<char>(99);
        replace_last_chunk_payload(path, "MTEX", 2, payload);
        expect(!undecedent::load_map_file(path).ok, "invalid material texture image codec should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_bad_compression.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 2, "bad.bin", "bad.bin", std::vector<std::uint8_t>{1, 2, 3});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "bad material texture compression setup should save");
        std::string payload = chunk_payload(path, "MTEX", 2);
        payload[8] = static_cast<char>(99);
        replace_last_chunk_payload(path, "MTEX", 2, payload);
        expect(!undecedent::load_map_file(path).ok, "invalid material texture compression codec should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_material_texture_bad_crc.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {32, 0}, {32, 32}, {0, 32}});
        undecedent::MaterialLibrary materials = undecedent::default_material_library();
        undecedent::set_material_texture(materials, 2, "bad.bin", "bad.bin", std::vector<std::uint8_t>{1, 2, 3});
        expect(undecedent::save_map_file({sector}, {}, {}, {}, materials, path).ok,
            "bad material texture CRC setup should save");
        std::string payload = chunk_payload(path, "MTEX", 2);
        payload[32] = static_cast<char>(payload[32] ^ 0x7F);
        replace_last_chunk_payload(path, "MTEX", 2, payload);
        expect(!undecedent::load_map_file(path).ok, "material texture CRC mismatch should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_invalid_loop.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "sectors 1\n"
            "sector\n"
            "height 96\n"
            "outer 2\n"
            "v 0 0\n"
            "v 1 0\n"
            "holes 0\n"
            "endsector\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "invalid loop should be rejected");
        std::filesystem::remove(path);
    }

    {
        const std::filesystem::path path = test_path("undecedent_map_io_self_intersection.udmap");
        write_text(path,
            "UNDECEDENT_MAP 1\n"
            "sectors 1\n"
            "sector\n"
            "height 96\n"
            "outer 4\n"
            "v 0 0\n"
            "v 10 10\n"
            "v 0 10\n"
            "v 10 0\n"
            "holes 0\n"
            "endsector\n");
        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(!loaded.ok, "self-intersecting loop should be rejected");
        std::filesystem::remove(path);
    }

    return EXIT_SUCCESS;
}
