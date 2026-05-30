#include "undecedent/csg.hpp"
#include "undecedent/runtime_render.hpp"
#include "undecedent/runtime_visibility.hpp"
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

undecedent::GameCamera camera_at(
    const float x,
    const float y,
    const float z,
    const float yaw,
    const float pitch = 0.0F
) {
    undecedent::GameCamera camera{};
    camera.x = x;
    camera.y = y;
    camera.z = z;
    camera.yaw = yaw;
    camera.pitch = pitch;
    return camera;
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
        expect(world.portals.size() == 2, "adjacent rectangles should create one directed portal per side");
        expect(!world.sectors[0].portal_ids.empty(), "first sector should expose a runtime portal");
        expect(!world.sectors[1].portal_ids.empty(), "second sector should expose a runtime portal");
        expect(world.walls.size() == 6, "adjacent rectangles should omit the shared wall segment");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors.front().height = 128.0F;
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(contains(world.sectors[0].neighbors, 1), "height-offset sectors should stay linked when volumes overlap");
        expect(world.walls.size() == 7, "height mismatch should create one shared-edge wall gap");
        expect(world.triangles.size() == 22, "height mismatch should render the wall gap triangles");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors.back().floor_height = 128.0F;
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(!contains(world.sectors[0].neighbors, 1), "non-overlapping sectors should not be runtime neighbors");
        expect(!contains(world.sectors[1].neighbors, 0), "non-overlapping reverse neighbor should be suppressed");
        expect(world.walls.size() == 8, "non-overlapping neighbor volumes should close both shared edges");
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
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 5.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(visible.size() == 1 && contains(visible, 0), "portal visibility should include the containing sector");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 5.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(contains(visible, 0) && contains(visible, 1), "portal visibility should traverse a visible portal");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 5.0F, 1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(contains(visible, 0), "portal visibility should keep the containing sector when the portal is behind");
        expect(!contains(visible, 1), "portal visibility should reject a portal behind the camera");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors = add(std::move(sectors), loop({{40, 0}, {50, 0}, {50, 10}, {40, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 5.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(contains(visible, 0) && contains(visible, 1), "portal visibility should keep the visible connected island");
        expect(!contains(visible, 2), "portal visibility should reject disconnected sectors");
    }

    {
        std::vector<SectorPlane> sectors(3);
        sectors[0].outer = loop({{0, 0}, {10, 0}, {10, 2}, {0, 2}});
        sectors[0].edge_neighbors = {-1, 1, -1, -1};
        sectors[1].outer = loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}, {10, 2}});
        sectors[1].edge_neighbors = {-1, -1, 2, -1, 0};
        sectors[2].outer = loop({{10, 10}, {20, 10}, {20, 20}, {10, 20}});
        sectors[2].edge_neighbors = {1, -1, -1, -1};
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const int camera_corridor = undecedent::sector_at_point(world, Vec3{5.0F, 48.0F, 1.0F});
        const int forward_corridor = undecedent::sector_at_point(world, Vec3{15.0F, 48.0F, 5.0F});
        const int side_room = undecedent::sector_at_point(world, Vec3{15.0F, 48.0F, 15.0F});
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 1.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(camera_corridor >= 0 && forward_corridor >= 0 && side_room >= 0, "corridor test sectors should exist");
        expect(contains(visible, camera_corridor), "portal visibility should keep the camera corridor");
        expect(contains(visible, forward_corridor), "portal visibility should traverse the corridor ahead");
        expect(!contains(visible, side_room), "portal visibility should cull a side room hidden behind a turned corner");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(100.0F, 48.0F, 100.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(visible.empty(), "portal visibility should return empty outside all sectors for draw-all fallback");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        sectors.front().height = 300.0F;
        sectors.back().floor_height = 200.0F;
        sectors.back().height = 50.0F;
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const std::vector<int> visible = undecedent::visible_sectors_from_camera(
            world,
            camera_at(5.0F, 48.0F, 5.0F, -1.5707963F),
            undecedent::GameRenderConfig{},
            1280,
            720
        );
        expect(contains(visible, 0), "vertical portal visibility should keep the containing sector");
        expect(!contains(visible, 1), "vertical portal visibility should reject a portal outside the camera view");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(!world.triangles.empty(), "runtime world should generate render triangles");
        for (const undecedent::RuntimeTaggedTriangle& triangle : world.triangles) {
            expect(triangle.sector_id == 0, "runtime triangles should retain source sector id");
            expect(triangle.material_id == 0, "runtime triangles should default to material 0");
        }
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_material = 5;
        sectors.front().ceiling_material = 6;
        sectors.front().wall_materials = {1, 2, 3, 4};
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(world.triangles[0].material_id == 5, "runtime floor should retain material id");
        expect(world.triangles[1].material_id == 6, "runtime ceiling should retain material id");
        expect(world.triangles[4].material_id == 1, "runtime wall should retain material id");
        expect(world.triangles[4].surface.kind == undecedent::RuntimeSurfaceKind::Wall,
            "runtime wall should retain surface kind");
        expect(world.triangles[4].surface.index == 0, "runtime wall should retain edge index");
    }

    return EXIT_SUCCESS;
}
