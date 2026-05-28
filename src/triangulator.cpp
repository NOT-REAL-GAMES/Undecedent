#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace undecedent {
namespace {

float cross(const Vec2 a, const Vec2 b, const Vec2 c) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float acx = c.x - a.x;
    const float acy = c.y - a.y;
    return (abx * acy) - (aby * acx);
}

float distance_squared(const Vec2 a, const Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool nearly_equal(const Vec2 a, const Vec2 b) {
    return distance_squared(a, b) <= (kGeometryEpsilon * kGeometryEpsilon);
}

float signed_area(const std::vector<Vec2>& vertices) {
    double area = 0.0;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vec2 a = vertices[i];
        const Vec2 b = vertices[(i + 1) % vertices.size()];
        area += (static_cast<double>(a.x) * b.y) - (static_cast<double>(b.x) * a.y);
    }
    return static_cast<float>(area * 0.5);
}

PolygonLoop with_winding(PolygonLoop loop, const bool want_ccw) {
    const bool is_ccw = signed_area(loop.vertices) > 0.0F;
    if (is_ccw != want_ccw) {
        std::reverse(loop.vertices.begin(), loop.vertices.end());
    }
    return loop;
}

bool on_segment(const Vec2 a, const Vec2 b, const Vec2 p) {
    if (std::abs(cross(a, b, p)) > kGeometryEpsilon) {
        return false;
    }

    return p.x >= std::min(a.x, b.x) - kGeometryEpsilon &&
        p.x <= std::max(a.x, b.x) + kGeometryEpsilon &&
        p.y >= std::min(a.y, b.y) - kGeometryEpsilon &&
        p.y <= std::max(a.y, b.y) + kGeometryEpsilon;
}

int orientation(const Vec2 a, const Vec2 b, const Vec2 c) {
    const float value = cross(a, b, c);
    if (value > kGeometryEpsilon) {
        return 1;
    }
    if (value < -kGeometryEpsilon) {
        return -1;
    }
    return 0;
}

bool segments_intersect(const Vec2 a, const Vec2 b, const Vec2 c, const Vec2 d) {
    const int o1 = orientation(a, b, c);
    const int o2 = orientation(a, b, d);
    const int o3 = orientation(c, d, a);
    const int o4 = orientation(c, d, b);

    if (o1 != o2 && o3 != o4) {
        return true;
    }

    return (o1 == 0 && on_segment(a, b, c)) ||
        (o2 == 0 && on_segment(a, b, d)) ||
        (o3 == 0 && on_segment(c, d, a)) ||
        (o4 == 0 && on_segment(c, d, b));
}

bool point_on_loop_boundary(const std::vector<Vec2>& loop, const Vec2 point) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
        if (on_segment(loop[i], loop[(i + 1) % loop.size()], point)) {
            return true;
        }
    }
    return false;
}

bool point_in_polygon(const std::vector<Vec2>& loop, const Vec2 point) {
    if (point_on_loop_boundary(loop, point)) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const Vec2 a = loop[i];
        const Vec2 b = loop[j];
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
            (point.x < ((b.x - a.x) * (point.y - a.y) / (b.y - a.y)) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

bool has_duplicate_vertices(const std::vector<Vec2>& vertices) {
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = i + 1; j < vertices.size(); ++j) {
            if (nearly_equal(vertices[i], vertices[j])) {
                return true;
            }
        }
    }
    return false;
}

bool loop_self_intersects(const std::vector<Vec2>& vertices) {
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const std::size_t i_next = (i + 1) % vertices.size();
        for (std::size_t j = i + 1; j < vertices.size(); ++j) {
            const std::size_t j_next = (j + 1) % vertices.size();
            const bool adjacent = i == j || i_next == j || j_next == i;
            if (adjacent) {
                continue;
            }

            if (segments_intersect(vertices[i], vertices[i_next], vertices[j], vertices[j_next])) {
                return true;
            }
        }
    }
    return false;
}

TriangulationResult fail(const TriangulationStatus status, const char* message) {
    return TriangulationResult{status, message, {}};
}

std::optional<TriangulationResult> validate_loop(const PolygonLoop& loop) {
    if (loop.vertices.size() < 3) {
        return fail(TriangulationStatus::NotEnoughVertices, "Loop needs at least three vertices.");
    }

    if (has_duplicate_vertices(loop.vertices)) {
        return fail(TriangulationStatus::DuplicateVertex, "Loop contains duplicate or near-duplicate vertices.");
    }

    if (loop_self_intersects(loop.vertices)) {
        return fail(TriangulationStatus::SelfIntersection, "Loop edges intersect.");
    }

    if (std::abs(signed_area(loop.vertices)) <= kGeometryEpsilon) {
        return fail(TriangulationStatus::DegeneratePolygon, "Loop area is too small or collinear.");
    }

    return std::nullopt;
}

bool loops_intersect(const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Vec2 a0 = a[i];
        const Vec2 a1 = a[(i + 1) % a.size()];
        for (std::size_t j = 0; j < b.size(); ++j) {
            if (segments_intersect(a0, a1, b[j], b[(j + 1) % b.size()])) {
                return true;
            }
        }
    }
    return false;
}

std::size_t rightmost_vertex_index(const std::vector<Vec2>& vertices) {
    std::size_t result = 0;
    for (std::size_t i = 1; i < vertices.size(); ++i) {
        if (vertices[i].x > vertices[result].x ||
            (std::abs(vertices[i].x - vertices[result].x) <= kGeometryEpsilon && vertices[i].y < vertices[result].y)) {
            result = i;
        }
    }
    return result;
}

bool bridge_intersects_loop(
    const Vec2 a,
    const Vec2 b,
    const std::vector<Vec2>& loop,
    const std::optional<std::size_t> allowed_vertex
) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const std::size_t next = (i + 1) % loop.size();
        if (allowed_vertex.has_value() && (i == *allowed_vertex || next == *allowed_vertex)) {
            continue;
        }

        if (segments_intersect(a, b, loop[i], loop[next])) {
            return true;
        }
    }
    return false;
}

bool bridge_intersects_hole(const Vec2 a, const Vec2 b, const std::vector<Vec2>& hole, const std::size_t hole_vertex) {
    for (std::size_t i = 0; i < hole.size(); ++i) {
        const std::size_t next = (i + 1) % hole.size();
        if (i == hole_vertex || next == hole_vertex) {
            continue;
        }

        if (segments_intersect(a, b, hole[i], hole[next])) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t> find_bridge_vertex(
    const std::vector<Vec2>& polygon,
    const std::vector<Vec2>& hole,
    const std::vector<PolygonLoop>& all_holes,
    const std::size_t hole_vertex
) {
    const Vec2 h = hole[hole_vertex];
    std::optional<std::size_t> best;
    float best_distance = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2 candidate = polygon[i];
        if (bridge_intersects_loop(h, candidate, polygon, i)) {
            continue;
        }
        if (bridge_intersects_hole(h, candidate, hole, hole_vertex)) {
            continue;
        }

        bool hits_other_hole = false;
        for (const PolygonLoop& other_hole : all_holes) {
            if (other_hole.vertices.data() == hole.data()) {
                continue;
            }
            if (bridge_intersects_loop(h, candidate, other_hole.vertices, std::nullopt)) {
                hits_other_hole = true;
                break;
            }
        }
        if (hits_other_hole) {
            continue;
        }

        const float candidate_distance = distance_squared(h, candidate);
        if (candidate_distance < best_distance) {
            best = i;
            best_distance = candidate_distance;
        }
    }

    return best;
}

std::optional<std::vector<Vec2>> bridge_holes(PolygonLoop outer, std::vector<PolygonLoop> holes) {
    std::vector<Vec2> polygon = with_winding(std::move(outer), true).vertices;
    for (PolygonLoop& hole : holes) {
        hole = with_winding(std::move(hole), false);
    }

    std::sort(holes.begin(), holes.end(), [](const PolygonLoop& a, const PolygonLoop& b) {
        const Vec2 ar = a.vertices[rightmost_vertex_index(a.vertices)];
        const Vec2 br = b.vertices[rightmost_vertex_index(b.vertices)];
        return ar.x > br.x;
    });

    for (const PolygonLoop& hole : holes) {
        const std::size_t h = rightmost_vertex_index(hole.vertices);
        const std::optional<std::size_t> bridge = find_bridge_vertex(polygon, hole.vertices, holes, h);
        if (!bridge.has_value()) {
            return std::nullopt;
        }

        std::vector<Vec2> merged;
        merged.reserve(polygon.size() + hole.vertices.size() + 2);

        for (std::size_t i = 0; i <= *bridge; ++i) {
            merged.push_back(polygon[i]);
        }

        merged.push_back(hole.vertices[h]);
        for (std::size_t offset = 1; offset < hole.vertices.size(); ++offset) {
            const std::size_t index = (h + offset) % hole.vertices.size();
            merged.push_back(hole.vertices[index]);
        }
        merged.push_back(hole.vertices[h]);
        merged.push_back(polygon[*bridge]);

        for (std::size_t i = *bridge + 1; i < polygon.size(); ++i) {
            merged.push_back(polygon[i]);
        }

        polygon = std::move(merged);
    }

    return polygon;
}

bool point_in_triangle(const Vec2 p, const Vec2 a, const Vec2 b, const Vec2 c) {
    const float ab = cross(a, b, p);
    const float bc = cross(b, c, p);
    const float ca = cross(c, a, p);
    return ab >= -kGeometryEpsilon && bc >= -kGeometryEpsilon && ca >= -kGeometryEpsilon;
}

TriangulationResult ear_clip(std::vector<Vec2> polygon) {
    if (signed_area(polygon) < 0.0F) {
        std::reverse(polygon.begin(), polygon.end());
    }

    std::vector<std::size_t> indices;
    indices.reserve(polygon.size());
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        indices.push_back(i);
    }

    TriangulationResult result{};
    std::size_t guard = 0;
    while (indices.size() > 3 && guard < polygon.size() * polygon.size()) {
        bool clipped = false;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const std::size_t previous = indices[(i + indices.size() - 1) % indices.size()];
            const std::size_t current = indices[i];
            const std::size_t next = indices[(i + 1) % indices.size()];
            const Vec2 a = polygon[previous];
            const Vec2 b = polygon[current];
            const Vec2 c = polygon[next];

            if (cross(a, b, c) <= kGeometryEpsilon) {
                continue;
            }

            bool contains_point = false;
            for (const std::size_t candidate : indices) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (nearly_equal(polygon[candidate], a) || nearly_equal(polygon[candidate], b) ||
                    nearly_equal(polygon[candidate], c)) {
                    continue;
                }
                if (point_in_triangle(polygon[candidate], a, b, c)) {
                    contains_point = true;
                    break;
                }
            }

            if (contains_point) {
                continue;
            }

            result.triangles.push_back(Triangle{a, b, c});
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }

        if (!clipped) {
            return fail(TriangulationStatus::TriangulationFailed, "Could not find a valid ear.");
        }
        ++guard;
    }

    if (indices.size() != 3) {
        return fail(TriangulationStatus::TriangulationFailed, "Triangulation produced an invalid final polygon.");
    }

    const Vec2 a = polygon[indices[0]];
    const Vec2 b = polygon[indices[1]];
    const Vec2 c = polygon[indices[2]];
    if (cross(a, b, c) <= kGeometryEpsilon) {
        return fail(TriangulationStatus::TriangulationFailed, "Final triangle is degenerate.");
    }

    result.triangles.push_back(Triangle{a, b, c});
    result.status = TriangulationStatus::Ok;
    result.message = "Ok";
    return result;
}

} // namespace

TriangulationResult triangulate_polygon(const PolygonLoop& outer, const std::vector<PolygonLoop>& holes) {
    if (const std::optional<TriangulationResult> error = validate_loop(outer)) {
        return *error;
    }

    for (const PolygonLoop& hole : holes) {
        if (const std::optional<TriangulationResult> error = validate_loop(hole)) {
            return *error;
        }

        for (const Vec2 vertex : hole.vertices) {
            if (!point_in_polygon(outer.vertices, vertex)) {
                return fail(TriangulationStatus::HoleOutsideOuter, "Hole vertex is outside the outer loop.");
            }
        }

        if (loops_intersect(outer.vertices, hole.vertices)) {
            return fail(TriangulationStatus::HoleIntersectsOuter, "Hole edges intersect the outer loop.");
        }
    }

    for (std::size_t i = 0; i < holes.size(); ++i) {
        for (std::size_t j = i + 1; j < holes.size(); ++j) {
            if (loops_intersect(holes[i].vertices, holes[j].vertices) ||
                point_in_polygon(holes[i].vertices, holes[j].vertices.front()) ||
                point_in_polygon(holes[j].vertices, holes[i].vertices.front())) {
                return fail(TriangulationStatus::HoleIntersectsHole, "Hole loops overlap, intersect, or nest.");
            }
        }
    }

    const std::optional<std::vector<Vec2>> merged = bridge_holes(outer, holes);
    if (!merged.has_value()) {
        return fail(TriangulationStatus::BridgeFailed, "Could not connect holes to the outer loop.");
    }

    return ear_clip(*merged);
}

const char* triangulation_status_name(const TriangulationStatus status) {
    switch (status) {
        case TriangulationStatus::Ok: return "Ok";
        case TriangulationStatus::NotEnoughVertices: return "NotEnoughVertices";
        case TriangulationStatus::DuplicateVertex: return "DuplicateVertex";
        case TriangulationStatus::DegeneratePolygon: return "DegeneratePolygon";
        case TriangulationStatus::SelfIntersection: return "SelfIntersection";
        case TriangulationStatus::HoleOutsideOuter: return "HoleOutsideOuter";
        case TriangulationStatus::HoleIntersectsOuter: return "HoleIntersectsOuter";
        case TriangulationStatus::HoleIntersectsHole: return "HoleIntersectsHole";
        case TriangulationStatus::BridgeFailed: return "BridgeFailed";
        case TriangulationStatus::TriangulationFailed: return "TriangulationFailed";
    }
    return "Unknown";
}

} // namespace undecedent
