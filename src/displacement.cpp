#include "undecedent/displacement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace undecedent {
namespace {

constexpr float kDisplacementEpsilon = 0.001F;
constexpr std::int64_t kDisplacementScale = 1024;

struct SampleKey {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

bool operator<(const SampleKey& a, const SampleKey& b) {
    if (a.x != b.x) {
        return a.x < b.x;
    }
    return a.y < b.y;
}

SampleKey sample_key(const Vec2 point) {
    return SampleKey{
        static_cast<std::int64_t>(std::llround(static_cast<double>(point.x) * kDisplacementScale)),
        static_cast<std::int64_t>(std::llround(static_cast<double>(point.y) * kDisplacementScale)),
    };
}

Vec2 add_vec2(const Vec2 a, const Vec2 b) {
    return Vec2{a.x + b.x, a.y + b.y};
}

Vec2 sub_vec2(const Vec2 a, const Vec2 b) {
    return Vec2{a.x - b.x, a.y - b.y};
}

Vec2 mul_vec2(const Vec2 point, const float scale) {
    return Vec2{point.x * scale, point.y * scale};
}

float cross_2d(const Vec2 a, const Vec2 b) {
    return (a.x * b.y) - (a.y * b.x);
}

float distance_squared(const Vec2 a, const Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool barycentric_weights(
    const Vec2 point,
    const Vec2 a,
    const Vec2 b,
    const Vec2 c,
    float& wa,
    float& wb,
    float& wc
) {
    const Vec2 v0 = sub_vec2(b, a);
    const Vec2 v1 = sub_vec2(c, a);
    const Vec2 v2 = sub_vec2(point, a);
    const float denom = cross_2d(v0, v1);
    if (std::abs(denom) <= kDisplacementEpsilon) {
        return false;
    }

    wb = cross_2d(v2, v1) / denom;
    wc = cross_2d(v0, v2) / denom;
    wa = 1.0F - wb - wc;
    return wa >= -kDisplacementEpsilon && wb >= -kDisplacementEpsilon && wc >= -kDisplacementEpsilon &&
        wa <= 1.0F + kDisplacementEpsilon && wb <= 1.0F + kDisplacementEpsilon && wc <= 1.0F + kDisplacementEpsilon;
}

bool point_in_triangle(const Vec2 point, const Triangle& triangle) {
    float wa = 0.0F;
    float wb = 0.0F;
    float wc = 0.0F;
    return barycentric_weights(point, triangle.a, triangle.b, triangle.c, wa, wb, wc);
}

bool point_in_sector_triangulation(const SectorPlane& sector, const Vec2 point) {
    return std::any_of(sector.triangles.begin(), sector.triangles.end(), [point](const Triangle& triangle) {
        return point_in_triangle(point, triangle);
    });
}

bool point_on_segment(const Vec2 a, const Vec2 b, const Vec2 point) {
    const float cross = ((b.x - a.x) * (point.y - a.y)) - ((b.y - a.y) * (point.x - a.x));
    if (std::abs(cross) > kDisplacementEpsilon) {
        return false;
    }

    return point.x >= std::min(a.x, b.x) - kDisplacementEpsilon &&
        point.x <= std::max(a.x, b.x) + kDisplacementEpsilon &&
        point.y >= std::min(a.y, b.y) - kDisplacementEpsilon &&
        point.y <= std::max(a.y, b.y) + kDisplacementEpsilon;
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

bool point_in_loop_strict(const PolygonLoop& loop, const Vec2 point) {
    if (loop.vertices.size() < 3) {
        return false;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        if (point_on_segment(loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()], point)) {
            return false;
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

bool point_in_sector_source_shape(const SectorPlane& sector, const Vec2 point) {
    if (!point_in_loop_strict(sector.outer, point)) {
        return false;
    }
    return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
        return point_in_loop_or_on(hole, point);
    });
}

Vec2 subdivided_point(const Triangle& triangle, const int i, const int j, const int resolution) {
    const float u = static_cast<float>(i) / static_cast<float>(resolution);
    const float v = static_cast<float>(j) / static_cast<float>(resolution);
    return add_vec2(
        triangle.a,
        add_vec2(mul_vec2(sub_vec2(triangle.b, triangle.a), u), mul_vec2(sub_vec2(triangle.c, triangle.a), v))
    );
}

std::map<SampleKey, float> sample_offsets_by_key(const SectorSurfaceDisplacement& displacement) {
    std::map<SampleKey, float> offsets;
    if (!displacement.enabled) {
        return offsets;
    }
    for (const SectorDisplacementSample& sample : displacement.samples) {
        if (std::isfinite(sample.position.x) && std::isfinite(sample.position.y) && std::isfinite(sample.offset)) {
            offsets[sample_key(sample.position)] = sample.offset;
        }
    }
    return offsets;
}

float offset_at_generated_point(const std::map<SampleKey, float>& offsets, const Vec2 point) {
    const auto found = offsets.find(sample_key(point));
    if (found == offsets.end()) {
        return 0.0F;
    }
    return found->second;
}

SurfaceSampleVertex sample_vertex(
    const Vec2 position,
    const float offset,
    const float base_height
) {
    return SurfaceSampleVertex{position, offset, base_height + offset};
}

void append_subdivided_surface_triangles(
    std::vector<SurfaceSampleTriangle>& output,
    const Triangle& triangle,
    const int resolution,
    const float base_height,
    const std::map<SampleKey, float>& offsets
) {
    for (int i = 0; i < resolution; ++i) {
        for (int j = 0; j < resolution - i; ++j) {
            const Vec2 p0 = subdivided_point(triangle, i, j, resolution);
            const Vec2 p1 = subdivided_point(triangle, i + 1, j, resolution);
            const Vec2 p2 = subdivided_point(triangle, i, j + 1, resolution);
            output.push_back(SurfaceSampleTriangle{
                sample_vertex(p0, offset_at_generated_point(offsets, p0), base_height),
                sample_vertex(p1, offset_at_generated_point(offsets, p1), base_height),
                sample_vertex(p2, offset_at_generated_point(offsets, p2), base_height),
            });

            if (i + j < resolution - 1) {
                const Vec2 p3 = subdivided_point(triangle, i + 1, j + 1, resolution);
                output.push_back(SurfaceSampleTriangle{
                    sample_vertex(p1, offset_at_generated_point(offsets, p1), base_height),
                    sample_vertex(p3, offset_at_generated_point(offsets, p3), base_height),
                    sample_vertex(p2, offset_at_generated_point(offsets, p2), base_height),
                });
            }
        }
    }
}

void generated_sample_points(
    const SectorPlane& sector,
    const int resolution,
    std::map<SampleKey, SectorDisplacementSample>& samples
) {
    for (const Triangle& triangle : sector.triangles) {
        for (int i = 0; i <= resolution; ++i) {
            for (int j = 0; j <= resolution - i; ++j) {
                const Vec2 position = subdivided_point(triangle, i, j, resolution);
                samples.try_emplace(sample_key(position), SectorDisplacementSample{position, 0.0F});
            }
        }
    }
}

float unclamped_surface_height(const SectorPlane& sector, const SectorSurfaceKind surface, const Vec2 point) {
    return base_surface_height(sector, surface) + sample_surface_offset(sector, surface, point);
}

} // namespace

int clamped_displacement_resolution(const int resolution) {
    return std::clamp(resolution, kMinDisplacementResolution, kMaxDisplacementResolution);
}

float base_surface_height(const SectorPlane& sector, const SectorSurfaceKind surface) {
    if (surface == SectorSurfaceKind::Floor) {
        return sector.floor_height;
    }
    return sector.floor_height + sector.height;
}

const SectorSurfaceDisplacement& displacement_for_surface(
    const SectorPlane& sector,
    const SectorSurfaceKind surface
) {
    return surface == SectorSurfaceKind::Floor ? sector.floor_displacement : sector.ceiling_displacement;
}

SectorSurfaceDisplacement& displacement_for_surface(SectorPlane& sector, const SectorSurfaceKind surface) {
    return surface == SectorSurfaceKind::Floor ? sector.floor_displacement : sector.ceiling_displacement;
}

void normalize_displacement(SectorPlane& sector, const SectorSurfaceKind surface) {
    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    displacement.resolution = clamped_displacement_resolution(displacement.resolution);
    if (!displacement.enabled) {
        displacement.samples.clear();
        return;
    }

    std::map<SampleKey, SectorDisplacementSample> normalized;
    for (const SectorDisplacementSample& sample : displacement.samples) {
        if (!std::isfinite(sample.position.x) || !std::isfinite(sample.position.y) || !std::isfinite(sample.offset)) {
            continue;
        }
        if (!point_in_sector_triangulation(sector, sample.position)) {
            continue;
        }
        normalized[sample_key(sample.position)] = sample;
    }

    displacement.samples.clear();
    displacement.samples.reserve(normalized.size());
    for (const auto& [key, sample] : normalized) {
        (void)key;
        displacement.samples.push_back(sample);
    }
}

void ensure_displacement_samples(SectorPlane& sector, const SectorSurfaceKind surface) {
    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    set_displacement_resolution(sector, surface, displacement.resolution);
}

void set_displacement_resolution(SectorPlane& sector, const SectorSurfaceKind surface, const int target_resolution) {
    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    const bool resample_existing_surface = displacement.enabled && !displacement.samples.empty();
    const int resolution = clamped_displacement_resolution(target_resolution);

    std::map<SampleKey, SectorDisplacementSample> samples;
    generated_sample_points(sector, resolution, samples);
    for (auto& [key, sample] : samples) {
        (void)key;
        if (resample_existing_surface) {
            sample.offset = sample_surface_offset(sector, surface, sample.position);
        }
    }

    displacement.enabled = true;
    displacement.resolution = resolution;

    displacement.samples.clear();
    displacement.samples.reserve(samples.size());
    for (const auto& [key, sample] : samples) {
        (void)key;
        displacement.samples.push_back(sample);
    }
}

std::vector<SurfaceSampleTriangle> build_surface_sample_triangles(
    const SectorPlane& sector,
    const SectorSurfaceKind surface
) {
    const SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    const float base_height = base_surface_height(sector, surface);
    std::vector<SurfaceSampleTriangle> output;

    if (!displacement.enabled) {
        output.reserve(sector.triangles.size());
        for (const Triangle& triangle : sector.triangles) {
            output.push_back(SurfaceSampleTriangle{
                sample_vertex(triangle.a, 0.0F, base_height),
                sample_vertex(triangle.b, 0.0F, base_height),
                sample_vertex(triangle.c, 0.0F, base_height),
            });
        }
        return output;
    }

    const int resolution = clamped_displacement_resolution(displacement.resolution);
    const std::map<SampleKey, float> offsets = sample_offsets_by_key(displacement);
    output.reserve(sector.triangles.size() * resolution * resolution);
    for (const Triangle& triangle : sector.triangles) {
        append_subdivided_surface_triangles(output, triangle, resolution, base_height, offsets);
    }
    return output;
}

float sample_surface_offset(const SectorPlane& sector, const SectorSurfaceKind surface, const Vec2 point) {
    const SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    if (!displacement.enabled || displacement.samples.empty()) {
        return 0.0F;
    }

    const std::vector<SurfaceSampleTriangle> triangles = build_surface_sample_triangles(sector, surface);
    for (const SurfaceSampleTriangle& triangle : triangles) {
        float wa = 0.0F;
        float wb = 0.0F;
        float wc = 0.0F;
        if (barycentric_weights(point, triangle.a.position, triangle.b.position, triangle.c.position, wa, wb, wc)) {
            return (wa * triangle.a.offset) + (wb * triangle.b.offset) + (wc * triangle.c.offset);
        }
    }

    const SectorDisplacementSample* closest = nullptr;
    float closest_distance = 0.0F;
    for (const SectorDisplacementSample& sample : displacement.samples) {
        const float d = distance_squared(point, sample.position);
        if (closest == nullptr || d < closest_distance) {
            closest = &sample;
            closest_distance = d;
        }
    }
    return closest != nullptr ? closest->offset : 0.0F;
}

float sample_surface_height(const SectorPlane& sector, const SectorSurfaceKind surface, const Vec2 point) {
    return base_surface_height(sector, surface) + sample_surface_offset(sector, surface, point);
}

SurfaceHeightRange sector_surface_height_range(const SectorPlane& sector, const SectorSurfaceKind surface) {
    SurfaceHeightRange range{base_surface_height(sector, surface), base_surface_height(sector, surface)};
    const std::vector<SurfaceSampleTriangle> triangles = build_surface_sample_triangles(sector, surface);
    bool first = true;
    for (const SurfaceSampleTriangle& triangle : triangles) {
        const float heights[] = {triangle.a.height, triangle.b.height, triangle.c.height};
        for (const float height : heights) {
            if (first) {
                range.min_height = height;
                range.max_height = height;
                first = false;
            } else {
                range.min_height = std::min(range.min_height, height);
                range.max_height = std::max(range.max_height, height);
            }
        }
    }
    return range;
}

bool sector_displacement_enabled(const SectorPlane& sector) {
    return sector.floor_displacement.enabled || sector.ceiling_displacement.enabled;
}

bool sculpt_surface_displacement(
    SectorPlane& sector,
    const SectorSurfaceKind surface,
    const Vec2 center,
    const float radius,
    const float delta
) {
    if (radius <= 0.0F || delta == 0.0F || sector.triangles.empty()) {
        return false;
    }

    ensure_displacement_samples(sector, surface);
    SectorSurfaceDisplacement& displacement = displacement_for_surface(sector, surface);
    bool changed = false;
    for (SectorDisplacementSample& sample : displacement.samples) {
        const float distance = std::sqrt(distance_squared(sample.position, center));
        if (distance > radius) {
            continue;
        }
        const float weight = 1.0F - (distance / radius);
        const float previous = sample.offset;
        float next = previous + (delta * weight);

        if (surface == SectorSurfaceKind::Floor) {
            const float ceiling = unclamped_surface_height(sector, SectorSurfaceKind::Ceiling, sample.position);
            next = std::min(next, ceiling - base_surface_height(sector, surface) - kSectorMinHeight);
        } else {
            const float floor = unclamped_surface_height(sector, SectorSurfaceKind::Floor, sample.position);
            next = std::max(next, floor + kSectorMinHeight - base_surface_height(sector, surface));
        }

        if (std::abs(next - previous) > kDisplacementEpsilon) {
            sample.offset = next;
            changed = true;
        }
    }
    return changed;
}

void resample_displacements_from_sources(SectorPlane& sector, const std::vector<SectorPlane>& sources) {
    for (const SectorSurfaceKind surface : {SectorSurfaceKind::Floor, SectorSurfaceKind::Ceiling}) {
        const bool any_source_enabled = std::any_of(sources.begin(), sources.end(), [surface](const SectorPlane& source) {
            return displacement_for_surface(source, surface).enabled;
        });
        SectorSurfaceDisplacement& target_displacement = displacement_for_surface(sector, surface);
        if (!any_source_enabled) {
            target_displacement.enabled = false;
            target_displacement.samples.clear();
            continue;
        }

        target_displacement.enabled = true;
        target_displacement.resolution = std::max(
            target_displacement.resolution,
            clamped_displacement_resolution(displacement_for_surface(sources.front(), surface).resolution)
        );
        for (const SectorPlane& source : sources) {
            const SectorSurfaceDisplacement& source_displacement = displacement_for_surface(source, surface);
            if (source_displacement.enabled) {
                target_displacement.resolution = std::max(
                    target_displacement.resolution,
                    clamped_displacement_resolution(source_displacement.resolution)
                );
            }
        }
        target_displacement.resolution = clamped_displacement_resolution(target_displacement.resolution);
        ensure_displacement_samples(sector, surface);
        bool sampled_from_source = false;
        for (SectorDisplacementSample& sample : target_displacement.samples) {
            const auto source = std::find_if(
                sources.begin(),
                sources.end(),
                [surface, point = sample.position](const SectorPlane& candidate) {
                    return displacement_for_surface(candidate, surface).enabled &&
                        point_in_sector_source_shape(candidate, point);
                }
            );
            if (source == sources.end()) {
                sample.offset = 0.0F;
                continue;
            }
            sample.offset = sample_surface_height(*source, surface, sample.position) - base_surface_height(sector, surface);
            sampled_from_source = true;
        }
        if (!sampled_from_source) {
            target_displacement.enabled = false;
            target_displacement.samples.clear();
        }
    }
}

} // namespace undecedent
