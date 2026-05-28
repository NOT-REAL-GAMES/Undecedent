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

std::vector<SectorPlane> subtract(std::vector<SectorPlane> sectors, const PolygonLoop& cut) {
    const undecedent::CsgAddResult result = undecedent::csg_subtract_sector(sectors, cut);
    if (!result.ok) {
        std::cerr << "CSG subtract failed: " << result.message << '\n';
        std::exit(EXIT_FAILURE);
    }
    return result.sectors;
}

std::vector<SectorPlane> merge(const std::vector<SectorPlane>& sectors, const std::vector<int>& selected) {
    const undecedent::CsgAddResult result = undecedent::csg_merge_sectors(sectors, selected);
    if (!result.ok) {
        std::cerr << "CSG merge failed with " << sectors.size() << " sectors:";
        for (const int index : selected) {
            std::cerr << ' ' << index;
        }
        std::cerr << " - " << result.message << '\n';
        for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
            std::cerr << "sector " << sector_index << " neighbors:";
            for (const int neighbor : sectors[sector_index].edge_neighbors) {
                std::cerr << ' ' << neighbor;
            }
            std::cerr << '\n';
        }
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

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = subtract(std::move(sectors), loop({{3, 3}, {7, 3}, {7, 7}, {3, 7}}));
        expect(sectors.size() == 1, "inner subtract should preserve one editable sector");
        expect(sectors.front().holes.size() == 1, "inner subtract should store a sector hole");
        expect_area("inner subtract", sectors, 84.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = subtract(std::move(sectors), loop({{8, 2}, {12, 2}, {12, 8}, {8, 8}}));
        expect_area("cross-sector subtract", sectors, 176.0F);
        expect(has_neighbor(sectors), "cross-sector subtract should preserve adjacency");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = subtract(std::move(sectors), loop({{4, 0}, {6, 0}, {6, 8}, {4, 8}}));
        expect_area("edge-touching subtract", sectors, 84.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = subtract(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        expect(sectors.size() == 1, "subtracting a whole face should remove it");
        expect_area("whole-face subtract", sectors, 100.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_height = 16.0F;
        sectors.front().height = 128.0F;
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        for (SectorPlane& sector : sectors) {
            for (Vec2& vertex : sector.outer.vertices) {
                if (std::abs(vertex.x - 10.0F) < 0.001F && std::abs(vertex.y - 10.0F) < 0.001F) {
                    vertex = Vec2{12.0F, 10.0F};
                }
            }
        }
        const undecedent::CsgAddResult result = undecedent::csg_rebuild_sectors(sectors);
        expect(result.ok, "rebuild after shared vertex move should succeed");
        expect(has_neighbor(result.sectors), "rebuild after shared vertex move should keep welded adjacency");
        expect(result.sectors.front().floor_height == 16.0F, "rebuild should preserve source floor height");
        expect(result.sectors.front().height == 128.0F, "rebuild should preserve source ceiling height");
        expect_area("shared vertex move rebuild", result.sectors, 200.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = merge(sectors, {0, 1});
        expect(sectors.size() == 1, "adjacent merge should produce one sector");
        expect(sectors.front().holes.empty(), "adjacent merge should not create holes");
        expect(sectors.front().outer.vertices.size() == 4, "adjacent rectangles should merge into one rectangle");
        expect_area("adjacent merge", sectors, 200.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {8, 0}, {8, 8}, {0, 8}}));
        sectors = add(std::move(sectors), loop({{8, 0}, {16, 0}, {16, 8}, {8, 8}}));
        sectors = add(std::move(sectors), loop({{0, 8}, {8, 8}, {8, 16}, {0, 16}}));
        sectors = merge(sectors, {0, 1, 2});
        expect(sectors.size() == 1, "concave merge should produce one sector");
        expect(sectors.front().outer.vertices.size() > 4, "concave merge should preserve concave outline");
        expect_area("concave merge", sectors, 192.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        const undecedent::CsgAddResult result = undecedent::csg_merge_sectors(sectors, {0, 1});
        expect(!result.ok, "disconnected merge should be rejected");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = subtract(std::move(sectors), loop({{3, 3}, {7, 3}, {7, 7}, {3, 7}}));
        expect(sectors.size() == 1, "contained-hole subtract should not require a merge cleanup");
        expect(sectors.front().holes.size() == 1, "contained-hole subtract should preserve the empty space as a hole");
        expect_area("contained-hole subtract", sectors, 84.0F);
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        sectors = merge(sectors, {0, 1});
        expect(sectors.size() == 2, "partial merge should keep unselected neighbor");
        expect_area("partial merge", sectors, 300.0F);
        expect(has_neighbor(sectors), "merged sector should keep adjacency with unselected neighbor");
    }

    return EXIT_SUCCESS;
}
