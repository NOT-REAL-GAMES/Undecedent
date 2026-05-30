#include "undecedent/math3d.hpp"

#include <cmath>

namespace undecedent {

Vec3 add_vec3(const Vec3 a, const Vec3 b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub_vec3(const Vec3 a, const Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul_vec3(const Vec3 a, const float value) {
    return Vec3{a.x * value, a.y * value, a.z * value};
}

float dot_vec3(const Vec3 a, const Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

Vec3 cross_vec3(const Vec3 a, const Vec3 b) {
    return Vec3{
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
}

Vec3 normalize_vec3(const Vec3 value) {
    const float length = std::sqrt(dot_vec3(value, value));
    if (length <= 0.00001F) {
        return Vec3{0.0F, 0.0F, 0.0F};
    }
    return mul_vec3(value, 1.0F / length);
}

bool ray_triangle_intersection(
    const Vec3 origin,
    const Vec3 direction,
    const RuntimeTriangle& triangle,
    float& out_t
) {
    constexpr float epsilon = 0.00001F;
    const Vec3 edge1 = sub_vec3(triangle.b, triangle.a);
    const Vec3 edge2 = sub_vec3(triangle.c, triangle.a);
    const Vec3 h = cross_vec3(direction, edge2);
    const float det = dot_vec3(edge1, h);
    if (det > -epsilon && det < epsilon) {
        return false;
    }

    const float inv_det = 1.0F / det;
    const Vec3 s = sub_vec3(origin, triangle.a);
    const float u = inv_det * dot_vec3(s, h);
    if (u < 0.0F || u > 1.0F) {
        return false;
    }

    const Vec3 q = cross_vec3(s, edge1);
    const float v = inv_det * dot_vec3(direction, q);
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }

    const float t = inv_det * dot_vec3(edge2, q);
    if (t <= epsilon) {
        return false;
    }
    out_t = t;
    return true;
}

} // namespace undecedent
