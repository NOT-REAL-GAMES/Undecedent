#include "undecedent/csg.hpp"
#include "undecedent/displacement.hpp"
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
        expect(sector.floor_material == 0, "sector default floor material should be 0");
        expect(sector.ceiling_material == 0, "sector default ceiling material should be 0");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_height = 32.0F;
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() == 12, "single rectangle should create 2 floor, 2 ceiling, and 8 wall triangles");
        expect(geometry.material_ids.size() == geometry.triangles.size(), "runtime geometry should tag every triangle with a material");
        expect(geometry.surfaces.size() == geometry.triangles.size(), "runtime geometry should tag every triangle with a surface");
        expect(geometry.uv_a.size() == geometry.triangles.size(), "runtime geometry should emit first UV per triangle");
        expect(geometry.uv_b.size() == geometry.triangles.size(), "runtime geometry should emit second UV per triangle");
        expect(geometry.uv_c.size() == geometry.triangles.size(), "runtime geometry should emit third UV per triangle");
        expect(geometry.triangles.front().a.y == 32.0F, "floor offset should move floor triangles");
        expect(geometry.uv_a.front().x == geometry.triangles.front().a.x &&
            geometry.uv_a.front().y == geometry.triangles.front().a.z,
            "floor UVs should match their vertex world X/Z coordinates");
        bool found_ceiling_uv = false;
        bool found_wall_uv = false;
        for (std::size_t i = 0; i < geometry.surfaces.size(); ++i) {
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Ceiling) {
                expect(geometry.uv_a[i].x == geometry.triangles[i].a.x &&
                    geometry.uv_a[i].y == -geometry.triangles[i].a.z,
                    "ceiling UVs should flip world Z for image-space texture Y");
                found_ceiling_uv = true;
            }
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Wall &&
                geometry.uv_b[i].y < geometry.uv_a[i].y) {
                found_wall_uv = true;
            }
        }
        expect(found_ceiling_uv, "single rectangle should emit ceiling UVs");
        expect(found_wall_uv, "wall UVs should flip vertical height for image-space texture Y");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {16, 0}, {16, 16}, {0, 16}}));
        sectors.front().floor_displacement.resolution = 2;
        const bool changed = undecedent::sculpt_surface_displacement(
            sectors.front(),
            undecedent::SectorSurfaceKind::Floor,
            Vec2{8, 8},
            32.0F,
            8.0F
        );
        expect(changed, "test displacement sculpt should change the floor");
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() > 12, "displaced sector should render subdivided surface triangles");
        bool found_raised_floor = false;
        for (std::size_t i = 0; i < geometry.triangles.size(); ++i) {
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Floor &&
                (geometry.triangles[i].a.y > 0.0F || geometry.triangles[i].b.y > 0.0F || geometry.triangles[i].c.y > 0.0F)) {
                found_raised_floor = true;
            }
        }
        expect(found_raised_floor, "displaced floor should emit raised runtime vertices");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_material = 2;
        sectors.front().ceiling_material = 3;
        sectors.front().wall_materials = {4, 5, 6, 7};
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        bool found_floor = false;
        bool found_ceiling = false;
        bool found_first_wall = false;
        bool found_second_wall = false;
        for (std::size_t i = 0; i < geometry.surfaces.size(); ++i) {
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Floor && geometry.material_ids[i] == 2) {
                found_floor = true;
            }
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Ceiling && geometry.material_ids[i] == 3) {
                found_ceiling = true;
            }
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Wall &&
                geometry.surfaces[i].index == 0 && geometry.material_ids[i] == 4) {
                found_first_wall = true;
            }
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::Wall &&
                geometry.surfaces[i].index == 1 && geometry.material_ids[i] == 5) {
                found_second_wall = true;
            }
        }
        expect(found_floor, "floor triangle should use floor material");
        expect(found_ceiling, "ceiling triangle should use ceiling material");
        expect(found_first_wall, "first wall should use first wall material");
        expect(found_second_wall, "second wall should use second wall material");
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
        sectors.front().height = 128.0F;
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() == 22, "height mismatch should add a shared-edge wall gap");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors.back().floor_height = 128.0F;
        const undecedent::RuntimeGeometry geometry = undecedent::build_runtime_geometry(sectors);
        expect(geometry.triangles.size() == 24, "non-overlapping neighbor volumes should close both shared edges");
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
        bool found_hole_wall_uv = false;
        for (std::size_t i = 0; i < geometry.surfaces.size(); ++i) {
            if (geometry.surfaces[i].kind == undecedent::RuntimeSurfaceKind::HoleWall &&
                geometry.uv_b[i].y < geometry.uv_a[i].y) {
                found_hole_wall_uv = true;
            }
        }
        expect(found_hole_wall_uv, "hole-wall UVs should flip vertical height for image-space texture Y");
    }

    return EXIT_SUCCESS;
}
