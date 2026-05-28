#include "undecedent/csg.hpp"
#include "undecedent/runtime_geometry.hpp"

#include <cstdlib>
#include <iostream>
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

std::vector<SectorPlane> add(std::vector<SectorPlane> sectors, const PolygonLoop& added) {
    const undecedent::CsgAddResult result = undecedent::csg_add_sector(sectors, added);
    if (!result.ok) {
        std::cerr << "CSG add failed: " << result.message << '\n';
        std::exit(EXIT_FAILURE);
    }
    return result.sectors;
}

} // namespace

int main() {
    {
        SectorPlane sector;
        expect(sector.height == 96.0F, "sector default height should be 96");
        expect(sector.floor_height == 0.0F, "sector default floor should be 0");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_height = 32.0F;
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() == 12, "single rectangle should create 2 floor, 2 ceiling, and 8 wall triangles");
        expect(geometry.triangles.front().a.y == 32.0F, "floor offset should move floor triangles");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() == 20, "two adjacent rectangles should omit the shared wall");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::CsgAddResult merged = undecedent::csg_merge_sectors(sectors, {0, 1});
        expect(merged.ok, "merge should succeed");
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(merged.sectors);
        expect(geometry.triangles.size() == 12, "merged adjacent rectangles should render as one larger room");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::CsgAddResult subtracted =
            undecedent::csg_subtract_sector(sectors, loop({{3, 3}, {7, 3}, {7, 7}, {3, 7}}));
        expect(subtracted.ok, "subtract should succeed");
        expect(subtracted.sectors.size() == 1 && subtracted.sectors.front().holes.size() == 1,
            "contained subtract should preserve one editable sector with a hole");

        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(subtracted.sectors);
        expect(geometry.triangles.size() == 32, "rectangle with one rectangular hole should create inner and outer walls");
    }

    return EXIT_SUCCESS;
}
