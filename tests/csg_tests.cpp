#include "undecedent/csg.hpp"
#include "undecedent/displacement.hpp"

#include <algorithm>
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

std::vector<SectorPlane> knife(
    const std::vector<SectorPlane>& sectors,
    const Vec2 a,
    const Vec2 b,
    const float floor_height = 0.0F
) {
    const undecedent::CsgAddResult result = undecedent::csg_split_sectors_by_line_at_floor(sectors, a, b, floor_height);
    if (!result.ok) {
        std::cerr << "CSG knife failed: " << result.message << '\n';
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

bool point_in_triangle(const Vec2 point, const undecedent::Triangle& triangle) {
    const auto cross = [](const Vec2 a, const Vec2 b, const Vec2 c) {
        return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
    };
    const float ab = cross(triangle.a, triangle.b, point);
    const float bc = cross(triangle.b, triangle.c, point);
    const float ca = cross(triangle.c, triangle.a, point);
    return (ab >= -0.001F && bc >= -0.001F && ca >= -0.001F) ||
        (ab <= 0.001F && bc <= 0.001F && ca <= 0.001F);
}

bool sector_contains_point(const SectorPlane& sector, const Vec2 point) {
    return std::any_of(sector.triangles.begin(), sector.triangles.end(), [point](const undecedent::Triangle& triangle) {
        return point_in_triangle(point, triangle);
    });
}

SectorPlane& sector_at_point(std::vector<SectorPlane>& sectors, const Vec2 point) {
    const auto found = std::find_if(sectors.begin(), sectors.end(), [point](const SectorPlane& sector) {
        return sector_contains_point(sector, point);
    });
    if (found == sectors.end()) {
        std::cerr << "Expected sector at point " << point.x << ", " << point.y << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *found;
}

const SectorPlane& sector_at_point(const std::vector<SectorPlane>& sectors, const Vec2 point) {
    const auto found = std::find_if(sectors.begin(), sectors.end(), [point](const SectorPlane& sector) {
        return sector_contains_point(sector, point);
    });
    if (found == sectors.end()) {
        std::cerr << "Expected sector at point " << point.x << ", " << point.y << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *found;
}

float cross(const Vec2 a, const Vec2 b, const Vec2 c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

bool on_segment(const Vec2 a, const Vec2 b, const Vec2 point) {
    return std::abs(cross(a, b, point)) <= 0.001F &&
        point.x >= std::min(a.x, b.x) - 0.001F &&
        point.x <= std::max(a.x, b.x) + 0.001F &&
        point.y >= std::min(a.y, b.y) - 0.001F &&
        point.y <= std::max(a.y, b.y) + 0.001F;
}

int outer_material_on_segment(const SectorPlane& sector, const Vec2 a, const Vec2 b) {
    for (std::size_t i = 0; i < sector.outer.vertices.size(); ++i) {
        const Vec2 edge_a = sector.outer.vertices[i];
        const Vec2 edge_b = sector.outer.vertices[(i + 1) % sector.outer.vertices.size()];
        if (on_segment(edge_a, edge_b, a) && on_segment(edge_a, edge_b, b)) {
            return i < sector.wall_materials.size() ? sector.wall_materials[i] : undecedent::kDefaultMaterialId;
        }
        if (on_segment(a, b, edge_a) && on_segment(a, b, edge_b)) {
            return i < sector.wall_materials.size() ? sector.wall_materials[i] : undecedent::kDefaultMaterialId;
        }
    }
    return -1;
}

int hole_material_on_segment(const SectorPlane& sector, const Vec2 a, const Vec2 b) {
    for (std::size_t hole_index = 0; hole_index < sector.holes.size(); ++hole_index) {
        const PolygonLoop& hole = sector.holes[hole_index];
        for (std::size_t edge_index = 0; edge_index < hole.vertices.size(); ++edge_index) {
            const Vec2 edge_a = hole.vertices[edge_index];
            const Vec2 edge_b = hole.vertices[(edge_index + 1) % hole.vertices.size()];
            if (on_segment(edge_a, edge_b, a) && on_segment(edge_a, edge_b, b)) {
                return hole_index < sector.hole_wall_materials.size() &&
                        edge_index < sector.hole_wall_materials[hole_index].size()
                    ? sector.hole_wall_materials[hole_index][edge_index]
                    : undecedent::kDefaultMaterialId;
            }
        }
    }
    return -1;
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
        int old_owned_faces = 0;
        int new_owned_faces = 0;
        for (const SectorPlane& sector : sectors) {
            if (sector.floor_material == 3 && sector.ceiling_material == 4) {
                ++old_owned_faces;
            }
            if (sector.floor_material == 0 && sector.ceiling_material == 0) {
                ++new_owned_faces;
            }
        }
        expect(old_owned_faces == 2, "overlap add should preserve existing floor and ceiling materials on old-owned faces");
        expect(new_owned_faces == 1, "overlap add should leave new-only face materials at default");
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
        sectors.front().wall_materials = {1, 2, 3, 4};
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        expect(sectors.size() == 2, "edge-touching add should keep two sectors");
        expect_area("touching edge", sectors, 200.0F);
        expect(has_neighbor(sectors), "edge-touching sectors should become neighbors");
        const SectorPlane& original = sector_at_point(sectors, Vec2{5, 5});
        expect(outer_material_on_segment(original, Vec2{0, 0}, Vec2{10, 0}) == 1,
            "adjacent add should preserve original bottom wall material");
        expect(outer_material_on_segment(original, Vec2{10, 0}, Vec2{10, 10}) == 2,
            "adjacent add should preserve original touched wall material");
        expect(outer_material_on_segment(original, Vec2{10, 10}, Vec2{0, 10}) == 3,
            "adjacent add should preserve original top wall material");
        expect(outer_material_on_segment(original, Vec2{0, 10}, Vec2{0, 0}) == 4,
            "adjacent add should preserve original left wall material");
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
        sectors.front().wall_materials = {1, 2, 3, 4};
        sectors.front().hole_wall_materials = {{5, 6, 7, 1}};
        sectors.front().outer.vertices[2] = Vec2{0, 12};
        const undecedent::CsgAddResult result = undecedent::csg_rebuild_sectors(sectors);
        expect(result.ok, "rebuild after outer vertex move with hole should succeed");
        expect(result.sectors.size() == 1, "rebuild after outer vertex move should keep one sector");
        expect(result.sectors.front().holes.size() == 1, "rebuild after outer vertex move should preserve contained hole");
        expect_area("outer vertex move with hole rebuild", result.sectors, 204.0F);
        expect(materials_are_valid(result.sectors), "hole-preserving rebuild should keep material arrays valid");
        expect(outer_material_on_segment(result.sectors.front(), Vec2{10, 0}, Vec2{0, 12}) == 2,
            "rebuild should preserve remapped outer wall material");
        expect(hole_material_on_segment(result.sectors.front(), Vec2{-2, -2}, Vec2{2, -2}) == 5,
            "rebuild should preserve hole wall material");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 96.0F);
        expect(sectors.size() == 2, "same-XY sectors on different floors should coexist");
        expect_area("stacked floor add", sectors, 200.0F);
        expect(!has_neighbor(sectors), "stacked floor sectors should not become neighbors");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_material = 5;
        sectors.front().ceiling_material = 6;
        sectors.front().wall_materials = {1, 2, 3, 4};
        sectors = knife(sectors, Vec2{5, -5}, Vec2{5, 15});
        expect(sectors.size() == 2, "knife should split one rectangle into two sectors");
        expect_area("knife split rectangle", sectors, 100.0F);
        expect(has_neighbor(sectors), "knife split should create shared adjacency");
        for (const SectorPlane& sector : sectors) {
            expect(sector.floor_material == 5 && sector.ceiling_material == 6,
                "knife split should preserve source floor and ceiling materials");
        }
        const SectorPlane& left = sector_at_point(sectors, Vec2{2, 5});
        const SectorPlane& right = sector_at_point(sectors, Vec2{8, 5});
        expect(outer_material_on_segment(left, Vec2{0, 0}, Vec2{5, 0}) == 1,
            "knife split should preserve bottom wall material on split left sector");
        expect(outer_material_on_segment(right, Vec2{5, 0}, Vec2{10, 0}) == 1,
            "knife split should preserve bottom wall material on split right sector");
        expect(outer_material_on_segment(left, Vec2{0, 10}, Vec2{5, 10}) == 3,
            "knife split should preserve top wall material on split left sector");
        expect(outer_material_on_segment(right, Vec2{5, 10}, Vec2{10, 10}) == 3,
            "knife split should preserve top wall material on split right sector");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors.front().wall_materials = {1, 2, 3, 4};
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 96.0F);
        for (SectorPlane& sector : sectors) {
            if (std::abs(sector.floor_height - 96.0F) <= 0.001F) {
                sector.wall_materials = {4, 3, 2, 1};
            }
        }
        sectors = knife(sectors, Vec2{5, -5}, Vec2{5, 15}, 0.0F);
        int lower_count = 0;
        int upper_count = 0;
        for (const SectorPlane& sector : sectors) {
            if (std::abs(sector.floor_height) <= 0.001F) {
                ++lower_count;
            }
            if (std::abs(sector.floor_height - 96.0F) <= 0.001F) {
                ++upper_count;
                expect(outer_material_on_segment(sector, Vec2{0, 0}, Vec2{10, 0}) == 4,
                    "knife should leave inactive stacked floor wall materials unchanged");
            }
        }
        expect(lower_count == 2, "knife should split only active floor sectors");
        expect(upper_count == 1, "knife should preserve inactive stacked floor sector count");
    }

    {
        std::vector<SectorPlane> sectors = add_at_floor({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 0.0F);
        sectors.front().floor_material = 5;
        sectors.front().ceiling_material = 6;
        sectors.front().wall_materials = {1, 2, 3, 4};
        sectors = add_at_floor(std::move(sectors), loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 64.0F);
        for (SectorPlane& sector : sectors) {
            if (std::abs(sector.floor_height - 64.0F) <= 0.001F) {
                sector.floor_material = 7;
                sector.ceiling_material = 6;
                sector.wall_materials = {4, 3, 2, 1};
            }
        }
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
        for (const SectorPlane& sector : sectors) {
            if (std::abs(sector.floor_height - 64.0F) <= 0.001F) {
                expect(sector.floor_material == 7 && sector.ceiling_material == 6,
                    "scoped floor add should preserve inactive floor and ceiling materials");
                expect(outer_material_on_segment(sector, Vec2{0, 0}, Vec2{10, 0}) == 4,
                    "scoped floor add should preserve inactive wall materials");
            }
        }
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sector_at_point(sectors, Vec2{5, 5}).floor_material = 6;
        sector_at_point(sectors, Vec2{5, 5}).ceiling_material = 7;
        sector_at_point(sectors, Vec2{5, 5}).wall_materials = {1, 2, 3, 4};
        sector_at_point(sectors, Vec2{15, 5}).wall_materials = {1, 5, 3, 2};
        sectors = merge(sectors, {0, 1});
        expect(sectors.size() == 1, "adjacent merge should produce one sector");
        expect(sectors.front().holes.empty(), "adjacent merge should not create holes");
        expect(sectors.front().outer.vertices.size() == 4, "adjacent rectangles should merge into one rectangle");
        expect_area("adjacent merge", sectors, 200.0F);
        expect(materials_are_valid(sectors), "CSG merge should keep material arrays valid");
        expect(sectors.front().floor_material == 6, "merge should preserve primary floor material");
        expect(sectors.front().ceiling_material == 7, "merge should preserve primary ceiling material");
        expect(outer_material_on_segment(sectors.front(), Vec2{0, 0}, Vec2{20, 0}) == 1,
            "merge should preserve compatible bottom wall material");
        expect(outer_material_on_segment(sectors.front(), Vec2{20, 0}, Vec2{20, 10}) == 5,
            "merge should preserve right exterior wall material");
        expect(outer_material_on_segment(sectors.front(), Vec2{20, 10}, Vec2{0, 10}) == 3,
            "merge should preserve compatible top wall material");
        expect(outer_material_on_segment(sectors.front(), Vec2{0, 10}, Vec2{0, 0}) == 4,
            "merge should preserve left exterior wall material");
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

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 2;
        undecedent::sculpt_surface_displacement(
            sectors.front(),
            undecedent::SectorSurfaceKind::Floor,
            Vec2{5, 5},
            32.0F,
            12.0F
        );
        sectors = knife(sectors, Vec2{5, -2}, Vec2{5, 12});
        expect(sectors.size() == 2, "knife should split displaced sector");
        for (const SectorPlane& sector : sectors) {
            expect(sector.floor_displacement.enabled, "knife split should preserve floor displacement");
            const undecedent::Triangle triangle = sector.triangles.front();
            const Vec2 sample{
                (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0F,
                (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0F,
            };
            expect(undecedent::sample_surface_height(sector, undecedent::SectorSurfaceKind::Floor, sample) > sector.floor_height,
                "knife split should resample displaced floor height");
        }
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 2;
        undecedent::sculpt_surface_displacement(
            sectors.front(),
            undecedent::SectorSurfaceKind::Floor,
            Vec2{5, 5},
            32.0F,
            12.0F
        );
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const SectorPlane& added = sector_at_point(sectors, Vec2{15, 5});
        expect(
            std::abs(undecedent::sample_surface_height(added, undecedent::SectorSurfaceKind::Floor, Vec2{15, 5}) -
                added.floor_height) <= 0.001F,
            "newly-added CSG space should sample flat"
        );
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 4;
        undecedent::sculpt_surface_displacement(
            sectors.front(),
            undecedent::SectorSurfaceKind::Floor,
            Vec2{5, 5},
            32.0F,
            8.0F
        );
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = knife(sectors, Vec2{15, -2}, Vec2{15, 12});
        const SectorPlane& right_a = sector_at_point(sectors, Vec2{12.5F, 5.0F});
        const SectorPlane& right_b = sector_at_point(sectors, Vec2{17.5F, 5.0F});
        expect(!right_a.floor_displacement.enabled, "knife split should not enable subdivision on flat connected sector");
        expect(!right_b.floor_displacement.enabled, "knife split should keep both flat connected pieces unsubdivided");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 4;
        undecedent::sculpt_surface_displacement(
            sectors.front(),
            undecedent::SectorSurfaceKind::Floor,
            Vec2{5, 5},
            32.0F,
            8.0F
        );
        sectors.front().outer.vertices[2] = Vec2{8, 10};
        const undecedent::CsgAddResult rebuilt = undecedent::csg_rebuild_sectors(sectors);
        expect(rebuilt.ok, "rebuild after vertex edit should succeed");
        expect(rebuilt.sectors.size() == 1, "single edited sector should stay single after rebuild");
        expect(
            rebuilt.sectors.front().floor_displacement.enabled,
            "rebuild after vertex edit should preserve subdivision despite stale source triangles"
        );
    }

    return EXIT_SUCCESS;
}
