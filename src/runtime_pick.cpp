#include "undecedent/runtime_pick.hpp"

#include "undecedent/math3d.hpp"
#include "undecedent/runtime_render_cache.hpp"

#include <cmath>
#include <limits>

namespace undecedent {

Vec3 camera_forward(const GameCamera& camera) {
    const float forward_flat = std::cos(camera.pitch);
    return Vec3{
        -std::sin(camera.yaw) * forward_flat,
        std::sin(camera.pitch),
        -std::cos(camera.yaw) * forward_flat,
    };
}

Vec3 camera_ray_direction(
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const float fov_y_degrees
) {
    const float ndc_x = (2.0F * screen_x / static_cast<float>(width)) - 1.0F;
    const float ndc_y = 1.0F - (2.0F * screen_y / static_cast<float>(height));
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float tan_y = std::tan((fov_y_degrees * 3.14159265358979323846F / 180.0F) * 0.5F);
    const Vec3 forward = normalize_vec3(camera_forward(camera));
    const Vec3 right = Vec3{std::cos(camera.yaw), 0.0F, -std::sin(camera.yaw)};
    const Vec3 up = normalize_vec3(cross_vec3(right, forward));
    return normalize_vec3(add_vec3(
        add_vec3(forward, mul_vec3(right, ndc_x * tan_y * aspect)),
        mul_vec3(up, ndc_y * tan_y)
    ));
}

SurfacePick pick_runtime_surface(
    const RuntimeWorld& world,
    const GameCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y,
    const GameRenderConfig& config
) {
    if (width <= 0 || height <= 0) {
        return {};
    }

    const Vec3 direction = camera_ray_direction(camera, width, height, screen_x, screen_y, config.fov_y_degrees);
    const Vec3 origin{camera.x, camera.y, camera.z};

    SurfacePick best{};
    best.distance = std::numeric_limits<float>::max();
    for (const RuntimeTaggedTriangle& tagged : world.triangles) {
        float t = 0.0F;
        if (tagged.sector_id < 0 || !ray_triangle_intersection(origin, direction, tagged.triangle, t)) {
            continue;
        }
        if (t < best.distance) {
            best.hit = true;
            best.distance = t;
            best.point = add_vec3(origin, mul_vec3(direction, t));
            best.normal = runtime_triangle_lighting_normal(tagged.triangle);
            best.sector_id = tagged.sector_id;
            best.material_id = tagged.material_id;
            best.surface = tagged.surface;
        }
    }

    return best;
}

} // namespace undecedent
