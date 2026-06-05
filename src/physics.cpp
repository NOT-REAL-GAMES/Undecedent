#include "undecedent/physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

float loop_area(const PolygonLoop& loop) {
    if (loop.vertices.size() < 3) {
        return 0.0F;
    }

    float area = 0.0F;
    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const Vec2 a = loop.vertices[i];
        const Vec2 b = loop.vertices[(i + 1) % loop.vertices.size()];
        area += (a.x * b.y) - (b.x * a.y);
    }
    return std::abs(area) * 0.5F;
}

float sector_area(const RuntimeSector& sector) {
    float area = loop_area(sector.outer);
    for (const PolygonLoop& hole : sector.holes) {
        area -= loop_area(hole);
    }
    return std::max(area, 0.0F);
}

bool body_overlaps_sector_volume(
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
    return head >= floor - kPhysicsEpsilon && feet <= ceiling + kPhysicsEpsilon;
}

int body_base_sector(
    const RuntimeWorld& world,
    const Vec3 eye_position,
    const PlayerPhysicsConfig config
) {
    const float height = std::max(config.height, 1.0F);
    const float eye_height = std::clamp(config.eye_height, 0.0F, height);
    const float feet = eye_position.y - eye_height;
    const float head = feet + height;
    const Vec2 sample{eye_position.x, eye_position.z};

    int best_sector = -1;
    float best_area = 0.0F;
    const auto consider = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (!body_overlaps_sector_volume(sector, sample, feet, head)) {
            return;
        }
        const float area = sector_area(sector);
        if (best_sector < 0 || area < best_area) {
            best_sector = sector_id;
            best_area = area;
        }
    };

    const auto candidates = sectors_near_point(world, eye_position);
    for (const int sector_id : candidates) {
        consider(sector_id);
    }
    if (best_sector >= 0) {
        return best_sector;
    }

    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        consider(static_cast<int>(sector_id));
    }
    return best_sector;
}

std::vector<char> neighbor_connected_sector_mask(const RuntimeWorld& world, const int start_sector) {
    std::vector<char> allowed(world.sectors.size(), 0);
    if (start_sector < 0 || start_sector >= static_cast<int>(world.sectors.size())) {
        return allowed;
    }

    std::vector<int> stack{start_sector};
    allowed[static_cast<std::size_t>(start_sector)] = 1;
    while (!stack.empty()) {
        const int sector_id = stack.back();
        stack.pop_back();
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        for (const int neighbor : sector.neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<int>(world.sectors.size())) {
                continue;
            }
            if (allowed[static_cast<std::size_t>(neighbor)] != 0) {
                continue;
            }
            allowed[static_cast<std::size_t>(neighbor)] = 1;
            stack.push_back(neighbor);
        }
    }
    return allowed;
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

bool sample_has_body_clearance(
    const RuntimeSector& sector,
    const Vec2 sample,
    const float head
) {
    if (!point_in_sector_2d(sector, sample)) {
        return false;
    }
    const float ceiling = runtime_ceiling_height_at(sector, sample);
    return head <= ceiling + kPhysicsEpsilon;
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

bool smaller_disallowed_sector_overlaps(
    const RuntimeWorld& world,
    const std::vector<char>& allowed_sectors,
    const Vec2 sample,
    const float feet,
    const float head,
    const float allowed_area
) {
    for (std::size_t sector_id = 0; sector_id < world.sectors.size(); ++sector_id) {
        if (allowed_sectors[sector_id] != 0) {
            continue;
        }
        const RuntimeSector& sector = world.sectors[sector_id];
        if (sector_area(sector) >= allowed_area - kPhysicsEpsilon) {
            continue;
        }
        if (body_overlaps_sector_volume(sector, sample, feet, head)) {
            return true;
        }
    }
    return false;
}

bool sample_fits_allowed_sectors(
    const RuntimeWorld& world,
    const std::vector<int>& candidates,
    const std::vector<char>& allowed_sectors,
    const Vec2 sample,
    const float feet,
    const float head
) {
    bool found_allowed_fit = false;
    float best_allowed_area = 0.0F;

    auto visit = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        if (allowed_sectors[static_cast<std::size_t>(sector_id)] == 0) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (!sample_fits_sector(sector, sample, feet, head)) {
            return;
        }
        const float area = sector_area(sector);
        if (!found_allowed_fit || area < best_allowed_area) {
            found_allowed_fit = true;
            best_allowed_area = area;
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

    return found_allowed_fit &&
        !smaller_disallowed_sector_overlaps(world, allowed_sectors, sample, feet, head, best_allowed_area);
}

bool sample_has_body_clearance_allowed_sectors(
    const RuntimeWorld& world,
    const std::vector<int>& candidates,
    const std::vector<char>& allowed_sectors,
    const Vec2 sample,
    const float feet,
    const float head
) {
    bool found_allowed_fit = false;
    float best_allowed_area = 0.0F;

    auto visit = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        if (allowed_sectors[static_cast<std::size_t>(sector_id)] == 0) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (!sample_has_body_clearance(sector, sample, head)) {
            return;
        }
        const float area = sector_area(sector);
        if (!found_allowed_fit || area < best_allowed_area) {
            found_allowed_fit = true;
            best_allowed_area = area;
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

    return found_allowed_fit &&
        !smaller_disallowed_sector_overlaps(world, allowed_sectors, sample, feet, head, best_allowed_area);
}

bool sample_floor_height_allowed_sectors(
    const RuntimeWorld& world,
    const std::vector<int>& candidates,
    const std::vector<char>& allowed_sectors,
    const Vec2 sample,
    const float feet,
    const float head,
    float& floor_height
) {
    bool found_floor = false;
    float best_allowed_area = 0.0F;
    auto visit = [&](const int sector_id) {
        if (sector_id < 0 || sector_id >= static_cast<int>(world.sectors.size())) {
            return;
        }
        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(sector_id)];
        if (allowed_sectors[static_cast<std::size_t>(sector_id)] == 0) {
            return;
        }
        if (!point_in_sector_2d(sector, sample)) {
            return;
        }
        const float sector_floor = runtime_floor_height_at(sector, sample);
        if (!found_floor || sector_floor > floor_height) {
            floor_height = sector_floor;
            found_floor = true;
        }
        const float area = sector_area(sector);
        if (best_allowed_area == 0.0F || area < best_allowed_area) {
            best_allowed_area = area;
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
    return found_floor &&
        !smaller_disallowed_sector_overlaps(world, allowed_sectors, sample, feet, head, best_allowed_area);
}

float foot_contact_radius_for_config(const PlayerPhysicsConfig config) {
    const float radius = std::max(config.radius, 0.0F);
    return std::clamp(config.foot_contact_radius, 0.0F, radius);
}

bool support_floor_height(
    const RuntimeWorld& world,
    const Vec3 eye_position,
    const PlayerPhysicsConfig config,
    const std::vector<char>& allowed_sectors,
    float& floor_height
) {
    const float radius = foot_contact_radius_for_config(config);
    const RuntimeBounds2 bounds{
        eye_position.x - radius,
        eye_position.z - radius,
        eye_position.x + radius,
        eye_position.z + radius,
    };
    const std::vector<int> candidates = sectors_in_bounds(world, bounds);
    const float height = std::max(config.height, 1.0F);
    const float eye_height = std::clamp(config.eye_height, 0.0F, height);
    const float feet = eye_position.y - eye_height;
    const float head = feet + height;
    bool found_floor = false;
    for (const Vec2 sample : collision_samples(eye_position, radius)) {
        float sample_floor = 0.0F;
        if (!sample_floor_height_allowed_sectors(world, candidates, allowed_sectors, sample, feet, head, sample_floor)) {
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
    state.sector_id = body_base_sector(world, eye_position, config);
    return state;
}

bool player_fits_at_with_allowed(
    const RuntimeWorld& world,
    const Vec3 eye_position,
    const PlayerPhysicsConfig config,
    const std::vector<char>& allowed_sectors
) {
    if (world.sectors.empty() || allowed_sectors.size() != world.sectors.size()) {
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
        if (!sample_has_body_clearance_allowed_sectors(world, candidates, allowed_sectors, sample, feet, head)) {
            return false;
        }
    }

    const float foot_radius = foot_contact_radius_for_config(config);
    for (const Vec2 sample : collision_samples(eye_position, foot_radius)) {
        if (!sample_fits_allowed_sectors(world, candidates, allowed_sectors, sample, feet, head)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool player_fits_at(const RuntimeWorld& world, const Vec3 eye_position, const PlayerPhysicsConfig config) {
    if (world.sectors.empty()) {
        return false;
    }

    const int base_sector = body_base_sector(world, eye_position, config);
    if (base_sector < 0) {
        return false;
    }
    return player_fits_at_with_allowed(
        world,
        eye_position,
        config,
        neighbor_connected_sector_mask(world, base_sector)
    );
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
        const std::vector<char> allowed_sectors =
            neighbor_connected_sector_mask(world, body_base_sector(world, state.position, config));
        const Vec3 candidate{
            state.position.x + move_delta.x,
            state.position.y + move_delta.y,
            state.position.z + move_delta.z,
        };
        if (player_fits_at_with_allowed(world, candidate, config, allowed_sectors)) {
            state.position = candidate;
            return;
        }

        if (move_delta.y == 0.0F && (move_delta.x != 0.0F || move_delta.z != 0.0F)) {
            float floor_height = 0.0F;
            if (!support_floor_height(world, candidate, config, allowed_sectors, floor_height)) {
                return;
            }
            const float height = std::max(config.height, 1.0F);
            const float eye_height = std::clamp(config.eye_height, 0.0F, height);
            const float current_feet = state.position.y - eye_height;
            if (std::abs(floor_height - current_feet) > std::max(config.max_step_height, 0.0F) + kPhysicsEpsilon) {
                return;
            }
            const Vec3 stepped{candidate.x, floor_height + eye_height, candidate.z};
            if (player_fits_at_with_allowed(world, stepped, config, allowed_sectors)) {
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
