#include "undecedent/csg.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using undecedent::PolygonLoop;
using undecedent::SectorPlane;
using undecedent::Vec2;

PolygonLoop loop(std::initializer_list<Vec2> vertices) {
    return PolygonLoop{std::vector<Vec2>(vertices)};
}

float triangle_area(const undecedent::Triangle& triangle) {
    return std::abs(
        ((triangle.a.x * (triangle.b.y - triangle.c.y)) +
            (triangle.b.x * (triangle.c.y - triangle.a.y)) +
            (triangle.c.x * (triangle.a.y - triangle.b.y))) *
        0.5F
    );
}

float sector_area(const SectorPlane& sector) {
    float area = 0.0F;
    for (const undecedent::Triangle& triangle : sector.triangles) {
        area += triangle_area(triangle);
    }
    return area;
}

float total_area(const std::vector<SectorPlane>& sectors) {
    float area = 0.0F;
    for (const SectorPlane& sector : sectors) {
        area += sector_area(sector);
    }
    return area;
}

bool has_neighbor(const std::vector<SectorPlane>& sectors) {
    for (const SectorPlane& sector : sectors) {
        for (const int neighbor : sector.edge_neighbors) {
            if (neighbor >= 0) {
                return true;
            }
        }
    }
    return false;
}

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_area(const char* name, const std::vector<SectorPlane>& sectors, const float expected) {
    const float actual = total_area(sectors);
    if (std::abs(actual - expected) > 0.05F) {
        std::cerr << name << " expected area " << expected << " got " << actual << '\n';
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
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        expect(sectors.size() == 2, "disjoint add should create two sectors");
        expect_area("disjoint", sectors, 200.0F);
        expect(!has_neighbor(sectors), "disjoint sectors should not be neighbors");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{5, 0}, {15, 0}, {15, 10}, {5, 10}}));
        expect(sectors.size() == 3, "overlap add should split into three sectors");
        expect_area("overlap", sectors, 150.0F);
        expect(has_neighbor(sectors), "overlap split should create neighbor edges");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        sectors = add(std::move(sectors), loop({{8, 3}, {22, 3}, {22, 7}, {8, 7}}));
        expect(sectors.size() >= 4, "bridge add should split across multiple sectors");
        expect_area("bridge", sectors, 240.0F);
        expect(has_neighbor(sectors), "bridge add should create neighbor edges");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        expect(sectors.size() == 2, "edge-touching add should keep two sectors");
        expect_area("touching edge", sectors, 200.0F);
        expect(has_neighbor(sectors), "edge-touching sectors should become neighbors");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 10}, {20, 10}, {20, 20}, {10, 20}}));
        expect(sectors.size() == 2, "vertex-touching add should keep two sectors");
        expect_area("touching vertex", sectors, 200.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {8, 8}}));
        sectors = add(std::move(sectors), loop({{4, -2}, {14, 2}, {5, 9}}));
        expect(sectors.size() > 2, "overlapping triangles should split into multiple sectors");
        expect(has_neighbor(sectors), "overlapping triangle split should create neighbor edges");
    }

    return EXIT_SUCCESS;
}
