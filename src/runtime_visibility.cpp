#include "undecedent/runtime_visibility.hpp"

#include "undecedent/runtime_render.hpp"

#include <algorithm>
#include <cmath>

namespace undecedent {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kClipEpsilon = 0.0001F;
constexpr float kPortalClipPadding = 0.015F;

struct ClipRect {
    float min_x = -1.0F;
    float min_y = -1.0F;
    float max_x = 1.0F;
    float max_y = 1.0F;
};

struct CameraBasis {
    Vec3 position;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

struct PortalVisit {
    int sector_id = -1;
    ClipRect clip;
};

float dot(const Vec3 a, const Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

Vec3 cross(const Vec3 a, const Vec3 b) {
    return Vec3{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

Vec3 subtract(const Vec3 a, const Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 normalize(const Vec3 value) {
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.000001F) {
        return Vec3{0.0F, 1.0F, 0.0F};
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

CameraBasis camera_basis(const GameCamera& camera) {
    const float cos_pitch = std::cos(camera.pitch);
    const Vec3 forward{
        -std::sin(camera.yaw) * cos_pitch,
        std::sin(camera.pitch),
        -std::cos(camera.yaw) * cos_pitch,
    };
    const Vec3 right{std::cos(camera.yaw), 0.0F, -std::sin(camera.yaw)};
    return CameraBasis{
        Vec3{camera.x, camera.y, camera.z},
        normalize(right),
        normalize(cross(right, forward)),
        normalize(forward),
    };
}

bool intersect_rects(const ClipRect a, const ClipRect b, ClipRect& out) {
    out = ClipRect{
        std::max(a.min_x, b.min_x),
        std::max(a.min_y, b.min_y),
        std::min(a.max_x, b.max_x),
        std::min(a.max_y, b.max_y),
    };
    return out.max_x > out.min_x + kClipEpsilon && out.max_y > out.min_y + kClipEpsilon;
}

ClipRect expanded_rect(const ClipRect rect, const float amount) {
    return ClipRect{
        rect.min_x - amount,
        rect.min_y - amount,
        rect.max_x + amount,
        rect.max_y + amount,
    };
}

ClipRect union_rects(const ClipRect a, const ClipRect b) {
    return ClipRect{
        std::min(a.min_x, b.min_x),
        std::min(a.min_y, b.min_y),
        std::max(a.max_x, b.max_x),
        std::max(a.max_y, b.max_y),
    };
}

bool rect_contains(const ClipRect outer, const ClipRect inner) {
    return inner.min_x >= outer.min_x - kClipEpsilon &&
        inner.min_y >= outer.min_y - kClipEpsilon &&
        inner.max_x <= outer.max_x + kClipEpsilon &&
        inner.max_y <= outer.max_y + kClipEpsilon;
}

bool project_point(
    const Vec3 point,
    const CameraBasis& basis,
    const float tan_half_fov_y,
    const float aspect,
    const float near_plane,
    float& x,
    float& y
) {
    const Vec3 relative = subtract(point, basis.position);
    const float depth = dot(relative, basis.forward);
    if (depth <= near_plane) {
        return false;
    }

    x = dot(relative, basis.right) / (depth * tan_half_fov_y * aspect);
    y = dot(relative, basis.up) / (depth * tan_half_fov_y);
    return true;
}

bool project_portal_rect(
    const RuntimePortal& portal,
    const CameraBasis& basis,
    const GameRenderConfig& config,
    const float aspect,
    ClipRect& out
) {
    const float fov_radians = config.fov_y_degrees * kPi / 180.0F;
    const float tan_half_fov_y = std::tan(fov_radians * 0.5F);
    const float near_plane = std::max(0.01F, config.near_plane);
    const Vec3 corners[] = {
        Vec3{portal.a.x, portal.bottom, portal.a.y},
        Vec3{portal.b.x, portal.bottom, portal.b.y},
        Vec3{portal.b.x, portal.top, portal.b.y},
        Vec3{portal.a.x, portal.top, portal.a.y},
    };

    int projected_count = 0;
    int behind_count = 0;
    ClipRect projected{
        1000000.0F,
        1000000.0F,
        -1000000.0F,
        -1000000.0F,
    };
    for (const Vec3 corner : corners) {
        float x = 0.0F;
        float y = 0.0F;
        if (!project_point(corner, basis, tan_half_fov_y, aspect, near_plane, x, y)) {
            ++behind_count;
            continue;
        }

        ++projected_count;
        projected.min_x = std::min(projected.min_x, x);
        projected.min_y = std::min(projected.min_y, y);
        projected.max_x = std::max(projected.max_x, x);
        projected.max_y = std::max(projected.max_y, y);
    }

    if (projected_count == 0) {
        return false;
    }

    if (behind_count > 0) {
        out = ClipRect{};
        return true;
    }

    const ClipRect screen{};
    return intersect_rects(screen, expanded_rect(projected, kPortalClipPadding), out);
}

} // namespace

std::vector<int> visible_sectors_from_camera(
    const RuntimeWorld& world,
    const GameCamera& camera,
    const GameRenderConfig& config,
    const int width,
    const int height
) {
    const int start_sector = sector_at_point(world, Vec3{camera.x, camera.y, camera.z});
    if (start_sector < 0 || start_sector >= static_cast<int>(world.sectors.size()) || width <= 0 || height <= 0) {
        return {};
    }

    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
    const CameraBasis basis = camera_basis(camera);
    std::vector<ClipRect> best_clip(world.sectors.size());
    std::vector<char> has_best_clip(world.sectors.size(), 0);
    std::vector<char> visible_mask(world.sectors.size(), 0);
    std::vector<int> visible;
    std::vector<PortalVisit> stack;

    const ClipRect full_screen{};
    best_clip[static_cast<std::size_t>(start_sector)] = full_screen;
    has_best_clip[static_cast<std::size_t>(start_sector)] = 1;
    stack.push_back(PortalVisit{start_sector, full_screen});

    while (!stack.empty()) {
        const PortalVisit visit = stack.back();
        stack.pop_back();
        if (visit.sector_id < 0 || visit.sector_id >= static_cast<int>(world.sectors.size())) {
            continue;
        }

        if (!visible_mask[static_cast<std::size_t>(visit.sector_id)]) {
            visible_mask[static_cast<std::size_t>(visit.sector_id)] = 1;
            visible.push_back(visit.sector_id);
        }

        const RuntimeSector& sector = world.sectors[static_cast<std::size_t>(visit.sector_id)];
        for (const int portal_id : sector.portal_ids) {
            if (portal_id < 0 || portal_id >= static_cast<int>(world.portals.size())) {
                continue;
            }

            const RuntimePortal& portal = world.portals[static_cast<std::size_t>(portal_id)];
            if (portal.to_sector < 0 || portal.to_sector >= static_cast<int>(world.sectors.size())) {
                continue;
            }

            ClipRect portal_rect{};
            if (!project_portal_rect(portal, basis, config, aspect, portal_rect)) {
                continue;
            }

            ClipRect next_clip{};
            if (!intersect_rects(visit.clip, portal_rect, next_clip)) {
                continue;
            }

            const std::size_t to_index = static_cast<std::size_t>(portal.to_sector);
            if (has_best_clip[to_index] && rect_contains(best_clip[to_index], next_clip)) {
                continue;
            }

            best_clip[to_index] = has_best_clip[to_index] ? union_rects(best_clip[to_index], next_clip) : next_clip;
            has_best_clip[to_index] = 1;
            stack.push_back(PortalVisit{portal.to_sector, next_clip});
        }
    }

    std::sort(visible.begin(), visible.end());
    return visible;
}

} // namespace undecedent
