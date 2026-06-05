#include "undecedent/csg.hpp"
#include "undecedent/displacement.hpp"
#include "undecedent/physics.hpp"
#include "undecedent/triangulator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using undecedent::PlayerPhysicsConfig;
using undecedent::PlayerPhysicsState;
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

void expect_near(const float actual, const float expected, const float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << message << " (actual=" << actual << ", expected=" << expected << ")\n";
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

SectorPlane make_sector(const PolygonLoop& outer, const float floor_height = 0.0F, const float height = 96.0F) {
    SectorPlane sector;
    sector.outer = outer;
    sector.floor_height = floor_height;
    sector.height = height;
    const undecedent::TriangulationResult triangulated = undecedent::triangulate_polygon(sector.outer, sector.holes);
    if (triangulated.status != undecedent::TriangulationStatus::Ok) {
        std::cerr << "Triangulation failed: " << triangulated.message << '\n';
        std::exit(EXIT_FAILURE);
    }
    sector.triangles = triangulated.triangles;
    sector.edge_neighbors.assign(sector.outer.vertices.size(), -1);
    return sector;
}

} // namespace

int main() {
    const PlayerPhysicsConfig config{1.0F, 6.0F, 3.0F};

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(undecedent::player_fits_at(world, Vec3{5, 3, 5}, config), "player should fit inside a room");
        expect(!undecedent::player_fits_at(world, Vec3{12, 3, 5}, config), "player should not fit outside a room");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        PlayerPhysicsState state{Vec3{5, 3, 5}, -1};
        state = undecedent::move_player(world, state, Vec3{20, 0, 0}, config);
        expect(state.position.x == 5.0F, "blocked horizontal movement should be rejected");
        state = undecedent::move_player(world, state, Vec3{0, 0, 1}, config);
        expect(state.position.z == 6.0F, "unblocked movement should still be accepted after a blocked axis");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors = add(std::move(sectors), loop({{10, 0}, {20, 0}, {20, 10}, {10, 10}}));
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        PlayerPhysicsState state{Vec3{9, 3, 5}, -1};
        state = undecedent::move_player(world, state, Vec3{2, 0, 0}, config);
        expect(state.position.x == 11.0F, "player should cross an adjacent sector portal");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().height = 8.0F;
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        PlayerPhysicsState state{Vec3{5, 3, 5}, -1};
        state = undecedent::move_player(world, state, Vec3{0, 10, 0}, config);
        expect(state.position.y == 3.0F, "vertical movement into a ceiling should be rejected");
        state = undecedent::move_player(world, state, Vec3{0, -2, 0}, config);
        expect(state.position.y == 3.0F, "vertical movement below the floor should be rejected");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 1;
        undecedent::ensure_displacement_samples(sectors.front(), undecedent::SectorSurfaceKind::Floor);
        for (undecedent::SectorDisplacementSample& sample : sectors.front().floor_displacement.samples) {
            sample.offset = sample.position.x;
        }
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        PlayerPhysicsState state{Vec3{1, 4, 5}, -1};
        state = undecedent::move_player(world, state, Vec3{2, 0, 0}, PlayerPhysicsConfig{0.0F, 6.0F, 3.0F, 18.0F});
        expect(state.position.x == 3.0F, "player should move horizontally on a sloped floor");
        expect(state.position.y > 4.0F, "player should step up to the sampled sloped floor");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {12, 0}, {12, 10}, {0, 10}}));
        sectors.front().floor_displacement.resolution = 1;
        undecedent::ensure_displacement_samples(sectors.front(), undecedent::SectorSurfaceKind::Floor);
        for (undecedent::SectorDisplacementSample& sample : sectors.front().floor_displacement.samples) {
            sample.offset = sample.position.x;
        }
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        PlayerPhysicsState state{Vec3{3, 7, 5}, -1};
        state = undecedent::move_player(
            world,
            state,
            Vec3{2, 0, 0},
            PlayerPhysicsConfig{2.0F, 8.0F, 4.0F, 18.0F, 0.5F}
        );
        expect(state.position.x == 5.0F, "full-radius body should still move across a sloped floor");
        expect_near(state.position.y, 9.5F, 0.01F, "floor support should come from the foot contact radius");
    }

    {
        std::vector<SectorPlane> sectors = add({}, loop({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        sectors.front().height = 12.0F;
        sectors.front().ceiling_displacement.resolution = 1;
        undecedent::ensure_displacement_samples(sectors.front(), undecedent::SectorSurfaceKind::Ceiling);
        for (undecedent::SectorDisplacementSample& sample : sectors.front().ceiling_displacement.samples) {
            sample.offset = -8.0F;
        }
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        expect(!undecedent::player_fits_at(world, Vec3{5, 3, 5}, config), "low displaced ceiling should block player");
    }

    {
        std::vector<SectorPlane> sectors{
            make_sector(loop({{0, 0}, {100, 0}, {100, 100}, {0, 100}}), 0.0F),
            make_sector(loop({{40, 40}, {60, 40}, {60, 60}, {40, 60}}), 24.0F),
        };
        const undecedent::RuntimeWorld world = undecedent::build_runtime_world(sectors);
        const PlayerPhysicsConfig nested_config{4.0F, 56.0F, 48.0F};
        expect(
            undecedent::player_fits_at(world, Vec3{50, 72, 50}, nested_config),
            "player should fit on an elevated nested sector floor"
        );
        expect(
            !undecedent::player_fits_at(world, Vec3{50, 71, 50}, nested_config),
            "nested sector floor should not be bypassed by the enclosing sector"
        );

        PlayerPhysicsState state{Vec3{50, 72, 50}, -1};
        state = undecedent::move_player(world, state, Vec3{0, -10, 0}, nested_config);
        expect_near(state.position.y, 72.0F, 0.01F, "player should not fall through a nested sector floor");

        state = undecedent::move_player(world, state, Vec3{20, 0, 0}, nested_config);
        expect_near(state.position.x, 50.0F, 0.01F, "nested sector walls should block movement into the enclosing sector");
    }

    return EXIT_SUCCESS;
}
