#include "undecedent/map_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

} // namespace

int main() {
    {
        const std::filesystem::path path = test_path("undecedent_map_io_rectangle.udmap");
        SectorPlane sector;
        sector.outer = loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "rectangle save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "rectangle load should succeed");
        expect(loaded.sectors.size() == 1, "rectangle load should contain one sector");
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
        sector.holes.push_back(loop({{6, 6}, {14, 6}, {14, 14}, {6, 14}}));
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, path);
        expect(saved.ok, "sector with hole save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "sector with hole load should succeed");
        expect(loaded.sectors.size() == 1, "sector with hole load should contain one sector");
        expect(loaded.sectors.front().floor_height == 24.0F, "non-default floor should round-trip");
        expect(loaded.sectors.front().height == 144.0F, "non-default height should round-trip");
        expect(loaded.sectors.front().holes.size() == 1, "hole should round-trip");
        expect(!loaded.sectors.front().triangles.empty(), "sector with hole should rebuild triangles");
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
        expect(loaded.sectors.front().floor_height == 0.0F, "legacy map floor should default to zero");
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
