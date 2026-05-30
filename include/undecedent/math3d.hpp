#pragma once

#include "undecedent/geometry.hpp"
#include "undecedent/runtime_geometry.hpp"

namespace undecedent {

Vec3 add_vec3(Vec3 a, Vec3 b);
Vec3 sub_vec3(Vec3 a, Vec3 b);
Vec3 mul_vec3(Vec3 a, float value);
float dot_vec3(Vec3 a, Vec3 b);
Vec3 cross_vec3(Vec3 a, Vec3 b);
Vec3 normalize_vec3(Vec3 value);

bool ray_triangle_intersection(Vec3 origin, Vec3 direction, const RuntimeTriangle& triangle, float& out_t);

} // namespace undecedent
