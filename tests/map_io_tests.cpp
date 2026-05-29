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
        const undecedent::SaveMapResult saved = undecedent::save_map_file({sector}, spawn, {light}, path);
        expect(saved.ok, "point light save should succeed");

        const undecedent::LoadMapResult loaded = undecedent::load_map_file(path);
        expect(loaded.ok, "point light load should succeed");
        expect(loaded.point_lights.size() == 1, "point light should round-trip");
        expect(loaded.point_lights.front().position.x == 4.0F, "point light x should round-trip");
        expect(loaded.point_lights.front().position.y == 64.0F, "point light y should round-trip");
        expect(loaded.point_lights.front().position.z == 6.0F, "point light z should round-trip");
        expect(loaded.point_lights.front().color.x == 0.75F, "point light color should round-trip");
        expect(loaded.point_lights.front().radius == 256.0F, "point light radius should round-trip");
        expect(loaded.point_lights.front().intensity == 2.25F, "point light intensity should round-trip");
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
        expect(loaded.sectors.front().floor_height == 0.0F, "legacy map floor should default to zero");
        expect(loaded.sectors.front().floor_material == 0, "legacy map floor material should default to zero");
        expect(loaded.sectors.front().wall_materials.size() == 4, "legacy map wall materials should be generated");
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
