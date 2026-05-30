#include "undecedent/map_io.hpp"

#include "undecedent/displacement.hpp"
#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace undecedent {
namespace {

constexpr const char* kMapMagic = "UNDECEDENT_MAP";
constexpr int kMapVersion = 2;

SaveMapResult save_error(const std::string& message) {
    return SaveMapResult{false, message};
}

LoadMapResult load_error(const std::string& message) {
    return LoadMapResult{false, message, {}, {}, {}};
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
    std::vector<PointLight> point_lights
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
    return LoadMapResult{true, "Loaded map.", std::move(sectors), player_spawn, std::move(point_lights)};
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

void write_materials(std::ostream& output, SectorPlane sector) {
    normalize_sector(sector);
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

void write_loop(std::ostream& output, const char* label, const PolygonLoop& loop) {
    output << label << ' ' << loop.vertices.size() << '\n';
    for (const Vec2 vertex : loop.vertices) {
        output << "v " << vertex.x << ' ' << vertex.y << '\n';
    }
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
    std::ofstream output(path);
    if (!output) {
        return save_error("Could not open map file for writing: " + path.string());
    }

    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << kMapMagic << ' ' << kMapVersion << '\n';
    if (player_spawn.set) {
        output << "player_spawn "
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
               << light.position.x << ' '
               << light.position.y << ' '
               << light.position.z << ' '
               << light.color.x << ' '
               << light.color.y << ' '
               << light.color.z << ' '
               << light.radius << ' '
               << light.intensity << '\n';
    }
    output << "sectors " << sectors.size() << '\n';

    for (const SectorPlane& sector : sectors) {
        output << "sector\n";
        output << "height " << sector.height << '\n';
        output << "floor " << sector.floor_height << '\n';
        write_loop(output, "outer", sector.outer);
        output << "holes " << sector.holes.size() << '\n';
        for (const PolygonLoop& hole : sector.holes) {
            write_loop(output, "hole", hole);
        }
        write_materials(output, sector);
        write_displacement(output, "floor", sector.floor_displacement);
        write_displacement(output, "ceiling", sector.ceiling_displacement);
        output << "endsector\n";
    }

    if (!output) {
        return save_error("Failed while writing map file: " + path.string());
    }

    std::ostringstream message;
    message << "Saved " << sectors.size() << " sectors to " << path.string();
    return SaveMapResult{true, message.str()};
}

LoadMapResult load_map_file(const std::filesystem::path& path) {
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
        if (!read_expected(input, "sector", message)) {
            return load_error(message);
        }

        SectorPlane sector;
        if (!read_expected(input, "height", message)) {
            return load_error(message);
        }
        if (!read_float(input, sector.height, message)) {
            return load_error(message);
        }
        if (sector.height <= 0.0F) {
            return load_error("Sector height must be greater than zero.");
        }

        std::string next_token;
        if (!(input >> next_token)) {
            return load_error("Expected 'floor' or 'outer' after sector height.");
        }

        if (next_token == "floor") {
            if (!read_float(input, sector.floor_height, message)) {
                return load_error(message);
            }
            if (!read_loop(input, "outer", sector.outer, message)) {
                return load_error(message);
            }
        } else if (next_token == "outer") {
            sector.floor_height = 0.0F;
            if (!read_loop_after_label(input, "outer", sector.outer, message)) {
                return load_error(message);
            }
        } else {
            return load_error("Expected 'floor' or 'outer' but found '" + next_token + "'.");
        }

        if (!read_expected(input, "holes", message)) {
            return load_error(message);
        }

        std::size_t hole_count = 0;
        if (!read_count(input, hole_count, message)) {
            return load_error(message);
        }

        sector.holes.reserve(hole_count);
        for (std::size_t hole_index = 0; hole_index < hole_count; ++hole_index) {
            PolygonLoop hole;
            if (!read_loop(input, "hole", hole, message)) {
                return load_error(message);
            }
            sector.holes.push_back(std::move(hole));
        }

        std::string token;
        bool read_material_block = false;
        while (true) {
            if (!(input >> token)) {
                return load_error("Expected material/displacement fields or 'endsector'.");
            }
            if (token == "endsector") {
                break;
            }
            if (token == "materials") {
                if (read_material_block) {
                    return load_error("Duplicate materials block.");
                }
                read_material_block = true;
            if (!read_material_id(input, sector.floor_material, message) ||
                !read_material_id(input, sector.ceiling_material, message)) {
                return load_error(message);
            }
            if (!read_expected(input, "wall_materials", message)) {
                return load_error(message);
            }
            if (!read_materials(input, sector.wall_materials, sector.outer.vertices.size(), message)) {
                return load_error(message);
            }
            if (!read_expected(input, "hole_wall_materials", message)) {
                return load_error(message);
            }
            std::size_t hole_material_group_count = 0;
            if (!read_count(input, hole_material_group_count, message)) {
                return load_error(message);
            }
            if (hole_material_group_count != sector.holes.size()) {
                return load_error("Hole wall material group count does not match hole count.");
            }
            sector.hole_wall_materials.resize(hole_material_group_count);
            for (std::size_t hole_index = 0; hole_index < hole_material_group_count; ++hole_index) {
                if (!read_expected(input, "hole_wall_materials", message)) {
                    return load_error(message);
                }
                if (!read_materials(
                        input,
                        sector.hole_wall_materials[hole_index],
                        sector.holes[hole_index].vertices.size(),
                        message
                    )) {
                    return load_error(message);
                }
            }
                continue;
            }
            if (token == "surface_displacement") {
                if (!read_displacement(input, sector, message)) {
                    return load_error(message);
                }
                continue;
            }
            return load_error("Expected material/displacement fields or 'endsector' but found '" + token + "'.");
        }

        normalize_materials(sector);
        sectors.push_back(std::move(sector));
    }

    std::string trailing;
    if (input >> trailing) {
        return load_error("Unexpected trailing token: " + trailing);
    }

    return rebuild_loaded_sectors(std::move(sectors), player_spawn, std::move(point_lights));
}

} // namespace undecedent
