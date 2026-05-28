#include "undecedent/csg.hpp"
#include "undecedent/runtime_world.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using undecedent::PolygonLoop;
using undecedent::SectorPlane;
using undecedent::Vec2;
using undecedent::Vec3;

PolygonLoop loop(std::initializer_list<Vec2> vertices) {
    return PolygonLoop{std::vector<Vec2>(vertices)};
}

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::vector<SectorPlane> add(std::vector<SectorPlane> sectors, const PolygonLoop& added) {
    const undecedent::CsgAddResult result = undecedent::csg_add_sector(sectors, added);
    if (!result.ok) {
        std::cerr << "CSG add failed: " << result.message << '\n';
        std::exit(EXIT_FAILURE);
    }
    return result.sectors;
}

bool contains(const std::vector<int>& values, const int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

int main() {
    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(world.cell_size == 128.0F, "runtime world should default to 128-unit cells");
        expect(world.sectors.size() == 1, "one source sector should create one runtime sector");
        expect(world.sectors.front().bounds.min_x == 0.0F && world.sectors.front().bounds.max_x == 10.0F,
            "runtime sector should store source bounds");
        expect(!world.spatial_cells.empty(), "runtime spatial hash should contain cells");
        expect(world.walls.size() == 4, "single rectangle should create four solid wall segments");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(world.sectors.size() == 2, "two source sectors should create two runtime sectors");
        expect(contains(world.sectors[0].neighbors, 1), "first sector should link to second sector");
        expect(contains(world.sectors[1].neighbors, 0), "second sector should link to first sector");
        expect(world.walls.size() == 6, "adjacent rectangles should omit the shared wall segment");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(undecedent::sector_at_point(world, Vec3{5, 0, 5}) == 0, "point lookup should find first sector");
        expect(undecedent::sector_at_point(world, Vec3{25, 0, 5}) == 1, "point lookup should find second sector");
        expect(undecedent::sector_at_point(world, Vec3{15, 0, 5}) == -1, "point lookup should reject empty space");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_height = 16.0F;
        sectors.front().height = 32.0F;
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(world.sectors.front().floor_height == 16.0F, "runtime sector should retain source floor height");
        expect(undecedent::sector_at_point(world, Vec3{5, 20, 5}) == 0, "point lookup should find elevated sector volume");
        expect(undecedent::sector_at_point(world, Vec3{5, 8, 5}) == -1, "point lookup should reject point below floor");
        expect(undecedent::sector_at_point(world, Vec3{5, 64, 5}) == -1, "point lookup should reject point above ceiling");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(contains(undecedent::sectors_near_point(world, Vec3{5, 0, 5}), 0),
            "nearby sector point query should return containing cell sector candidates");
        expect(!undecedent::walls_near_point(world, Vec3{0, 0, 0}).empty(),
            "nearby wall point query should return wall candidates");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = add(std::move(sectors), loop({{40, 0}, {50, 0}, {50, 10}, {40, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from(world, 0);
        expect(contains(visible, 0) && contains(visible, 1), "visible traversal should include connected sectors");
        expect(!contains(visible, 2), "visible traversal should not include disconnected sectors");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(!world.triangles.empty(), "runtime world should generate render triangles");
        for (const undecedent::RuntimeTaggedTriangle& triangle : world.triangles) {
            expect(triangle.sector_id == 0, "runtime triangles should retain source sector id");
        }
    }

    return EXIT_SUCCESS;
}
