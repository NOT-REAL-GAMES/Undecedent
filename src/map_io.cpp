#include "undecedent/map_io.hpp"

#include "undecedent/triangulator.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace undecedent {
namespace {

constexpr const char* kMapMagic = "UNDECEDENT_MAP";
constexpr int kMapVersion = 1;

SaveMapResult save_error(const std::string& message) {
    return SaveMapResult{false, message};
}

LoadMapResult load_error(const std::string& message) {
    return LoadMapResult{false, message, {}};
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

LoadMapResult rebuild_loaded_sectors(std::vector<SectorPlane> sectors) {
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        SectorPlane& sector = sectors[i];
        const TriangulationResult result = triangulate_polygon(sector.outer, sector.holes);
        sector.status = result.status;
        sector.status_message = result.message;
        sector.triangles = result.triangles;
        if (result.status != TriangulationStatus::Ok) {
            std::ostringstream stream;
            stream << "Sector " << i << " failed triangulation: " << result.message;
            return load_error(stream.str());
        }
    }

    rebuild_exact_adjacency(sectors);
    return LoadMapResult{true, "Loaded map.", std::move(sectors)};
}

void write_loop(std::ostream& output, const char* label, const PolygonLoop& loop) {
    output << label << ' ' << loop.vertices.size() << '\n';
    for (const Vec2 vertex : loop.vertices) {
        output << "v " << vertex.x << ' ' << vertex.y << '\n';
    }
}

} // namespace

SaveMapResult save_map_file(const std::vector<SectorPlane>& sectors, const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) {
        return save_error("Could not open map file for writing: " + path.string());
    }

    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << kMapMagic << ' ' << kMapVersion << '\n';
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

    if (version != kMapVersion) {
        return load_error("Unsupported map version.");
    }

    if (!read_expected(input, "sectors", message)) {
        return load_error(message);
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

        if (!read_expected(input, "endsector", message)) {
            return load_error(message);
        }

        sectors.push_back(std::move(sector));
    }

    std::string trailing;
    if (input >> trailing) {
        return load_error("Unexpected trailing token: " + trailing);
    }

    return rebuild_loaded_sectors(std::move(sectors));
}

} // namespace undecedent
