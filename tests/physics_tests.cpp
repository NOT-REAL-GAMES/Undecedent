#include "undecedent/csg.hpp"
#include "undecedent/displacement.hpp"
#include "undecedent/physics.hpp"

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

    return EXIT_SUCCESS;
}
