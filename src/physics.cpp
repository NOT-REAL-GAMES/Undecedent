#include "undecedent/physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace undecedent {
namespace {

constexpr float kPhysicsEpsilon = 0.001F;

bool point_on_segment(const Vec2 a, const Vec2 b, const Vec2 point) {
    const float cross = ((b.x - a.x) * (point.y - a.y)) - ((b.y - a.y) * (point.x - a.x));
    if (std::abs(cross) > kPhysicsEpsilon) {
        return false;
    }

    return point.x >= std::min(a.x, b.x) - kPhysicsEpsilon &&
        point.x <= std::max(a.x, b.x) + kPhysicsEpsilon &&
        point.y >= std::min(a.y, b.y) - kPhysicsEpsilon &&
        point.y <= std::max(a.y, b.y) + kPhysicsEpsilon;
}

bool point_in_loop_or_on(const PolygonLoop& loop, const Vec2 point) {
    if (loop.vertices.size() < 3) {
        return false;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        if (point_on_segment(loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()], point)) {
            return true;
        }
    }

    bool inside = false;
    for (std::size_t i = 0, j = loop.vertices.size() - 1; i < loop.vertices.size(); j = i++) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[j];
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
            (point.x < ((b.x - a.x) * (point.y - a.y) / (b.y - a.y)) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_sector_2d(const RuntimeSector& sector, const Vec2 point) {
    if (point.x < sector.bounds.min_x || point.x > sector.bounds.max_x ||
        point.y < sector.bounds.min_y || point.y > sector.bounds.max_y) {
        return false;
    }
    if (!point_in_loop_or_on(sector.outer, point)) {
        return false;
    }
    return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
        return point_in_loop_or_on(hole, point);
    });
}

bool sample_fits_sector(
    const RuntimeSector& sector,
    const Vec2 sample,
    const float feet,
    const float head
) {
    if (!point_in_sector_2d(sector, sample)) {
        return false;
    }
    const float floor = runtime_floor_height_at(sector, sample);
    const float ceiling = runtime_ceiling_height_at(sector, sample);
    return feet >= floor - kPhysicsEpsilon && head <= ceiling + kPhysicsEpsilon;
}

std::array<Vec2, 9> collision_samples(const Vec3 eye_position, const float radius) {
    constexpr float diagonal = 0.70710678118F;
    const Vec2 center{eye_position.x, eye_position.z};
    return {{
        center,
        {center.x + radius, center.y},
        {center.x - radius, center.y},
        {center.x, center.y + radius},
        {center.x, center.y - radius},
        {center.x + radius * diagonal, center.y + radius * diagonal},
        {center.x - radius * diagonal, center.y + radius * diagonal},
        {center.x + radius * diagonal, center.y - radius * diagonal},
        {center.x - radius * diagonal, center.y - radius * diagonal},
    }};
}

bool sample_fits_any_sector(
    const RuntimeWorld& world,
    const std::vector<int>& candidates,
    const Vec2 sample,
    const float feet,
    const float head
) {
    for (const int sector_id : candidates) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            continue;
        }
        if (sample_fits_sector(world.sectors[static_cast<std::size_t>(sector_id)], sample, feet, head)) {
            return true;
        }
    }

    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        if (std::find(candidates.begin(), candidates.end(), static_cast<int>(sector_id)) != candidates.end()) {
            continue;
        }
        if (sample_fits_sector(world.sectors[sector_id], sample, feet, head)) {
            return true;
        }
    }
    return false;
}

bool sample_floor_height_any_sector(
    const RuntimeWorld& world,
    const std::vector<int>& candidates,
    const Vec2 sample,
    float& floor_height
) {
    bool found_floor = false;
    auto visit = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (!point_in_sector_2d(sector, sample)) {
            return;
        }
        const float sector_floor = runtime_floor_height_at(sector, sample);
        if (!found_floor || sector_floor > floor_height) {
            floor_height = sector_floor;
            found_floor = true;
        }
    };

    for (const int sector_id : candidates) {
        visit(sector_id);
    }
    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        if (std::find(candidates.begin(), candidates.end(), static_cast<int>(sector_id)) != candidates.end()) {
            continue;
        }
        visit(static_cast<int>(sector_id));
    }
    return found_floor;
}

bool support_floor_height(
    const RuntimeWorld& world,
    const Vec3 eye_position,
    const PlayerPhysicsConfig config,
    float& floor_height
) {
    const float radius = std::max(config.radius, 0.0F);
    const RuntimeBounds2 bounds{
        eye_position.x - radius,
        eye_position.z - radius,
        eye_position.x + radius,
        eye_position.z + radius,
    };
    const std::vector<int> candidates = sectors_in_bounds(world, bounds);
    bool found_floor = false;
    for (const Vec2 sample : collision_samples(eye_position, radius)) {
        float sample_floor = 0.0F;
        if (!sample_floor_height_any_sector(world, candidates, sample, sample_floor)) {
            return false;
        }
        if (!found_floor || sample_floor > floor_height) {
            floor_height = sample_floor;
            found_floor = true;
        }
    }
    return found_floor;
}

PlayerPhysicsState state_for_position(
    const RuntimeWorld& world,
    const Vec3 eye_position,
    const PlayerPhysicsConfig config
) {
    PlayerPhysicsState state{eye_position, -1};
    state.sector_id = sector_at_point(world, eye_position);
    if (state.sector_id < 0) {
        const Vec3 feet_point{eye_position.x, eye_position.y - config.eye_height, eye_position.z};
        state.sector_id = sector_at_point(world, feet_point);
    }
    return state;
}

} // namespace

bool player_fits_at(const RuntimeWorld& world, const Vec3 eye_position, const PlayerPhysicsConfig config) {
    if (world.sectors.empty()) {
        return false;
    }

    const float radius = std::max(config.radius, 0.0F);
    const float height = std::max(config.height, 1.0F);
    const float eye_height = std::clamp(config.eye_height, 0.0F, height);
    const float feet = eye_position.y - eye_height;
    const float head = feet + height;
    const RuntimeBounds2 bounds{
        eye_position.x - radius,
        eye_position.z - radius,
        eye_position.x + radius,
        eye_position.z + radius,
    };
    const std::vector<int> candidates = sectors_in_bounds(world, bounds);

    for (const Vec2 sample : collision_samples(eye_position, radius)) {
        if (!sample_fits_any_sector(world, candidates, sample, feet, head)) {
            return false;
        }
    }
    return true;
}

PlayerPhysicsState move_player(
    const RuntimeWorld& world,
    PlayerPhysicsState state,
    const Vec3 delta,
    const PlayerPhysicsConfig config
) {
    if (world.sectors.empty()) {
        return state_for_position(world, state.position, config);
    }

    auto try_move = [&](const Vec3 move_delta) {
        const Vec3 candidate{
            state.position.x + move_delta.x,
            state.position.y + move_delta.y,
            state.position.z + move_delta.z,
        };
        if (player_fits_at(world, candidate, config)) {
            state.position = candidate;
            return;
        }

        if (move_delta.y == 0.0F && (move_delta.x != 0.0F || move_delta.z != 0.0F)) {
            float floor_height = 0.0F;
            if (!support_floor_height(world, candidate, config, floor_height)) {
                return;
            }
            const float height = std::max(config.height, 1.0F);
            const float eye_height = std::clamp(config.eye_height, 0.0F, height);
            const float current_feet = state.position.y - eye_height;
            if (std::abs(floor_height - current_feet) > std::max(config.max_step_height, 0.0F) + kPhysicsEpsilon) {
                return;
            }
            const Vec3 stepped{candidate.x, floor_height + eye_height, candidate.z};
            if (player_fits_at(world, stepped, config)) {
                state.position = stepped;
            }
        }
    };

    try_move(Vec3{delta.x, 0.0F, 0.0F});
    try_move(Vec3{0.0F, 0.0F, delta.z});
    try_move(Vec3{0.0F, delta.y, 0.0F});
    return state_for_position(world, state.position, config);
}

} // namespace undecedent
