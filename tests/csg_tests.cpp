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

std::vector<SectorPlane> add_at_floor(std::vector<SectorPlane> sectors, const PolygonLoop& added, const float floor_height) {
    const undecedent::CsgAddResult result = undecedent::csg_add_sector_at_floor(sectors, added, floor_height);
    if (!result.ok) {
        std::cerr << "CSG floor add failed: " << result.message << '\n';
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

std::vector<SectorPlane> delete_sectors(const std::vector<SectorPlane>& sectors, const std::vector<int>& selected) {
    const undecedent::CsgAddResult result = undecedent::csg_delete_sectors(sectors, selected);
    if (!result.ok) {
        std::cerr << "CSG delete failed:";
        for (const int index : selected) {
            std::cerr << ' ' << index;
        }
        std::cerr << " - " << result.message << '\n';
        std::exit(EXIT_FAILURE);
    }
    return result.sectors;
}

bool all_edges_solid(const std::vector<SectorPlane>& sectors) {
    for (const SectorPlane& sector : sectors) {
        for (const int neighbor : sector.edge_neighbors) {
            if (neighbor >= 0) {
                return false;
            }
        }
    }
    return true;
}

bool materials_are_valid(const std::vector<SectorPlane>& sectors) {
    for (const SectorPlane& sector : sectors) {
        if (sector.floor_material < 0 || sector.floor_material >= undecedent::kMaterialCount ||
            sector.ceiling_material < 0 || sector.ceiling_material >= undecedent::kMaterialCount) {
            return false;
        }
        if (sector.wall_materials.size() != sector.outer.vertices.size()) {
            return false;
        }
        if (sector.hole_wall_materials.size() != sector.holes.size()) {
            return false;
        }
        for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
            if (sector.hole_wall_materials[hole_index].size() != sector.holes[hole_index].vertices.size()) {
                return false;
            }
        }
    }
    return true;
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
        sectors.front().floor_material = 3;
        sectors.front().ceiling_material = 4;
        sectors = add(std::move(sectors), loop({{5, 0}, {15, 0}, {15, 10}, {5, 10}}));
        expect(sectors.size() == 3, "overlap add should split into three sectors");
        expect_area("overlap", sectors, 150.0F);
        expect(has_neighbor(sectors), "overlap split should create neighbor edges");
        expect(materials_are_valid(sectors), "CSG add should keep material arrays valid");
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
        expect(materials_are_valid(sectors), "CSG subtract should keep material arrays valid");
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
        const undecedent::CsgAddResult same_floor_add =
            undecedent::csg_add_sector_at_floor(sectors, loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}), 16.0F, 128.0F);
        expect(same_floor_add.ok, "same-floor add should succeed");
        sectors = same_floor_add.sectors;
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
        std::vector<SectorPlane> sectors = add({}, loop({{0, -10}, {10, 0}, {0, 10}, {-10, 0}}));
        sectors = subtract(std::move(sectors), loop({{-2, -2}, {2, -2}, {2, 2}, {-2, 2}}));
        expect(sectors.size() == 1 && sectors.front().holes.size() == 1,
            "test setup should create one sector with one contained hole");
        sectors.front().outer.vertices[2] = Vec2{0, 12};
        const undecedent::CsgAddResult result = undecedent::csg_rebuild_sectors(sectors);
        expect(result.ok, "rebuild after outer vertex move with hole should succeed");
        expect(result.sectors.size() == 1, "rebuild after outer vertex move should keep one sector");
        expect(result.sectors.front().holes.size() == 1, "rebuild after outer vertex move should preserve contained hole");
        expect_area("outer vertex move with hole rebuild", result.sectors, 204.0F);
        expect(materials_are_valid(result.sectors), "hole-preserving rebuild should keep material arrays valid");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 96.0F);
        expect(sectors.size() == 2, "same-XY sectors on different floors should coexist");
        expect_area("stacked floor add", sectors, 200.0F);
        expect(!has_neighbor(sectors), "stacked floor sectors should not become neighbors");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 64.0F);
        sectors = add_at_floor(std::move(sectors), loop({{5, 0}, {15, 0}, {15, 10}, {5, 10}}), 0.0F);
        expect(sectors.size() == 4, "active-floor add should split only sectors on the edited floor");
        expect_area("scoped floor add", sectors, 250.0F);
        int floor_zero_count = 0;
        int floor_sixty_four_count = 0;
        for (const SectorPlane& sector : sectors) {
            if (std::abs(sector.floor_height) <= 0.001F) {
                ++floor_zero_count;
            }
            if (std::abs(sector.floor_height - 64.0F) <= 0.001F) {
                ++floor_sixty_four_count;
            }
        }
        expect(floor_zero_count == 3, "edited floor should contain the split sectors");
        expect(floor_sixty_four_count == 1, "inactive stacked floor should be preserved unchanged");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = merge(sectors, {0, 1});
        expect(sectors.size() == 1, "adjacent merge should produce one sector");
        expect(sectors.front().holes.empty(), "adjacent merge should not create holes");
        expect(sectors.front().outer.vertices.size() == 4, "adjacent rectangles should merge into one rectangle");
        expect_area("adjacent merge", sectors, 200.0F);
        expect(materials_are_valid(sectors), "CSG merge should keep material arrays valid");
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

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = delete_sectors(sectors, {1});
        expect(sectors.size() == 1, "deleting one adjacent sector should leave one sector");
        expect_area("delete adjacent sector", sectors, 100.0F);
        expect(all_edges_solid(sectors), "deleted shared edge should become solid");
        expect(materials_are_valid(sectors), "CSG delete should keep material arrays valid");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        sectors = delete_sectors(sectors, {1});
        expect(sectors.size() == 2, "deleting middle sector should leave two sectors");
        expect_area("delete middle sector", sectors, 200.0F);
        expect(all_edges_solid(sectors), "sectors separated by a deleted middle should not remain neighbors");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = add(std::move(sectors), loop({{20, 0}, {30, 0}, {30, 10}, {20, 10}}));
        sectors = delete_sectors(sectors, {0, 2, 2, -1, 99});
        expect(sectors.size() == 1, "multi-delete should remove valid unique selected sectors only");
        expect_area("delete multiple sectors", sectors, 100.0F);
        expect(all_edges_solid(sectors), "remaining sector after multi-delete should have solid edges");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = delete_sectors(sectors, {0});
        expect(sectors.empty(), "deleting all sectors should return an empty map");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 96.0F);
        sectors = delete_sectors(sectors, {0});
        expect(sectors.size() == 1, "deleting stacked lower floor should leave upper floor");
        expect(std::abs(sectors.front().floor_height - 96.0F) <= 0.001F, "remaining stacked sector should keep its floor");
        expect_area("delete stacked floor", sectors, 100.0F);
    }

    return EXIT_SUCCESS;
}
