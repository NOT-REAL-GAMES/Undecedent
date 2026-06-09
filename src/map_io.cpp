#include "undecedent/map_io.hpp"

#include "undecedent/displacement.hpp"
#include "undecedent/texture_compression.hpp"
#include "undecedent/texture_image_codec.hpp"
#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace undecedent {
namespace {

constexpr const char* kMapMagic = "UNDECEDENT_MAP";
constexpr int kMapVersion = 2;
constexpr std::array<char, 8> kChunkedMagic{'U', 'D', 'M', 'A', 'P', '3', '\0', '\0'};
constexpr std::uint32_t kChunkedVersion = 3;
constexpr std::uint32_t kChunkFlagOptional = 1U << 0U;
constexpr std::uint32_t kChunkHeaderSize = 32;
constexpr std::uint32_t kChunkDirectoryEntrySize = 56;
constexpr float kSparseDisplacementEpsilon = 0.001F;
constexpr std::uint64_t kMaxEmbeddedTexturePayload = 256ULL * 1024ULL * 1024ULL;
constexpr int kMaxStoredMaterialTextureDimension = 2048;

constexpr std::uint32_t fourcc(const char a, const char b, const char c, const char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

constexpr std::uint32_t kChunkMeta = fourcc('M', 'E', 'T', 'A');
constexpr std::uint32_t kChunkSector = fourcc('S', 'E', 'C', 'T');
constexpr std::uint32_t kChunkEntities = fourcc('E', 'N', 'T', 'Y');
constexpr std::uint32_t kChunkLighting = fourcc('L', 'I', 'T', 'E');
constexpr std::uint32_t kChunkMaterials = fourcc('M', 'A', 'T', 'S');
constexpr std::uint32_t kChunkMaterialTexture = fourcc('M', 'T', 'E', 'X');
constexpr std::uint32_t kChunkEditorState = fourcc('E', 'D', 'S', 'T');
constexpr std::uint32_t kChunkScripts = fourcc('S', 'C', 'R', 'P');

SaveMapResult save_error(const std::string& message) {
    return SaveMapResult{false, message};
}

LoadMapResult load_error(const std::string& message) {
    return LoadMapResult{false, message, {}, {}, {}, {}, {}, {}};
}

std::uint32_t checksum_bytes(const std::vector<std::uint8_t>& bytes) {
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

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > bytes.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > bytes.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

std::vector<std::uint8_t> string_payload(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string payload_string(const std::vector<std::uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

bool read_expected(std::istream& input, const char* expected, std::string& message) {
    std::string token;
    if (!(input >> token)) {
        message = std::string("Expected '") + expected + "' but reached end of file.";
        return false;
    }

    if (token != expected) {
        message = "Expected '" + std::string(expected) + "' but found '" + token + "'.";
        return false;
    }

    return true;
}

bool read_count(std::istream& input, std::size_t& count, std::string& message) {
    if (!(input >> count)) {
        message = "Expected unsigned count.";
        return false;
    }
    return true;
}

bool read_float(std::istream& input, float& value, std::string& message) {
    if (!(input >> value)) {
        message = "Expected float value.";
        return false;
    }

    if (!std::isfinite(value)) {
        message = "Float value is not finite.";
        return false;
    }

    return true;
}

float vec3_length(const Vec3 value) {
    return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
}

Vec3 normalized_or_default(const Vec3 value, const Vec3 fallback) {
    const float length = vec3_length(value);
    if (!std::isfinite(length) || length <= 0.0001F) {
        return fallback;
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

WorldLighting normalized_lighting(WorldLighting lighting) {
    const WorldLighting defaults;
    lighting.sun_direction = normalized_or_default(lighting.sun_direction, defaults.sun_direction);
    if (!std::isfinite(lighting.sun_color.x) ||
        !std::isfinite(lighting.sun_color.y) ||
        !std::isfinite(lighting.sun_color.z)) {
        lighting.sun_color = defaults.sun_color;
    }
    lighting.sun_color.x = std::max(lighting.sun_color.x, 0.0F);
    lighting.sun_color.y = std::max(lighting.sun_color.y, 0.0F);
    lighting.sun_color.z = std::max(lighting.sun_color.z, 0.0F);
    if (!std::isfinite(lighting.sun_intensity) || lighting.sun_intensity < 0.0F) {
        lighting.sun_intensity = defaults.sun_intensity;
    }
    return lighting;
}

bool parse_float_token(const std::string& token, float& value, std::string& message) {
    std::istringstream stream(token);
    stream >> value;
    if (!stream || !stream.eof()) {
        message = "Expected float value.";
        return false;
    }
    if (!std::isfinite(value)) {
        message = "Float value is not finite.";
        return false;
    }
    return true;
}

bool read_material_id(std::istream& input, int& value, std::string& message) {
    if (!(input >> value)) {
        message = "Expected material id.";
        return false;
    }
    if (value < 0 || value >= kMaterialCount) {
        message = "Material id is out of range.";
        return false;
    }
    return true;
}

bool read_materials(std::istream& input, std::vector<int>& materials, const std::size_t expected_count, std::string& message) {
    std::size_t count = 0;
    if (!read_count(input, count, message)) {
        return false;
    }
    if (count != expected_count) {
        message = "Material count does not match geometry count.";
        return false;
    }
    materials.resize(count, kDefaultMaterialId);
    for (int& material : materials) {
        if (!read_material_id(input, material, message)) {
            return false;
        }
    }
    return true;
}

void normalize_materials(SectorPlane& sector) {
    sector.floor_material = clamped_material_id(sector.floor_material);
    sector.ceiling_material = clamped_material_id(sector.ceiling_material);
    sector.wall_materials.resize(sector.outer.vertices.size(), kDefaultMaterialId);
    for (int& material : sector.wall_materials) {
        material = clamped_material_id(material);
    }
    sector.hole_wall_materials.resize(sector.holes.size());
    for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
        sector.hole_wall_materials[hole_index].resize(sector.holes[hole_index].vertices.size(), kDefaultMaterialId);
        for (int& material : sector.hole_wall_materials[hole_index]) {
            material = clamped_material_id(material);
        }
    }
}

void normalize_sector(SectorPlane& sector) {
    normalize_materials(sector);
    normalize_displacement(sector, SectorSurfaceKind::Floor);
    normalize_displacement(sector, SectorSurfaceKind::Ceiling);
}

bool ensure_sector_triangulated(SectorPlane& sector, std::string& message) {
    if (!sector.triangles.empty() && sector.status == TriangulationStatus::Ok) {
        return true;
    }

    const TriangulationResult result = triangulate_polygon(sector.outer, sector.holes);
    sector.status = result.status;
    sector.status_message = result.message;
    sector.triangles = result.triangles;
    if (result.status != TriangulationStatus::Ok) {
        message = result.message;
        return false;
    }
    return true;
}

void prepare_displacement_for_write(SectorPlane& sector, const SectorSurfaceKind surface) {
    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    if (!displacement.enabled) {
        displacement.samples.clear();
        return;
    }

    ensure_displacement_samples(sector, surface);
    normalize_displacement(sector, surface);
}

SectorPlane prepared_sector_for_write(const SectorPlane& sector) {
    SectorPlane prepared = sector;
    std::string ignored;
    const bool triangulated = ensure_sector_triangulated(prepared, ignored);
    normalize_materials(prepared);
    if (triangulated) {
        prepare_displacement_for_write(prepared, SectorSurfaceKind::Floor);
        prepare_displacement_for_write(prepared, SectorSurfaceKind::Ceiling);
    }
    return prepared;
}

std::uint64_t assign_id(std::uint64_t& id, std::set<std::uint64_t>& used, std::uint64_t& next_id) {
    if (id != 0 && !used.contains(id)) {
        used.insert(id);
        next_id = std::max(next_id, id + 1U);
        return id;
    }

    while (next_id == 0 || used.contains(next_id)) {
        ++next_id;
    }
    id = next_id++;
    used.insert(id);
    return id;
}

std::uint64_t assign_missing_stable_ids(
    std::vector<SectorPlane>& sectors,
    PlayerSpawn& player_spawn,
    std::vector<PointLight>& point_lights
) {
    std::set<std::uint64_t> used;
    std::uint64_t next_id = 1;
    for (SectorPlane& sector : sectors) {
        assign_id(sector.id, used, next_id);
    }
    if (player_spawn.set) {
        assign_id(player_spawn.id, used, next_id);
    }
    for (PointLight& light : point_lights) {
        assign_id(light.id, used, next_id);
    }
    return next_id;
}

bool ids_are_unique(const std::vector<SectorPlane>& sectors, std::string& message) {
    std::set<std::uint64_t> used;
    for (const SectorPlane& sector : sectors) {
        if (sector.id == 0) {
            message = "Sector chunk has zero stable id.";
            return false;
        }
        if (!used.insert(sector.id).second) {
            message = "Duplicate sector stable id.";
            return false;
        }
    }
    return true;
}

bool read_loop_after_label(std::istream& input, const char* label, PolygonLoop& loop, std::string& message) {
    std::size_t vertex_count = 0;
    if (!read_count(input, vertex_count, message)) {
        return false;
    }

    if (vertex_count < 3) {
        message = std::string(label) + " loop must contain at least three vertices.";
        return false;
    }

    loop.vertices.clear();
    loop.vertices.reserve(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        if (!read_expected(input, "v", message)) {
            return false;
        }

        Vec2 vertex{};
        if (!read_float(input, vertex.x, message) || !read_float(input, vertex.y, message)) {
            return false;
        }

        loop.vertices.push_back(vertex);
    }

    return true;
}

bool read_loop(std::istream& input, const char* label, PolygonLoop& loop, std::string& message) {
    if (!read_expected(input, label, message)) {
        return false;
    }
    return read_loop_after_label(input, label, loop, message);
}

bool same_point_exact(const Vec2 a, const Vec2 b) {
    return a.x == b.x && a.y == b.y;
}

bool vertical_ranges_overlap(const SectorPlane& a, const SectorPlane& b) {
    const float lower = std::max(a.floor_height, b.floor_height);
    const float upper = std::min(a.floor_height + a.height, b.floor_height + b.height);
    return upper > lower + kGeometryEpsilon;
}

void rebuild_exact_adjacency(std::vector<SectorPlane>& sectors) {
    for (SectorPlane& sector : sectors) {
        sector.edge_neighbors.assign(sector.outer.vertices.size(), -1);
    }

    for (std::size_t sector_a = 0; sector_a < sectors.size(); ++sector_a) {
        SectorPlane& a = sectors[sector_a];
        for (std::size_t edge_a = 0; edge_a < a.outer.vertices.size(); ++edge_a) {
            const Vec2 a0 = a.outer.vertices[edge_a];
            const Vec2 a1 = a.outer.vertices[(edge_a + 1) % a.outer.vertices.size()];

            for (std::size_t sector_b = sector_a + 1; sector_b < sectors.size(); ++sector_b) {
                SectorPlane& b = sectors[sector_b];
                if (!vertical_ranges_overlap(a, b)) {
                    continue;
                }
                for (std::size_t edge_b = 0; edge_b < b.outer.vertices.size(); ++edge_b) {
                    const Vec2 b0 = b.outer.vertices[edge_b];
                    const Vec2 b1 = b.outer.vertices[(edge_b + 1) % b.outer.vertices.size()];
                    if (same_point_exact(a0, b1) && same_point_exact(a1, b0)) {
                        a.edge_neighbors[edge_a] = static_cast<int>(sector_b);
                        b.edge_neighbors[edge_b] = static_cast<int>(sector_a);
                    }
                }
            }
        }
    }
}

LoadMapResult rebuild_loaded_sectors(
    std::vector<SectorPlane> sectors,
    const PlayerSpawn player_spawn,
    std::vector<PointLight> point_lights,
    const WorldLighting world_lighting = {},
    MaterialLibrary material_library = {},
    ScriptStore scripts = {}
) {
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        SectorPlane& sector = sectors[i];
        const TriangulationResult result = triangulate_polygon(sector.outer, sector.holes);
        sector.status = result.status;
        sector.status_message = result.message;
        sector.triangles = result.triangles;
        normalize_sector(sector);
        if (result.status != TriangulationStatus::Ok) {
            std::ostringstream stream;
            stream << "Sector " << i << " failed triangulation: " << result.message;
            return load_error(stream.str());
        }
    }

    rebuild_exact_adjacency(sectors);
    return LoadMapResult{
        true,
        "Loaded map.",
        std::move(sectors),
        player_spawn,
        std::move(point_lights),
        normalized_lighting(world_lighting),
        normalized_material_library(std::move(material_library)),
        std::move(scripts)
    };
}

void write_displacement(
    std::ostream& output,
    const char* label,
    const SectorSurfaceDisplacement& displacement
) {
    if (!displacement.enabled || displacement.samples.empty()) {
        return;
    }
    output << "surface_displacement " << label << ' '
           << clamped_displacement_resolution(displacement.resolution) << ' '
           << displacement.samples.size() << '\n';
    for (const SectorDisplacementSample& sample : displacement.samples) {
        output << "surface_displacement_sample "
               << sample.position.x << ' '
               << sample.position.y << ' '
               << sample.offset << '\n';
    }
}

std::string dense_displacement_record(const char* label, const SectorSurfaceDisplacement& displacement) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    write_displacement(output, label, displacement);
    return output.str();
}

std::string sparse_displacement_record(const char* label, const SectorSurfaceDisplacement& displacement) {
    if (!displacement.enabled || displacement.samples.empty()) {
        return {};
    }

    std::vector<std::pair<std::size_t, float>> changed;
    changed.reserve(displacement.samples.size());
    for (std::size_t i = 0; i < displacement.samples.size(); ++i) {
        const float offset = displacement.samples[i].offset;
        if (std::isfinite(offset) && std::abs(offset) > kSparseDisplacementEpsilon) {
            changed.push_back({i, offset});
        }
    }

    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "surface_displacement_sparse " << label << ' '
           << clamped_displacement_resolution(displacement.resolution) << ' '
           << displacement.samples.size() << ' '
           << changed.size() << '\n';
    for (const auto& [index, offset] : changed) {
        output << "surface_displacement_offset " << index << ' ' << offset << '\n';
    }
    return output.str();
}

void write_adaptive_displacement(
    std::ostream& output,
    const char* label,
    const SectorSurfaceDisplacement& displacement
) {
    if (!displacement.enabled || displacement.samples.empty()) {
        return;
    }

    const std::string dense = dense_displacement_record(label, displacement);
    const std::string sparse = sparse_displacement_record(label, displacement);
    output << (!sparse.empty() && sparse.size() < dense.size() ? sparse : dense);
}

void write_materials(std::ostream& output, SectorPlane sector) {
    normalize_materials(sector);
    output << "materials " << sector.floor_material << ' ' << sector.ceiling_material << '\n';
    output << "wall_materials " << sector.wall_materials.size();
    for (const int material : sector.wall_materials) {
        output << ' ' << material;
    }
    output << '\n';
    output << "hole_wall_materials " << sector.hole_wall_materials.size() << '\n';
    for (const std::vector<int>& materials : sector.hole_wall_materials) {
        output << "hole_wall_materials " << materials.size();
        for (const int material : materials) {
            output << ' ' << material;
        }
        output << '\n';
    }
}

bool read_displacement(
    std::istream& input,
    SectorPlane& sector,
    std::string& message
) {
    std::string surface;
    int resolution = 0;
    std::size_t count = 0;
    if (!(input >> surface >> resolution)) {
        message = "Expected displacement surface and resolution.";
        return false;
    }
    if (!read_count(input, count, message)) {
        return false;
    }
    SectorSurfaceKind kind{};
    if (surface == "floor") {
        kind = SectorSurfaceKind::Floor;
    } else if (surface == "ceiling") {
        kind = SectorSurfaceKind::Ceiling;
    } else {
        message = "Expected displacement surface 'floor' or 'ceiling'.";
        return false;
    }

    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, kind);
    displacement.enabled = true;
    displacement.resolution = clamped_displacement_resolution(resolution);
    displacement.samples.clear();
    displacement.samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!read_expected(input, "surface_displacement_sample", message)) {
            return false;
        }
        SectorDisplacementSample sample;
        if (!read_float(input, sample.position.x, message) ||
            !read_float(input, sample.position.y, message) ||
            !read_float(input, sample.offset, message)) {
            return false;
        }
        displacement.samples.push_back(sample);
    }
    return true;
}

bool read_displacement_sparse(
    std::istream& input,
    SectorPlane& sector,
    std::string& message
) {
    std::string surface;
    int resolution = 0;
    std::size_t total_count = 0;
    std::size_t changed_count = 0;
    if (!(input >> surface >> resolution)) {
        message = "Expected sparse displacement surface and resolution.";
        return false;
    }
    if (!read_count(input, total_count, message) || !read_count(input, changed_count, message)) {
        return false;
    }

    SectorSurfaceKind kind{};
    if (surface == "floor") {
        kind = SectorSurfaceKind::Floor;
    } else if (surface == "ceiling") {
        kind = SectorSurfaceKind::Ceiling;
    } else {
        message = "Expected sparse displacement surface 'floor' or 'ceiling'.";
        return false;
    }

    if (!ensure_sector_triangulated(sector, message)) {
        message = "Cannot read sparse displacement for invalid sector triangulation: " + message;
        return false;
    }

    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, kind);
    displacement.enabled = true;
    displacement.resolution = clamped_displacement_resolution(resolution);
    displacement.samples.clear();
    ensure_displacement_samples(sector, kind);
    if (displacement.samples.size() != total_count) {
        message = "Sparse displacement sample count does not match generated samples.";
        return false;
    }

    std::vector<bool> seen(total_count, false);
    for (std::size_t i = 0; i < changed_count; ++i) {
        if (!read_expected(input, "surface_displacement_offset", message)) {
            return false;
        }
        std::size_t index = 0;
        if (!(input >> index)) {
            message = "Expected sparse displacement sample index.";
            return false;
        }
        if (index >= displacement.samples.size()) {
            message = "Sparse displacement sample index is out of range.";
            return false;
        }
        if (seen[index]) {
            message = "Sparse displacement sample index is duplicated.";
            return false;
        }
        seen[index] = true;
        if (!read_float(input, displacement.samples[index].offset, message)) {
            return false;
        }
    }
    return true;
}

void write_loop(std::ostream& output, const char* label, const PolygonLoop& loop) {
    output << label << ' ' << loop.vertices.size() << '\n';
    for (const Vec2 vertex : loop.vertices) {
        output << "v " << vertex.x << ' ' << vertex.y << '\n';
    }
}

void write_sector_record(std::ostream& output, const SectorPlane& sector) {
    const SectorPlane prepared = prepared_sector_for_write(sector);
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "sector\n";
    output << "height " << prepared.height << '\n';
    output << "floor " << prepared.floor_height << '\n';
    write_loop(output, "outer", prepared.outer);
    output << "holes " << prepared.holes.size() << '\n';
    for (const PolygonLoop& hole : prepared.holes) {
        write_loop(output, "hole", hole);
    }
    write_materials(output, prepared);
    write_adaptive_displacement(output, "floor", prepared.floor_displacement);
    write_adaptive_displacement(output, "ceiling", prepared.ceiling_displacement);
    output << "endsector\n";
}

bool read_sector_record(std::istream& input, SectorPlane& sector, std::string& message) {
    if (!read_expected(input, "sector", message)) {
        return false;
    }

    if (!read_expected(input, "height", message)) {
        return false;
    }
    if (!read_float(input, sector.height, message)) {
        return false;
    }
    if (sector.height <= 0.0F) {
        message = "Sector height must be greater than zero.";
        return false;
    }

    std::string next_token;
    if (!(input >> next_token)) {
        message = "Expected 'floor' or 'outer' after sector height.";
        return false;
    }

    if (next_token == "floor") {
        if (!read_float(input, sector.floor_height, message)) {
            return false;
        }
        if (!read_loop(input, "outer", sector.outer, message)) {
            return false;
        }
    } else if (next_token == "outer") {
        sector.floor_height = 0.0F;
        if (!read_loop_after_label(input, "outer", sector.outer, message)) {
            return false;
        }
    } else {
        message = "Expected 'floor' or 'outer' but found '" + next_token + "'.";
        return false;
    }

    if (!read_expected(input, "holes", message)) {
        return false;
    }

    std::size_t hole_count = 0;
    if (!read_count(input, hole_count, message)) {
        return false;
    }

    sector.holes.reserve(hole_count);
    for (std::size_t hole_index = 0; hole_index < hole_count; ++hole_index) {
        PolygonLoop hole;
        if (!read_loop(input, "hole", hole, message)) {
            return false;
        }
        sector.holes.push_back(std::move(hole));
    }

    std::string token;
    bool read_material_block = false;
    while (true) {
        if (!(input >> token)) {
            message = "Expected material/displacement fields or 'endsector'.";
            return false;
        }
        if (token == "endsector") {
            break;
        }
        if (token == "materials") {
            if (read_material_block) {
                message = "Duplicate materials block.";
                return false;
            }
            read_material_block = true;
            if (!read_material_id(input, sector.floor_material, message) ||
                !read_material_id(input, sector.ceiling_material, message)) {
                return false;
            }
            if (!read_expected(input, "wall_materials", message)) {
                return false;
            }
            if (!read_materials(input, sector.wall_materials, sector.outer.vertices.size(), message)) {
                return false;
            }
            if (!read_expected(input, "hole_wall_materials", message)) {
                return false;
            }
            std::size_t hole_material_group_count = 0;
            if (!read_count(input, hole_material_group_count, message)) {
                return false;
            }
            if (hole_material_group_count != sector.holes.size()) {
                message = "Hole wall material group count does not match hole count.";
                return false;
            }
            sector.hole_wall_materials.resize(hole_material_group_count);
            for (std::size_t hole_index = 0; hole_index < hole_material_group_count; ++hole_index) {
                if (!read_expected(input, "hole_wall_materials", message)) {
                    return false;
                }
                if (!read_materials(
                        input,
                        sector.hole_wall_materials[hole_index],
                        sector.holes[hole_index].vertices.size(),
                        message
                    )) {
                    return false;
                }
            }
            continue;
        }
        if (token == "surface_displacement") {
            if (!read_displacement(input, sector, message)) {
                return false;
            }
            continue;
        }
        if (token == "surface_displacement_sparse") {
            if (!read_displacement_sparse(input, sector, message)) {
                return false;
            }
            continue;
        }
        message = "Expected material/displacement fields or 'endsector' but found '" + token + "'.";
        return false;
    }

    normalize_materials(sector);
    return true;
}

struct ChunkRecord {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t id = 0;
    std::vector<std::uint8_t> data;
};

struct ChunkDirectoryEntry {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t checksum = 0;
};

struct MaterialTexturePayload {
    std::string name;
    std::vector<std::uint8_t> bytes;
    MaterialTextureImageCodec codec = MaterialTextureImageCodec::SdlSurfaceImage;
    std::uint32_t version = 0;
};

struct PreparedMaterialChunks {
    MaterialLibrary library;
    std::vector<ChunkRecord> texture_chunks;
    std::vector<std::string> warnings;
};

std::string write_meta_payload(const std::uint64_t next_id) {
    std::ostringstream output;
    output << "meta 1\n";
    output << "format 3\n";
    output << "generator Undecedent\n";
    output << "next_id " << next_id << '\n';
    return output.str();
}

std::string write_sector_payload(const SectorPlane& sector) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    write_sector_record(output, sector);
    return output.str();
}

std::string write_entities_payload(const PlayerSpawn& player_spawn, const std::vector<PointLight>& point_lights) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "entities 2\n";
    if (player_spawn.set) {
        output << "player_spawn "
               << player_spawn.id << ' '
               << player_spawn.position.x << ' '
               << player_spawn.position.y << ' '
               << player_spawn.position.z << ' '
               << player_spawn.yaw << '\n';
    } else {
        output << "player_spawn unset\n";
    }
    output << "point_lights " << point_lights.size() << '\n';
    for (const PointLight& light : point_lights) {
        output << "point_light "
               << light.id << ' '
               << light.position.x << ' '
               << light.position.y << ' '
               << light.position.z << ' '
               << light.color.x << ' '
               << light.color.y << ' '
               << light.color.z << ' '
               << light.radius << ' '
               << light.intensity << ' '
               << std::max(light.shadow_bias, 0.0F) << '\n';
    }
    return output.str();
}

std::string write_materials_payload(MaterialLibrary material_library) {
    material_library = normalized_material_library(std::move(material_library));
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "materials 4\n";
    output << "count " << kMaterialCount << '\n';
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialSlot& slot = material_library.slots[static_cast<std::size_t>(i)];
        output << "slot "
               << i << ' '
               << slot.base_color.r << ' '
               << slot.base_color.g << ' '
               << slot.base_color.b << ' '
               << slot.roughness << ' '
               << slot.metallic << ' '
               << slot.specular << ' '
               << slot.uv_scale << '\n';
    }
    for (int i = 0; i < kMaterialCount; ++i) {
        const MaterialSlot& slot = material_library.slots[static_cast<std::size_t>(i)];
        for (int channel_index = 0; channel_index < kMaterialTextureChannelCount; ++channel_index) {
            const auto channel = static_cast<MaterialTextureChannel>(channel_index);
            const MaterialTextureSource& source = material_texture_source(slot, channel);
            output << "texture "
                   << i << ' '
                   << static_cast<std::uint32_t>(channel) << ' '
                   << std::quoted(source.path) << ' '
                   << static_cast<std::uint32_t>(source.codec) << ' '
                   << static_cast<std::uint32_t>(source.storage_mode) << ' '
                   << source.jxl_quality << '\n';
        }
    }
    return output.str();
}

std::uint64_t material_texture_chunk_id(const int material_id, const MaterialTextureChannel channel) {
    return (static_cast<std::uint64_t>(material_texture_channel_index(channel)) << 32U) |
        static_cast<std::uint64_t>(clamped_material_id(material_id));
}

bool decode_material_texture_chunk_id(
    const std::uint64_t chunk_id,
    const std::uint32_t payload_version,
    std::size_t& material_id,
    MaterialTextureChannel& channel
) {
    if (payload_version <= 2) {
        if (chunk_id >= static_cast<std::uint64_t>(kMaterialCount)) {
            return false;
        }
        material_id = static_cast<std::size_t>(chunk_id);
        channel = MaterialTextureChannel::Albedo;
        return true;
    }

    const std::uint64_t material_bits = chunk_id & 0xffffffffULL;
    const std::uint64_t channel_bits = chunk_id >> 32U;
    if (material_bits >= static_cast<std::uint64_t>(kMaterialCount) ||
        channel_bits >= static_cast<std::uint64_t>(kMaterialTextureChannelCount)) {
        return false;
    }
    material_id = static_cast<std::size_t>(material_bits);
    channel = static_cast<MaterialTextureChannel>(static_cast<std::uint32_t>(channel_bits));
    return true;
}

bool valid_material_texture_image_codec(const std::uint32_t codec) {
    return codec == static_cast<std::uint32_t>(MaterialTextureImageCodec::SdlSurfaceImage) ||
        codec == static_cast<std::uint32_t>(MaterialTextureImageCodec::JpegXl);
}

bool valid_material_texture_storage_mode(const std::uint32_t mode) {
    return mode == static_cast<std::uint32_t>(MaterialTextureStorageMode::SourceBytes) ||
        mode == static_cast<std::uint32_t>(MaterialTextureStorageMode::JpegXlLossless) ||
        mode == static_cast<std::uint32_t>(MaterialTextureStorageMode::JpegXlLossy);
}

bool valid_texture_payload_compression(const std::uint32_t codec) {
    return codec == static_cast<std::uint32_t>(TexturePayloadCompression::None) ||
        codec == static_cast<std::uint32_t>(TexturePayloadCompression::XzLzma2);
}

std::uint8_t bilinear_sample_channel(
    const DecodedTextureImage& image,
    const float source_x,
    const float source_y,
    const int channel
) {
    constexpr int kChannels = 4;
    const float clamped_x = std::clamp(source_x, 0.0F, static_cast<float>(image.width - 1));
    const float clamped_y = std::clamp(source_y, 0.0F, static_cast<float>(image.height - 1));
    const int x0 = static_cast<int>(std::floor(clamped_x));
    const int y0 = static_cast<int>(std::floor(clamped_y));
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);
    const auto pixel = [&image, channel](const int x, const int y) {
        return static_cast<float>(image.rgba[
            ((static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) +
                static_cast<std::size_t>(x)) *
                kChannels +
            static_cast<std::size_t>(channel)
        ]);
    };

    const float top = pixel(x0, y0) + ((pixel(x1, y0) - pixel(x0, y0)) * tx);
    const float bottom = pixel(x0, y1) + ((pixel(x1, y1) - pixel(x0, y1)) * tx);
    return static_cast<std::uint8_t>(std::clamp(top + ((bottom - top) * ty), 0.0F, 255.0F));
}

DecodedTextureImage resize_texture_to_max_dimension(const DecodedTextureImage& image, const int max_dimension) {
    constexpr int kChannels = 4;
    if (image.width <= max_dimension && image.height <= max_dimension) {
        return image;
    }

    const float scale = std::min(
        static_cast<float>(max_dimension) / static_cast<float>(image.width),
        static_cast<float>(max_dimension) / static_cast<float>(image.height)
    );
    DecodedTextureImage resized;
    resized.width = std::max(1, static_cast<int>(std::round(static_cast<float>(image.width) * scale)));
    resized.height = std::max(1, static_cast<int>(std::round(static_cast<float>(image.height) * scale)));
    resized.rgba.resize(
        static_cast<std::size_t>(resized.width) *
        static_cast<std::size_t>(resized.height) *
        kChannels
    );

    const float inv_scale_x = static_cast<float>(image.width) / static_cast<float>(resized.width);
    const float inv_scale_y = static_cast<float>(image.height) / static_cast<float>(resized.height);
    for (int y = 0; y < resized.height; ++y) {
        const float source_y = (static_cast<float>(y) + 0.5F) * inv_scale_y - 0.5F;
        for (int x = 0; x < resized.width; ++x) {
            const float source_x = (static_cast<float>(x) + 0.5F) * inv_scale_x - 0.5F;
            const std::size_t destination =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(resized.width)) +
                    static_cast<std::size_t>(x)) *
                kChannels;
            for (int channel = 0; channel < kChannels; ++channel) {
                resized.rgba[destination + static_cast<std::size_t>(channel)] =
                    bilinear_sample_channel(image, source_x, source_y, channel);
            }
        }
    }

    return resized;
}

void cap_material_texture_candidate_for_save(
    const MaterialTextureSource& source,
    std::vector<std::uint8_t>& candidate_bytes,
    MaterialTextureImageCodec& candidate_codec,
    std::vector<std::string>& warnings
) {
    if (candidate_bytes.empty()) {
        return;
    }

    DecodedTextureImage image;
    std::string message;
    const std::string label = source.name.empty()
        ? source.path
        : source.name;
    if (!decode_texture_image_bytes(candidate_codec, candidate_bytes, label, image, message)) {
        return;
    }
    if (image.width <= kMaxStoredMaterialTextureDimension &&
        image.height <= kMaxStoredMaterialTextureDimension) {
        return;
    }

    const DecodedTextureImage resized =
        resize_texture_to_max_dimension(image, kMaxStoredMaterialTextureDimension);
    std::vector<std::uint8_t> capped_bytes;
    if (!encode_jpeg_xl_rgba(resized, true, 100, capped_bytes, message)) {
        warnings.push_back("Could not cap material texture '" + label + "': " + message);
        return;
    }

    warnings.push_back(
        "Capped material texture '" + label + "' from " +
        std::to_string(image.width) + "x" + std::to_string(image.height) +
        " to " + std::to_string(resized.width) + "x" + std::to_string(resized.height) +
        " for .udmap storage."
    );
    candidate_bytes = std::move(capped_bytes);
    candidate_codec = MaterialTextureImageCodec::JpegXl;
}

void choose_material_texture_storage(
    const MaterialTextureSource& source,
    std::vector<std::uint8_t>& candidate_bytes,
    MaterialTextureImageCodec& candidate_codec,
    std::vector<std::string>& warnings
) {
    candidate_bytes = source.bytes;
    candidate_codec = source.codec;
    cap_material_texture_candidate_for_save(source, candidate_bytes, candidate_codec, warnings);
    if (source.storage_mode == MaterialTextureStorageMode::SourceBytes) {
        return;
    }

    DecodedTextureImage image;
    std::string message;
    const std::string label = source.name.empty()
        ? source.path
        : source.name;
    if (!decode_texture_image_bytes(candidate_codec, candidate_bytes, label, image, message)) {
        warnings.push_back("Could not decode material texture for JPEG XL save: " + message);
        return;
    }

    std::vector<std::uint8_t> jxl_bytes;
    const bool lossless = source.storage_mode == MaterialTextureStorageMode::JpegXlLossless;
    if (!encode_jpeg_xl_rgba(image, lossless, source.jxl_quality, jxl_bytes, message)) {
        warnings.push_back("Could not encode material texture as JPEG XL: " + message);
        return;
    }
    candidate_bytes = std::move(jxl_bytes);
    candidate_codec = MaterialTextureImageCodec::JpegXl;
}

std::vector<std::uint8_t> write_material_texture_payload(
    const MaterialTextureSource& source,
    std::vector<std::string>& warnings
) {
    std::vector<std::uint8_t> candidate_bytes;
    MaterialTextureImageCodec candidate_codec = MaterialTextureImageCodec::SdlSurfaceImage;
    choose_material_texture_storage(source, candidate_bytes, candidate_codec, warnings);

    std::vector<std::uint8_t> stored_bytes = candidate_bytes;
    TexturePayloadCompression compression = TexturePayloadCompression::None;
    std::vector<std::uint8_t> compressed_bytes;
    std::string message;
    if (compress_lzma2_xz(candidate_bytes, compressed_bytes, message) &&
        compressed_bytes.size() < candidate_bytes.size()) {
        stored_bytes = std::move(compressed_bytes);
        compression = TexturePayloadCompression::XzLzma2;
    }

    std::vector<std::uint8_t> payload;
    append_u32(payload, 3);
    append_u32(payload, static_cast<std::uint32_t>(candidate_codec));
    append_u32(payload, static_cast<std::uint32_t>(compression));
    append_u32(payload, static_cast<std::uint32_t>(source.name.size()));
    append_u64(payload, static_cast<std::uint64_t>(candidate_bytes.size()));
    append_u64(payload, static_cast<std::uint64_t>(stored_bytes.size()));
    append_u32(payload, crc32_bytes(candidate_bytes));
    payload.insert(payload.end(), source.name.begin(), source.name.end());
    payload.insert(payload.end(), stored_bytes.begin(), stored_bytes.end());
    return payload;
}

bool read_material_texture_payload(
    const std::vector<std::uint8_t>& payload,
    MaterialTexturePayload& texture,
    std::string& message
) {
    std::size_t offset = 0;
    std::uint32_t version = 0;
    if (!read_u32(payload, offset, version)) {
        message = "Material texture chunk is truncated.";
        return false;
    }
    texture.version = version;

    if (version == 1) {
        std::uint32_t name_length = 0;
        std::uint64_t byte_count = 0;
        if (!read_u32(payload, offset, name_length) ||
            !read_u64(payload, offset, byte_count)) {
            message = "Material texture chunk is truncated.";
            return false;
        }
        if (byte_count == 0 || byte_count > kMaxEmbeddedTexturePayload) {
            message = "Material texture chunk size is invalid.";
            return false;
        }
        if (static_cast<std::uint64_t>(payload.size() - offset) !=
            static_cast<std::uint64_t>(name_length) + byte_count) {
            message = "Material texture chunk size is invalid.";
            return false;
        }

        texture.name.assign(
            payload.begin() + static_cast<std::ptrdiff_t>(offset),
            payload.begin() + static_cast<std::ptrdiff_t>(offset + name_length)
        );
        offset += name_length;
        texture.bytes.assign(
            payload.begin() + static_cast<std::ptrdiff_t>(offset),
            payload.end()
        );
        texture.codec = MaterialTextureImageCodec::SdlSurfaceImage;
        return true;
    }

    if (version != 2 && version != 3) {
        message = "Unsupported material texture chunk version.";
        return false;
    }

    std::uint32_t image_codec = 0;
    std::uint32_t compression_codec = 0;
    std::uint32_t name_length = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t stored_size = 0;
    std::uint32_t raw_crc32 = 0;
    if (!read_u32(payload, offset, image_codec) ||
        !read_u32(payload, offset, compression_codec) ||
        !read_u32(payload, offset, name_length) ||
        !read_u64(payload, offset, uncompressed_size) ||
        !read_u64(payload, offset, stored_size) ||
        !read_u32(payload, offset, raw_crc32)) {
        message = "Material texture chunk is truncated.";
        return false;
    }
    if (!valid_material_texture_image_codec(image_codec)) {
        message = "Material texture image codec is unsupported.";
        return false;
    }
    if (!valid_texture_payload_compression(compression_codec)) {
        message = "Material texture compression codec is unsupported.";
        return false;
    }
    if (uncompressed_size == 0 || uncompressed_size > kMaxEmbeddedTexturePayload ||
        stored_size == 0 || stored_size > kMaxEmbeddedTexturePayload) {
        message = "Material texture chunk size is invalid.";
        return false;
    }
    if (static_cast<std::uint64_t>(payload.size() - offset) !=
        static_cast<std::uint64_t>(name_length) + stored_size) {
        message = "Material texture chunk size is invalid.";
        return false;
    }

    texture.name.assign(
        payload.begin() + static_cast<std::ptrdiff_t>(offset),
        payload.begin() + static_cast<std::ptrdiff_t>(offset + name_length)
    );
    offset += name_length;
    std::vector<std::uint8_t> stored_bytes(
        payload.begin() + static_cast<std::ptrdiff_t>(offset),
        payload.end()
    );

    if (compression_codec == static_cast<std::uint32_t>(TexturePayloadCompression::None)) {
        if (stored_size != uncompressed_size) {
            message = "Material texture raw size is invalid.";
            return false;
        }
        texture.bytes = std::move(stored_bytes);
    } else if (!decompress_lzma2_xz(stored_bytes, uncompressed_size, texture.bytes, message)) {
        return false;
    }
    if (crc32_bytes(texture.bytes) != raw_crc32) {
        message = "Material texture chunk CRC mismatch.";
        return false;
    }
    texture.codec = static_cast<MaterialTextureImageCodec>(image_codec);
    return true;
}

bool read_texture_source_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& message
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        message = "Could not open material texture for embedding: " + path.string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        message = "Material texture file is empty: " + path.string();
        return false;
    }
    return true;
}

std::filesystem::path resolved_material_texture_path(
    const std::filesystem::path& map_path,
    const std::filesystem::path& texture_path
) {
    if (texture_path.is_absolute()) {
        return texture_path;
    }

    const std::filesystem::path map_relative = map_path.parent_path() / texture_path;
    std::error_code error;
    if (std::filesystem::exists(map_relative, error)) {
        return map_relative;
    }
    return std::filesystem::current_path() / texture_path;
}

PreparedMaterialChunks prepare_material_chunks_for_save(
    MaterialLibrary material_library,
    const std::filesystem::path& map_path
) {
    PreparedMaterialChunks prepared;
    prepared.library = normalized_material_library(std::move(material_library));

    for (int i = 0; i < kMaterialCount; ++i) {
        MaterialSlot& slot = prepared.library.slots[static_cast<std::size_t>(i)];
        for (int channel_index = 0; channel_index < kMaterialTextureChannelCount; ++channel_index) {
            const auto channel = static_cast<MaterialTextureChannel>(channel_index);
            MaterialTextureSource& source = material_texture_source(slot, channel);
            if (source.bytes.empty() && !source.path.empty()) {
                std::vector<std::uint8_t> bytes;
                std::string message;
                const std::filesystem::path texture_path =
                    resolved_material_texture_path(map_path, source.path);
                if (read_texture_source_file(texture_path, bytes, message)) {
                    source.bytes = std::move(bytes);
                    source.codec = material_texture_codec_for_path(texture_path);
                    if (source.name.empty()) {
                        source.name = texture_path.filename().generic_string();
                    }
                } else {
                    prepared.warnings.push_back(message);
                }
            }

            if (!source.bytes.empty()) {
                if (source.name.empty()) {
                    const std::filesystem::path texture_path = source.path;
                    source.name = texture_path.filename().empty()
                        ? "material_" + std::to_string(i) + "_" + material_texture_channel_short_label(channel)
                        : texture_path.filename().generic_string();
                }
                prepared.texture_chunks.push_back(ChunkRecord{
                    kChunkMaterialTexture,
                    kChunkFlagOptional,
                    material_texture_chunk_id(i, channel),
                    write_material_texture_payload(source, prepared.warnings)
                });
            }
        }
    }

    return prepared;
}

std::string write_lighting_payload(const WorldLighting world_lighting) {
    const WorldLighting lighting = normalized_lighting(world_lighting);
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << "lighting 1\n";
    output << "sun "
           << (lighting.sun_enabled ? 1 : 0) << ' '
           << lighting.sun_direction.x << ' '
           << lighting.sun_direction.y << ' '
           << lighting.sun_direction.z << ' '
           << lighting.sun_color.x << ' '
           << lighting.sun_color.y << ' '
           << lighting.sun_color.z << ' '
           << lighting.sun_intensity << '\n';
    return output.str();
}

bool script_store_empty(const ScriptStore& scripts) {
    return !scripts.has_global_script && scripts.entity_scripts.empty() && scripts.sector_scripts.empty();
}

std::string write_scripts_payload(const ScriptStore& scripts) {
    std::ostringstream output;
    write_script_store_payload(output, scripts);
    return output.str();
}

bool read_lighting_payload(std::istream& input, WorldLighting& world_lighting, std::string& message) {
    if (!read_expected(input, "lighting", message)) {
        return false;
    }
    int version = 0;
    if (!(input >> version) || version != 1) {
        message = "Unsupported lighting chunk version.";
        return false;
    }
    if (!read_expected(input, "sun", message)) {
        return false;
    }
    int enabled = 0;
    WorldLighting lighting;
    if (!(input >> enabled)) {
        message = "Expected sun enabled flag.";
        return false;
    }
    if (enabled != 0 && enabled != 1) {
        message = "Sun enabled flag must be 0 or 1.";
        return false;
    }
    lighting.sun_enabled = enabled != 0;
    if (!read_float(input, lighting.sun_direction.x, message) ||
        !read_float(input, lighting.sun_direction.y, message) ||
        !read_float(input, lighting.sun_direction.z, message) ||
        !read_float(input, lighting.sun_color.x, message) ||
        !read_float(input, lighting.sun_color.y, message) ||
        !read_float(input, lighting.sun_color.z, message) ||
        !read_float(input, lighting.sun_intensity, message)) {
        return false;
    }
    if (lighting.sun_intensity < 0.0F) {
        message = "Sun intensity must be non-negative.";
        return false;
    }
    if (lighting.sun_color.x < 0.0F || lighting.sun_color.y < 0.0F || lighting.sun_color.z < 0.0F) {
        message = "Sun color cannot be negative.";
        return false;
    }
    world_lighting = normalized_lighting(lighting);

    std::string trailing;
    if (input >> trailing) {
        message = "Unexpected trailing lighting token: " + trailing;
        return false;
    }
    return true;
}

bool validate_sector_script_ids(
    const ScriptStore& scripts,
    const std::set<std::uint64_t>& sector_ids,
    std::string& message
) {
    for (const auto& [sector_id, program] : scripts.sector_scripts) {
        (void)program;
        if (!sector_ids.contains(sector_id)) {
            message = "Sector script references missing sector id.";
            return false;
        }
    }
    return true;
}

bool read_entities_payload(
    std::istream& input,
    PlayerSpawn& player_spawn,
    std::vector<PointLight>& point_lights,
    std::string& message
) {
    if (!read_expected(input, "entities", message)) {
        return false;
    }
    int version = 0;
    if (!(input >> version) || version < 1 || version > 2) {
        message = "Unsupported entities chunk version.";
        return false;
    }
    if (!read_expected(input, "player_spawn", message)) {
        return false;
    }

    std::string spawn_token;
    if (!(input >> spawn_token)) {
        message = "Expected player spawn id or 'unset'.";
        return false;
    }
    if (spawn_token == "unset") {
        player_spawn = PlayerSpawn{};
    } else {
        std::istringstream id_stream(spawn_token);
        if (!(id_stream >> player_spawn.id) || !id_stream.eof()) {
            message = "Expected player spawn id.";
            return false;
        }
        if (player_spawn.id == 0) {
            message = "Player spawn id must be non-zero.";
            return false;
        }
        if (!read_float(input, player_spawn.position.x, message) ||
            !read_float(input, player_spawn.position.y, message) ||
            !read_float(input, player_spawn.position.z, message) ||
            !read_float(input, player_spawn.yaw, message)) {
            return false;
        }
        player_spawn.set = true;
    }

    if (!read_expected(input, "point_lights", message)) {
        return false;
    }
    std::size_t light_count = 0;
    if (!read_count(input, light_count, message)) {
        return false;
    }
    point_lights.clear();
    point_lights.reserve(light_count);
    std::set<std::uint64_t> light_ids;
    for (std::size_t light_index = 0; light_index < light_count; ++light_index) {
        if (!read_expected(input, "point_light", message)) {
            return false;
        }
        PointLight light;
        if (!(input >> light.id)) {
            message = "Expected point light id.";
            return false;
        }
        if (light.id == 0 || !light_ids.insert(light.id).second) {
            message = "Point light id must be non-zero and unique.";
            return false;
        }
        if (!read_float(input, light.position.x, message) ||
            !read_float(input, light.position.y, message) ||
            !read_float(input, light.position.z, message) ||
            !read_float(input, light.color.x, message) ||
            !read_float(input, light.color.y, message) ||
            !read_float(input, light.color.z, message) ||
            !read_float(input, light.radius, message) ||
            !read_float(input, light.intensity, message)) {
            return false;
        }
        if (version >= 2 && !read_float(input, light.shadow_bias, message)) {
            return false;
        }
        if (light.radius <= 0.0F) {
            message = "Point light radius must be greater than zero.";
            return false;
        }
        if (light.intensity < 0.0F) {
            message = "Point light intensity must be non-negative.";
            return false;
        }
        if (light.shadow_bias < 0.0F) {
            message = "Point light shadow bias must be non-negative.";
            return false;
        }
        point_lights.push_back(light);
    }

    std::string trailing;
    if (input >> trailing) {
        message = "Unexpected trailing entity token: " + trailing;
        return false;
    }
    return true;
}

bool read_materials_payload(std::istream& input, MaterialLibrary& material_library, std::string& message) {
    if (!read_expected(input, "materials", message)) {
        return false;
    }
    int version = 0;
    if (!(input >> version) || (version != 1 && version != 2 && version != 3 && version != 4)) {
        message = "Unsupported materials chunk version.";
        return false;
    }
    if (!read_expected(input, "count", message)) {
        return false;
    }
    std::size_t count = 0;
    if (!read_count(input, count, message)) {
        return false;
    }
    if (count != kMaterialCount) {
        message = "Material palette count is unsupported.";
        return false;
    }
    if (version == 1) {
        material_library = default_material_library();
        return true;
    }
    MaterialLibrary parsed = default_material_library();
    std::vector<bool> seen(kMaterialCount, false);
    for (std::size_t i = 0; i < count; ++i) {
        if (!read_expected(input, "slot", message)) {
            return false;
        }
        int index = -1;
        MaterialSlot slot;
        if (!(input >> index) ||
            !read_float(input, slot.base_color.r, message) ||
            !read_float(input, slot.base_color.g, message) ||
            !read_float(input, slot.base_color.b, message) ||
            !read_float(input, slot.roughness, message) ||
            !read_float(input, slot.metallic, message) ||
            !read_float(input, slot.specular, message) ||
            !read_float(input, slot.uv_scale, message)) {
            message = "Malformed material slot.";
            return false;
        }
        if (version < 4) {
            MaterialTextureSource& albedo = material_texture_source(slot, MaterialTextureChannel::Albedo);
            if (!(input >> std::quoted(albedo.path))) {
                message = "Malformed material slot.";
                return false;
            }
            if (version >= 3) {
                std::uint32_t image_codec = 0;
                std::uint32_t storage_mode = 0;
                if (!(input >> image_codec >> storage_mode >> albedo.jxl_quality) ||
                    !valid_material_texture_image_codec(image_codec) ||
                    !valid_material_texture_storage_mode(storage_mode)) {
                    message = "Malformed material texture storage fields.";
                    return false;
                }
                albedo.codec = static_cast<MaterialTextureImageCodec>(image_codec);
                albedo.storage_mode = static_cast<MaterialTextureStorageMode>(storage_mode);
            } else {
                albedo.codec = material_texture_codec_for_path(albedo.path);
                albedo.storage_mode = MaterialTextureStorageMode::SourceBytes;
                albedo.jxl_quality = 80;
            }
        }
        if (index < 0 || index >= kMaterialCount || seen[static_cast<std::size_t>(index)]) {
            message = "Material slot index is invalid or duplicated.";
            return false;
        }
        seen[static_cast<std::size_t>(index)] = true;
        parsed.slots[static_cast<std::size_t>(index)] = std::move(slot);
    }
    if (version >= 4) {
        for (std::size_t texture_index = 0; texture_index < count * kMaterialTextureChannelCount; ++texture_index) {
            if (!read_expected(input, "texture", message)) {
                return false;
            }
            int slot_index = -1;
            std::uint32_t channel_value = 0;
            std::uint32_t image_codec = 0;
            std::uint32_t storage_mode = 0;
            MaterialTextureSource source;
            if (!(input >> slot_index >> channel_value >> std::quoted(source.path) >> image_codec >> storage_mode >> source.jxl_quality) ||
                !valid_material_texture_image_codec(image_codec) ||
                !valid_material_texture_storage_mode(storage_mode)) {
                message = "Malformed material texture storage fields.";
                return false;
            }
            if (slot_index < 0 || slot_index >= kMaterialCount ||
                channel_value >= static_cast<std::uint32_t>(kMaterialTextureChannelCount)) {
                message = "Material texture slot/channel index is invalid.";
                return false;
            }
            source.codec = static_cast<MaterialTextureImageCodec>(image_codec);
            source.storage_mode = static_cast<MaterialTextureStorageMode>(storage_mode);
            material_texture_source(
                parsed.slots[static_cast<std::size_t>(slot_index)],
                static_cast<MaterialTextureChannel>(channel_value)
            ) = std::move(source);
        }
    }
    for (const bool slot_seen : seen) {
        if (!slot_seen) {
            message = "Material slot table is incomplete.";
            return false;
        }
    }
    material_library = normalized_material_library(std::move(parsed));
    std::string trailing;
    if (input >> trailing) {
        message = "Unexpected trailing material token: " + trailing;
        return false;
    }
    return true;
}

std::vector<ChunkRecord> build_chunk_records(
    std::vector<SectorPlane> sectors,
    PlayerSpawn player_spawn,
    std::vector<PointLight> point_lights,
    const WorldLighting world_lighting,
    const MaterialLibrary& material_library,
    const ScriptStore& scripts,
    const std::filesystem::path& path,
    std::vector<std::string>& warnings
) {
    const std::uint64_t next_id = assign_missing_stable_ids(sectors, player_spawn, point_lights);
    PreparedMaterialChunks material_chunks = prepare_material_chunks_for_save(material_library, path);
    warnings.insert(warnings.end(), material_chunks.warnings.begin(), material_chunks.warnings.end());

    std::vector<ChunkRecord> chunks;
    chunks.push_back(ChunkRecord{kChunkMeta, 0, 0, string_payload(write_meta_payload(next_id))});
    chunks.push_back(ChunkRecord{kChunkMaterials, kChunkFlagOptional, 0, string_payload(write_materials_payload(material_chunks.library))});
    chunks.push_back(ChunkRecord{kChunkLighting, kChunkFlagOptional, 0, string_payload(write_lighting_payload(world_lighting))});
    chunks.push_back(ChunkRecord{kChunkEntities, 0, 0, string_payload(write_entities_payload(player_spawn, point_lights))});
    if (!script_store_empty(scripts)) {
        chunks.push_back(ChunkRecord{kChunkScripts, kChunkFlagOptional, 0, string_payload(write_scripts_payload(scripts))});
    }
    for (const SectorPlane& sector : sectors) {
        chunks.push_back(ChunkRecord{kChunkSector, 0, sector.id, string_payload(write_sector_payload(sector))});
    }
    chunks.insert(
        chunks.end(),
        std::make_move_iterator(material_chunks.texture_chunks.begin()),
        std::make_move_iterator(material_chunks.texture_chunks.end())
    );
    return chunks;
}

bool write_chunk_file(const std::filesystem::path& path, std::vector<ChunkRecord> chunks, std::string& message) {
    const std::uint64_t directory_offset = kChunkHeaderSize;
    const std::uint64_t payload_offset =
        directory_offset + (static_cast<std::uint64_t>(chunks.size()) * kChunkDirectoryEntrySize);
    std::uint64_t current_offset = payload_offset;

    std::vector<ChunkDirectoryEntry> directory;
    directory.reserve(chunks.size());
    for (const ChunkRecord& chunk : chunks) {
        directory.push_back(ChunkDirectoryEntry{
            chunk.type,
            chunk.flags,
            chunk.id,
            current_offset,
            static_cast<std::uint64_t>(chunk.data.size()),
            checksum_bytes(chunk.data),
        });
        current_offset += chunk.data.size();
    }

    std::vector<std::uint8_t> file;
    file.reserve(static_cast<std::size_t>(current_offset));
    file.insert(file.end(), kChunkedMagic.begin(), kChunkedMagic.end());
    append_u32(file, kChunkedVersion);
    append_u32(file, static_cast<std::uint32_t>(chunks.size()));
    append_u64(file, directory_offset);
    append_u64(file, payload_offset);

    for (const ChunkDirectoryEntry& entry : directory) {
        append_u32(file, entry.type);
        append_u32(file, entry.flags);
        append_u64(file, entry.id);
        append_u64(file, entry.offset);
        append_u64(file, entry.size);
        append_u32(file, entry.checksum);
        append_u32(file, 0);
        append_u64(file, 0);
        append_u64(file, 0);
    }
    for (const ChunkRecord& chunk : chunks) {
        file.insert(file.end(), chunk.data.begin(), chunk.data.end());
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            message = "Could not open temporary map file for writing: " + temp_path.string();
            return false;
        }
        output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!output) {
            message = "Failed while writing temporary map file: " + temp_path.string();
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temp_path, path, error);
    }
    if (error) {
        message = "Could not replace map file: " + error.message();
        return false;
    }
    return true;
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        message = "Could not open map file for reading: " + path.string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        message = "Map file is empty.";
        return false;
    }
    return true;
}

bool has_chunked_magic(const std::vector<std::uint8_t>& bytes) {
    return bytes.size() >= kChunkedMagic.size() &&
        std::equal(kChunkedMagic.begin(), kChunkedMagic.end(), bytes.begin());
}

bool has_legacy_magic(const std::vector<std::uint8_t>& bytes) {
    const std::string magic = kMapMagic;
    return bytes.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), bytes.begin());
}

bool parse_chunk_directory(
    const std::vector<std::uint8_t>& bytes,
    std::vector<ChunkDirectoryEntry>& directory,
    std::string& message
) {
    if (bytes.size() < kChunkHeaderSize || !has_chunked_magic(bytes)) {
        message = "Map file is not a chunked v3 udmap.";
        return false;
    }

    std::size_t offset = kChunkedMagic.size();
    std::uint32_t version = 0;
    std::uint32_t chunk_count = 0;
    std::uint64_t directory_offset = 0;
    std::uint64_t payload_offset = 0;
    if (!read_u32(bytes, offset, version) ||
        !read_u32(bytes, offset, chunk_count) ||
        !read_u64(bytes, offset, directory_offset) ||
        !read_u64(bytes, offset, payload_offset)) {
        message = "Chunked map header is truncated.";
        return false;
    }
    if (version != kChunkedVersion) {
        message = "Unsupported chunked map version.";
        return false;
    }
    if (directory_offset != kChunkHeaderSize) {
        message = "Chunked map directory offset is unsupported.";
        return false;
    }
    const std::uint64_t directory_size =
        static_cast<std::uint64_t>(chunk_count) * kChunkDirectoryEntrySize;
    if (directory_offset + directory_size > bytes.size() || payload_offset < directory_offset + directory_size) {
        message = "Chunked map directory is out of bounds.";
        return false;
    }

    directory.clear();
    directory.reserve(chunk_count);
    offset = static_cast<std::size_t>(directory_offset);
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        ChunkDirectoryEntry entry;
        std::uint32_t reserved = 0;
        std::uint64_t reserved64 = 0;
        if (!read_u32(bytes, offset, entry.type) ||
            !read_u32(bytes, offset, entry.flags) ||
            !read_u64(bytes, offset, entry.id) ||
            !read_u64(bytes, offset, entry.offset) ||
            !read_u64(bytes, offset, entry.size) ||
            !read_u32(bytes, offset, entry.checksum) ||
            !read_u32(bytes, offset, reserved) ||
            !read_u64(bytes, offset, reserved64) ||
            !read_u64(bytes, offset, reserved64)) {
            message = "Chunked map directory entry is truncated.";
            return false;
        }
        if (entry.offset < payload_offset || entry.offset + entry.size > bytes.size()) {
            message = "Chunk payload is out of bounds.";
            return false;
        }
        directory.push_back(entry);
    }
    return true;
}

std::vector<std::uint8_t> chunk_payload_bytes(
    const std::vector<std::uint8_t>& file,
    const ChunkDirectoryEntry& entry
) {
    const auto begin = file.begin() + static_cast<std::ptrdiff_t>(entry.offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(entry.size);
    return std::vector<std::uint8_t>(begin, end);
}

LoadMapResult load_chunked_map_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::string message;
    std::vector<ChunkDirectoryEntry> directory;
    if (!parse_chunk_directory(bytes, directory, message)) {
        return load_error(message);
    }

    bool saw_meta = false;
    bool saw_entities = false;
    PlayerSpawn player_spawn;
    std::vector<PointLight> point_lights;
    WorldLighting world_lighting;
    MaterialLibrary material_library = default_material_library();
    std::array<std::array<MaterialTexturePayload, kMaterialTextureChannelCount>, kMaterialCount> material_textures{};
    std::array<std::array<bool, kMaterialTextureChannelCount>, kMaterialCount> material_texture_seen{};
    ScriptStore scripts;
    std::vector<SectorPlane> sectors;
    std::set<std::uint64_t> sector_ids;

    for (const ChunkDirectoryEntry& entry : directory) {
        std::vector<std::uint8_t> payload = chunk_payload_bytes(bytes, entry);
        if (checksum_bytes(payload) != entry.checksum) {
            return load_error("Chunk checksum mismatch.");
        }

        if (entry.type == kChunkMeta) {
            saw_meta = true;
            continue;
        }
        if (entry.type == kChunkMaterials) {
            std::istringstream input(payload_string(payload));
            if (!read_materials_payload(input, material_library, message)) {
                return load_error(message);
            }
            continue;
        }
        if (entry.type == kChunkMaterialTexture) {
            if ((entry.flags & kChunkFlagOptional) == 0U) {
                return load_error("Material texture chunk must be optional.");
            }
            MaterialTexturePayload decoded_texture;
            if (!read_material_texture_payload(payload, decoded_texture, message)) {
                return load_error(message);
            }
            std::size_t material_id = 0;
            MaterialTextureChannel channel = MaterialTextureChannel::Albedo;
            if (!decode_material_texture_chunk_id(entry.id, decoded_texture.version, material_id, channel)) {
                return load_error("Material texture chunk id is out of range.");
            }
            const std::size_t channel_index =
                static_cast<std::size_t>(material_texture_channel_index(channel));
            if (material_texture_seen[material_id][channel_index]) {
                return load_error("Duplicate material texture chunk.");
            }
            material_textures[material_id][channel_index] = std::move(decoded_texture);
            material_texture_seen[material_id][channel_index] = true;
            continue;
        }
        if (entry.type == kChunkLighting && (entry.flags & kChunkFlagOptional) != 0U) {
            std::istringstream input(payload_string(payload));
            if (!read_lighting_payload(input, world_lighting, message)) {
                return load_error(message);
            }
            continue;
        }
        if (entry.type == kChunkEntities) {
            if (saw_entities) {
                return load_error("Duplicate entity chunk.");
            }
            saw_entities = true;
            std::istringstream input(payload_string(payload));
            if (!read_entities_payload(input, player_spawn, point_lights, message)) {
                return load_error(message);
            }
            continue;
        }
        if (entry.type == kChunkScripts && (entry.flags & kChunkFlagOptional) != 0U) {
            std::istringstream input(payload_string(payload));
            if (!read_script_store_payload(input, scripts, message)) {
                return load_error(message);
            }
            continue;
        }
        if (entry.type == kChunkSector) {
            if (entry.id == 0 || !sector_ids.insert(entry.id).second) {
                return load_error("Duplicate or zero sector chunk id.");
            }
            std::istringstream input(payload_string(payload));
            SectorPlane sector;
            sector.id = entry.id;
            if (!read_sector_record(input, sector, message)) {
                return load_error(message);
            }
            std::string trailing;
            if (input >> trailing) {
                return load_error("Unexpected trailing sector token: " + trailing);
            }
            sectors.push_back(std::move(sector));
            continue;
        }
        if (entry.type == kChunkEditorState && (entry.flags & kChunkFlagOptional) != 0U) {
            continue;
        }
        if ((entry.flags & kChunkFlagOptional) != 0U) {
            continue;
        }
        return load_error("Unsupported required chunk in map file.");
    }

    if (!saw_meta) {
        return load_error("Chunked map is missing META chunk.");
    }
    if (!saw_entities) {
        return load_error("Chunked map is missing ENTY chunk.");
    }
    if (!ids_are_unique(sectors, message)) {
        return load_error(message);
    }
    if (!validate_sector_script_ids(scripts, sector_ids, message)) {
        return load_error(message);
    }

    for (int i = 0; i < kMaterialCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        MaterialSlot& slot = material_library.slots[index];
        for (int channel_index = 0; channel_index < kMaterialTextureChannelCount; ++channel_index) {
            if (!material_texture_seen[index][static_cast<std::size_t>(channel_index)]) {
                continue;
            }
            const auto channel = static_cast<MaterialTextureChannel>(channel_index);
            MaterialTextureSource& source = material_texture_source(slot, channel);
            MaterialTexturePayload& payload = material_textures[index][static_cast<std::size_t>(channel_index)];
            source.name = std::move(payload.name);
            source.bytes = std::move(payload.bytes);
            source.codec = payload.codec;
        }
    }

    LoadMapResult result =
        rebuild_loaded_sectors(
            std::move(sectors),
            player_spawn,
            std::move(point_lights),
            world_lighting,
            std::move(material_library),
            std::move(scripts)
        );
    if (result.ok) {
        result.message = "Loaded chunked map.";
    }
    return result;
}

} // namespace

SaveMapResult save_map_file(const std::vector<SectorPlane>& sectors, const std::filesystem::path& path) {
    return save_map_file(sectors, PlayerSpawn{}, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::filesystem::path& path
) {
    return save_map_file(sectors, player_spawn, {}, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const std::filesystem::path& path
) {
    return save_map_file(sectors, player_spawn, point_lights, WorldLighting{}, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const std::filesystem::path& path
) {
    return save_map_file(sectors, player_spawn, point_lights, world_lighting, ScriptStore{}, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const MaterialLibrary& material_library,
    const std::filesystem::path& path
) {
    return save_map_file(sectors, player_spawn, point_lights, world_lighting, material_library, ScriptStore{}, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const ScriptStore& scripts,
    const std::filesystem::path& path
) {
    return save_map_file(sectors, player_spawn, point_lights, world_lighting, default_material_library(), scripts, path);
}

SaveMapResult save_map_file(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const MaterialLibrary& material_library,
    const ScriptStore& scripts,
    const std::filesystem::path& path
) {
    std::string error_message;
    std::vector<std::string> warnings;
    std::vector<ChunkRecord> chunks =
        build_chunk_records(sectors, player_spawn, point_lights, world_lighting, material_library, scripts, path, warnings);
    if (!write_chunk_file(path, std::move(chunks), error_message)) {
        return save_error(error_message);
    }

    std::ostringstream message;
    message << "Saved chunked map with " << sectors.size() << " sectors to " << path.string();
    for (const std::string& warning : warnings) {
        message << "\nWarning: " << warning;
    }
    return SaveMapResult{true, message.str()};
}

SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
) {
    return save_map_file_dirty(sectors, player_spawn, point_lights, WorldLighting{}, dirty_state, path);
}

SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
) {
    return save_map_file_dirty(sectors, player_spawn, point_lights, world_lighting, ScriptStore{}, dirty_state, path);
}

SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const MaterialLibrary& material_library,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
) {
    return save_map_file_dirty(sectors, player_spawn, point_lights, world_lighting, material_library, ScriptStore{}, dirty_state, path);
}

SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const ScriptStore& scripts,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
) {
    return save_map_file_dirty(
        sectors,
        player_spawn,
        point_lights,
        world_lighting,
        default_material_library(),
        scripts,
        dirty_state,
        path
    );
}

SaveMapResult save_map_file_dirty(
    const std::vector<SectorPlane>& sectors,
    const PlayerSpawn player_spawn,
    const std::vector<PointLight>& point_lights,
    const WorldLighting world_lighting,
    const MaterialLibrary& material_library,
    const ScriptStore& scripts,
    const MapDirtyState& dirty_state,
    const std::filesystem::path& path
) {
    if (dirty_state.topology) {
        return save_map_file(sectors, player_spawn, point_lights, world_lighting, material_library, scripts, path);
    }

    std::vector<SectorPlane> sectors_with_ids = sectors;
    PlayerSpawn spawn_with_id = player_spawn;
    std::vector<PointLight> lights_with_ids = point_lights;
    const std::uint64_t next_id = assign_missing_stable_ids(sectors_with_ids, spawn_with_id, lights_with_ids);

    std::vector<std::uint8_t> existing_bytes;
    std::string read_message;
    std::vector<ChunkDirectoryEntry> existing_directory;
    const bool can_reuse_existing =
        read_file_bytes(path, existing_bytes, read_message) &&
        has_chunked_magic(existing_bytes) &&
        parse_chunk_directory(existing_bytes, existing_directory, read_message);

    if (!can_reuse_existing) {
        return save_map_file(sectors_with_ids, spawn_with_id, lights_with_ids, world_lighting, material_library, scripts, path);
    }

    auto existing_chunk = [&existing_bytes, &existing_directory](const std::uint32_t type, const std::uint64_t id, ChunkRecord& out) {
        const auto found = std::find_if(existing_directory.begin(), existing_directory.end(), [type, id](const ChunkDirectoryEntry& entry) {
            return entry.type == type && entry.id == id;
        });
        if (found == existing_directory.end()) {
            return false;
        }
        std::vector<std::uint8_t> payload = chunk_payload_bytes(existing_bytes, *found);
        if (checksum_bytes(payload) != found->checksum) {
            return false;
        }
        out = ChunkRecord{found->type, found->flags, found->id, std::move(payload)};
        return true;
    };

    std::vector<std::string> warnings;
    PreparedMaterialChunks material_chunks = prepare_material_chunks_for_save(material_library, path);
    warnings.insert(warnings.end(), material_chunks.warnings.begin(), material_chunks.warnings.end());

    std::vector<ChunkRecord> chunks;
    chunks.push_back(ChunkRecord{kChunkMeta, 0, 0, string_payload(write_meta_payload(next_id))});

    ChunkRecord reused;
    chunks.push_back(ChunkRecord{kChunkMaterials, kChunkFlagOptional, 0, string_payload(write_materials_payload(material_chunks.library))});

    if (!dirty_state.metadata && existing_chunk(kChunkLighting, 0, reused)) {
        chunks.push_back(std::move(reused));
    } else {
        chunks.push_back(ChunkRecord{kChunkLighting, kChunkFlagOptional, 0, string_payload(write_lighting_payload(world_lighting))});
    }

    if (!dirty_state.entities && existing_chunk(kChunkEntities, 0, reused)) {
        chunks.push_back(std::move(reused));
    } else {
        chunks.push_back(ChunkRecord{kChunkEntities, 0, 0, string_payload(write_entities_payload(spawn_with_id, lights_with_ids))});
    }

    if (dirty_state.scripts || !existing_chunk(kChunkScripts, 0, reused)) {
        if (!script_store_empty(scripts)) {
            chunks.push_back(ChunkRecord{kChunkScripts, kChunkFlagOptional, 0, string_payload(write_scripts_payload(scripts))});
        }
    } else {
        chunks.push_back(std::move(reused));
    }

    for (const SectorPlane& sector : sectors_with_ids) {
        if (!dirty_state.sector_ids.contains(sector.id) && existing_chunk(kChunkSector, sector.id, reused)) {
            chunks.push_back(std::move(reused));
        } else {
            chunks.push_back(ChunkRecord{kChunkSector, 0, sector.id, string_payload(write_sector_payload(sector))});
        }
    }

    chunks.insert(
        chunks.end(),
        std::make_move_iterator(material_chunks.texture_chunks.begin()),
        std::make_move_iterator(material_chunks.texture_chunks.end())
    );

    std::string write_message;
    if (!write_chunk_file(path, std::move(chunks), write_message)) {
        return save_error(write_message);
    }

    std::ostringstream message;
    message << "Saved dirty chunked map with " << sectors_with_ids.size() << " sectors to " << path.string();
    for (const std::string& warning : warnings) {
        message << "\nWarning: " << warning;
    }
    return SaveMapResult{true, message.str()};
}

LoadMapResult load_legacy_map_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return load_error("Could not open map file for reading: " + path.string());
    }

    std::string message;
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version)) {
        return load_error("Map file is missing magic/version header.");
    }

    if (magic != kMapMagic) {
        return load_error("Unsupported map magic: " + magic);
    }

    if (version < 1 || version > kMapVersion) {
        return load_error("Unsupported map version.");
    }

    PlayerSpawn player_spawn;
    std::vector<PointLight> point_lights;
    std::string next_token;
    if (!(input >> next_token)) {
        return load_error("Expected 'player_spawn' or 'sectors'.");
    }

    if (next_token == "player_spawn") {
        std::string spawn_token;
        if (!(input >> spawn_token)) {
            return load_error("Expected player spawn coordinates or 'unset'.");
        }
        if (spawn_token == "unset") {
            player_spawn = PlayerSpawn{};
        } else {
            if (!parse_float_token(spawn_token, player_spawn.position.x, message) ||
                !read_float(input, player_spawn.position.y, message) ||
                !read_float(input, player_spawn.position.z, message) ||
                !read_float(input, player_spawn.yaw, message)) {
                return load_error(message);
            }
            player_spawn.set = true;
        }
        if (!(input >> next_token)) {
            return load_error("Expected 'point_lights' or 'sectors' after player_spawn.");
        }
    } else if (next_token != "sectors") {
        return load_error("Expected 'player_spawn' or 'sectors' but found '" + next_token + "'.");
    }

    if (next_token == "point_lights") {
        std::size_t light_count = 0;
        if (!read_count(input, light_count, message)) {
            return load_error(message);
        }
        point_lights.reserve(light_count);
        for (std::size_t light_index = 0; light_index < light_count; ++light_index) {
            if (!read_expected(input, "point_light", message)) {
                return load_error(message);
            }
            PointLight light;
            if (!read_float(input, light.position.x, message) ||
                !read_float(input, light.position.y, message) ||
                !read_float(input, light.position.z, message) ||
                !read_float(input, light.color.x, message) ||
                !read_float(input, light.color.y, message) ||
                !read_float(input, light.color.z, message) ||
                !read_float(input, light.radius, message) ||
                !read_float(input, light.intensity, message)) {
                return load_error(message);
            }
            if (light.radius <= 0.0F) {
                return load_error("Point light radius must be greater than zero.");
            }
            if (light.intensity < 0.0F) {
                return load_error("Point light intensity must be non-negative.");
            }
            point_lights.push_back(light);
        }
        if (!read_expected(input, "sectors", message)) {
            return load_error(message);
        }
    }

    std::size_t sector_count = 0;
    if (!read_count(input, sector_count, message)) {
        return load_error(message);
    }

    std::vector<SectorPlane> sectors;
    sectors.reserve(sector_count);
    for (std::size_t sector_index = 0; sector_index < sector_count; ++sector_index) {
        SectorPlane sector;
        if (!read_sector_record(input, sector, message)) {
            return load_error(message);
        }

        sectors.push_back(std::move(sector));
    }

    std::string trailing;
    if (input >> trailing) {
        return load_error("Unexpected trailing token: " + trailing);
    }

    std::uint64_t next_id = 1;
    std::set<std::uint64_t> used;
    for (SectorPlane& sector : sectors) {
        assign_id(sector.id, used, next_id);
    }
    if (player_spawn.set) {
        assign_id(player_spawn.id, used, next_id);
    }
    for (PointLight& light : point_lights) {
        assign_id(light.id, used, next_id);
    }

    LoadMapResult result = rebuild_loaded_sectors(std::move(sectors), player_spawn, std::move(point_lights));
    if (result.ok) {
        result.message = "Loaded legacy text map.";
    }
    return result;
}

LoadMapResult load_map_file(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes;
    std::string message;
    if (!read_file_bytes(path, bytes, message)) {
        return load_error(message);
    }
    if (has_chunked_magic(bytes)) {
        return load_chunked_map_file(path, bytes);
    }
    if (has_legacy_magic(bytes)) {
        return load_legacy_map_file(path);
    }
    return load_error("Unsupported map magic.");
}

} // namespace undecedent
