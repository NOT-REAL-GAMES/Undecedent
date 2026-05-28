#include "undecedent/csg.hpp"

#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace undecedent {
namespace {

constexpr double kCsgEpsilon = 1.0e-8;
constexpr double kCsgAreaEpsilon = 1.0e-4;
constexpr std::int64_t kCsgScale = 1024;

struct GridPoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

struct Segment {
    GridPoint a;
    GridPoint b;
    std::vector<double> splits;
};

struct EdgeCandidate {
    int to = -1;
    double angle = 0.0;
};

bool operator<(const GridPoint& a, const GridPoint& b) {
    if (a.x != b.x) {
        return a.x < b.x;
    }
    return a.y < b.y;
}

bool operator==(const GridPoint& a, const GridPoint& b) {
    return a.x == b.x && a.y == b.y;
}

GridPoint to_grid(const Vec2 point) {
    return GridPoint{
        static_cast<std::int64_t>(std::llround(static_cast<double>(point.x) * kCsgScale)),
        static_cast<std::int64_t>(std::llround(static_cast<double>(point.y) * kCsgScale)),
    };
}

Vec2 to_vec2(const GridPoint point) {
    return Vec2{
        static_cast<float>(static_cast<double>(point.x) / kCsgScale),
        static_cast<float>(static_cast<double>(point.y) / kCsgScale),
    };
}

double cross(const GridPoint a, const GridPoint b, const GridPoint c) {
    const double abx = static_cast<double>(b.x - a.x);
    const double aby = static_cast<double>(b.y - a.y);
    const double acx = static_cast<double>(c.x - a.x);
    const double acy = static_cast<double>(c.y - a.y);
    return (abx * acy) - (aby * acx);
}

double signed_area(const std::vector<GridPoint>& points) {
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const GridPoint a = points[i];
        const GridPoint b = points[(i + 1) % points.size()];
        area += (static_cast<double>(a.x) * b.y) - (static_cast<double>(b.x) * a.y);
    }
    return area * 0.5;
}

double segment_t(const Segment& segment, const GridPoint point) {
    const double dx = static_cast<double>(segment.b.x - segment.a.x);
    const double dy = static_cast<double>(segment.b.y - segment.a.y);
    const double length_squared = (dx * dx) + (dy * dy);
    if (length_squared <= kCsgEpsilon) {
        return 0.0;
    }

    return (((static_cast<double>(point.x - segment.a.x) * dx) +
        (static_cast<double>(point.y - segment.a.y) * dy)) / length_squared);
}

GridPoint point_at(const Segment& segment, const double t) {
    const double x = static_cast<double>(segment.a.x) +
        (static_cast<double>(segment.b.x - segment.a.x) * t);
    const double y = static_cast<double>(segment.a.y) +
        (static_cast<double>(segment.b.y - segment.a.y) * t);
    return GridPoint{
        static_cast<std::int64_t>(std::llround(x)),
        static_cast<std::int64_t>(std::llround(y)),
    };
}

void add_split(std::vector<double>& splits, const double t) {
    if (t < -kCsgEpsilon || t > 1.0 + kCsgEpsilon) {
        return;
    }

    const double clamped = std::clamp(t, 0.0, 1.0);
    for (const double existing : splits) {
        if (std::abs(existing - clamped) <= 1.0e-7) {
            return;
        }
    }

    splits.push_back(clamped);
}

bool on_segment(const Segment& segment, const GridPoint point) {
    if (std::abs(cross(segment.a, segment.b, point)) > kCsgEpsilon) {
        return false;
    }

    return point.x >= std::min(segment.a.x, segment.b.x) &&
        point.x <= std::max(segment.a.x, segment.b.x) &&
        point.y >= std::min(segment.a.y, segment.b.y) &&
        point.y <= std::max(segment.a.y, segment.b.y);
}

void split_at_intersection(Segment& a, Segment& b) {
    const double ax = static_cast<double>(a.a.x);
    const double ay = static_cast<double>(a.a.y);
    const double bx = static_cast<double>(a.b.x);
    const double by = static_cast<double>(a.b.y);
    const double cx = static_cast<double>(b.a.x);
    const double cy = static_cast<double>(b.a.y);
    const double dx = static_cast<double>(b.b.x);
    const double dy = static_cast<double>(b.b.y);

    const double rx = bx - ax;
    const double ry = by - ay;
    const double sx = dx - cx;
    const double sy = dy - cy;
    const double denominator = (rx * sy) - (ry * sx);
    const double qpx = cx - ax;
    const double qpy = cy - ay;

    if (std::abs(denominator) <= kCsgEpsilon) {
        const double collinear = (qpx * ry) - (qpy * rx);
        if (std::abs(collinear) > kCsgEpsilon) {
            return;
        }

        if (on_segment(a, b.a)) {
            add_split(a.splits, segment_t(a, b.a));
            add_split(b.splits, 0.0);
        }
        if (on_segment(a, b.b)) {
            add_split(a.splits, segment_t(a, b.b));
            add_split(b.splits, 1.0);
        }
        if (on_segment(b, a.a)) {
            add_split(b.splits, segment_t(b, a.a));
            add_split(a.splits, 0.0);
        }
        if (on_segment(b, a.b)) {
            add_split(b.splits, segment_t(b, a.b));
            add_split(a.splits, 1.0);
        }
        return;
    }

    const double t = ((qpx * sy) - (qpy * sx)) / denominator;
    const double u = ((qpx * ry) - (qpy * rx)) / denominator;
    if (t >= -kCsgEpsilon && t <= 1.0 + kCsgEpsilon && u >= -kCsgEpsilon && u <= 1.0 + kCsgEpsilon) {
        add_split(a.splits, t);
        add_split(b.splits, u);
    }
}

void add_loop_segments(std::vector<Segment>& segments, const PolygonLoop& loop) {
    if (loop.vertices.size() < 2) {
        return;
    }

    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        Segment segment{to_grid(loop.vertices[i]), to_grid(loop.vertices[(i + 1) % loop.vertices.size()]), {0.0, 1.0}};
        if (!(segment.a == segment.b)) {
            segments.push_back(std::move(segment));
        }
    }
}

void add_sector_segments(std::vector<Segment>& segments, const SectorPlane& sector) {
    add_loop_segments(segments, sector.outer);
    for (const PolygonLoop& hole : sector.holes) {
        add_loop_segments(segments, hole);
    }
}

int point_id(std::map<GridPoint, int>& ids, std::vector<GridPoint>& points, const GridPoint point) {
    const auto found = ids.find(point);
    if (found != ids.end()) {
        return found->second;
    }

    const int id = static_cast<int>(points.size());
    ids.emplace(point, id);
    points.push_back(point);
    return id;
}

bool point_on_boundary(const PolygonLoop& loop, const Vec2 point) {
    const GridPoint p = to_grid(point);
    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const Segment segment{to_grid(loop.vertices[i]), to_grid(loop.vertices[(i + 1) % loop.vertices.size()]), {}};
        if (on_segment(segment, p)) {
            return true;
        }
    }
    return false;
}

bool point_in_polygon_or_on(const PolygonLoop& loop, const Vec2 point) {
    if (loop.vertices.size() < 3) {
        return false;
    }

    if (point_on_boundary(loop, point)) {
        return true;
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

PolygonLoop clean_loop(std::vector<GridPoint> points) {
    std::vector<GridPoint> cleaned;
    for (const GridPoint point : points) {
        if (cleaned.empty() || !(cleaned.back() == point)) {
            cleaned.push_back(point);
        }
    }

    if (cleaned.size() > 1 && cleaned.front() == cleaned.back()) {
        cleaned.pop_back();
    }

    bool changed = true;
    while (changed && cleaned.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < cleaned.size(); ++i) {
            const GridPoint previous = cleaned[(i + cleaned.size() - 1) % cleaned.size()];
            const GridPoint current = cleaned[i];
            const GridPoint next = cleaned[(i + 1) % cleaned.size()];
            if (std::abs(cross(previous, current, next)) <= kCsgEpsilon) {
                cleaned.erase(cleaned.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }

    PolygonLoop loop;
    loop.vertices.reserve(cleaned.size());
    for (const GridPoint point : cleaned) {
        loop.vertices.push_back(to_vec2(point));
    }
    return loop;
}

Vec2 sample_point_for_loop(const PolygonLoop& loop) {
    const TriangulationResult triangulated = triangulate_polygon(loop);
    if (triangulated.status == TriangulationStatus::Ok && !triangulated.triangles.empty()) {
        const Triangle triangle = triangulated.triangles.front();
        return Vec2{
            (triangle.a.x + triangle.b.x + triangle.c.x) / 3.0F,
            (triangle.a.y + triangle.b.y + triangle.c.y) / 3.0F,
        };
    }

    Vec2 sample{};
    for (const Vec2 point : loop.vertices) {
        sample.x += point.x;
        sample.y += point.y;
    }
    sample.x /= static_cast<float>(loop.vertices.size());
    sample.y /= static_cast<float>(loop.vertices.size());
    return sample;
}

bool in_existing_union(const std::vector<SectorPlane>& sectors, const Vec2 point) {
    return std::any_of(sectors.begin(), sectors.end(), [point](const SectorPlane& sector) {
        if (!point_in_polygon_or_on(sector.outer, point)) {
            return false;
        }
        return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
            return point_in_polygon_or_on(hole, point);
        });
    });
}

const SectorPlane* source_sector_for_point(const std::vector<SectorPlane>& sectors, const Vec2 point) {
    const auto found = std::find_if(sectors.begin(), sectors.end(), [point](const SectorPlane& sector) {
        if (!point_in_polygon_or_on(sector.outer, point)) {
            return false;
        }
        return std::none_of(sector.holes.begin(), sector.holes.end(), [point](const PolygonLoop& hole) {
            return point_in_polygon_or_on(hole, point);
        });
    });
    return found == sectors.end() ? nullptr : &(*found);
}

void rebuild_edges(std::vector<SectorPlane>& sectors) {
    std::map<std::pair<GridPoint, GridPoint>, std::pair<int, int>> directed_edges;

    for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
        SectorPlane& sector = sectors[sector_index];
        sector.edge_neighbors.assign(sector.outer.vertices.size(), -1);
        for (std::size_t edge_index = 0; edge_index < sector.outer.vertices.size(); ++edge_index) {
            const GridPoint a = to_grid(sector.outer.vertices[edge_index]);
            const GridPoint b = to_grid(sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()]);
            directed_edges[{a, b}] = {static_cast<int>(sector_index), static_cast<int>(edge_index)};
        }
    }

    for (std::size_t sector_index = 0; sector_index < sectors.size(); ++sector_index) {
        SectorPlane& sector = sectors[sector_index];
        for (std::size_t edge_index = 0; edge_index < sector.outer.vertices.size(); ++edge_index) {
            const GridPoint a = to_grid(sector.outer.vertices[edge_index]);
            const GridPoint b = to_grid(sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()]);
            const auto found = directed_edges.find({b, a});
            if (found != directed_edges.end()) {
                sector.edge_neighbors[edge_index] = found->second.first;
            }
        }
    }
}

enum class CsgOperation {
    Rebuild,
    Add,
    Subtract,
};

int orientation(const GridPoint a, const GridPoint b, const GridPoint c) {
    const double value = cross(a, b, c);
    if (value > kCsgEpsilon) {
        return 1;
    }
    if (value < -kCsgEpsilon) {
        return -1;
    }
    return 0;
}

bool segments_touch(const Segment& a, const Segment& b) {
    const int o1 = orientation(a.a, a.b, b.a);
    const int o2 = orientation(a.a, a.b, b.b);
    const int o3 = orientation(b.a, b.b, a.a);
    const int o4 = orientation(b.a, b.b, a.b);

    if (o1 != o2 && o3 != o4) {
        return true;
    }
    return (o1 == 0 && on_segment(a, b.a)) || (o2 == 0 && on_segment(a, b.b)) ||
           (o3 == 0 && on_segment(b, a.a)) || (o4 == 0 && on_segment(b, a.b));
}

bool segment_crosses_loop_except_endpoint(const Segment& bridge, const PolygonLoop& loop, const GridPoint endpoint) {
    for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
        const Segment edge{to_grid(loop.vertices[i]), to_grid(loop.vertices[(i + 1) % loop.vertices.size()])};
        if (!segments_touch(bridge, edge)) {
            continue;
        }
        if (bridge.a == endpoint && (edge.a == endpoint || edge.b == endpoint)) {
            continue;
        }
        if (bridge.b == endpoint && (edge.a == endpoint || edge.b == endpoint)) {
            continue;
        }
        return true;
    }
    return false;
}

bool cut_touches_existing_edges(const std::vector<SectorPlane>& existing_sectors, const PolygonLoop& cut_loop) {
    for (std::size_t i = 0; i < cut_loop.vertices.size(); ++i) {
        const Segment cut{to_grid(cut_loop.vertices[i]), to_grid(cut_loop.vertices[(i + 1) % cut_loop.vertices.size()])};
        for (const SectorPlane& sector : existing_sectors) {
            for (std::size_t j = 0; j < sector.outer.vertices.size(); ++j) {
                const Segment edge{
                    to_grid(sector.outer.vertices[j]),
                    to_grid(sector.outer.vertices[(j + 1) % sector.outer.vertices.size()]),
                };
                if (segments_touch(cut, edge)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void add_subtract_bridge_if_needed(
    std::vector<Segment>& segments,
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& cut_loop
) {
    if (cut_loop.vertices.empty() || cut_touches_existing_edges(existing_sectors, cut_loop)) {
        return;
    }

    GridPoint cut_point = to_grid(cut_loop.vertices.front());
    for (const Vec2 point : cut_loop.vertices) {
        const GridPoint grid = to_grid(point);
        if (grid.x > cut_point.x || (grid.x == cut_point.x && grid.y > cut_point.y)) {
            cut_point = grid;
        }
    }

    bool found = false;
    GridPoint best{};
    long long best_distance = 0;
    for (const SectorPlane& sector : existing_sectors) {
        for (const Vec2 point : sector.outer.vertices) {
            const GridPoint candidate = to_grid(point);
            if (candidate == cut_point) {
                continue;
            }

            const Segment bridge{cut_point, candidate};
            if (segment_crosses_loop_except_endpoint(bridge, cut_loop, cut_point)) {
                continue;
            }

            const long long dx = static_cast<long long>(candidate.x) - cut_point.x;
            const long long dy = static_cast<long long>(candidate.y) - cut_point.y;
            const long long distance = dx * dx + dy * dy;
            if (!found || distance < best_distance) {
                found = true;
                best = candidate;
                best_distance = distance;
            }
        }
    }

    if (found) {
        segments.push_back(Segment{cut_point, best});
    }
}

bool keep_face(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop* operand,
    const CsgOperation operation,
    const Vec2 sample
) {
    const bool inside_existing = in_existing_union(existing_sectors, sample);
    const bool inside_operand = operand != nullptr && point_in_polygon_or_on(*operand, sample);

    switch (operation) {
    case CsgOperation::Rebuild:
        return inside_existing;
    case CsgOperation::Add:
        return inside_existing || inside_operand;
    case CsgOperation::Subtract:
        return inside_existing && !inside_operand;
    }
    return false;
}

CsgAddResult run_csg(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop* operand,
    const CsgOperation operation
) {
    if (operand != nullptr) {
        const TriangulationResult operand_validation = triangulate_polygon(*operand);
        if (operand_validation.status != TriangulationStatus::Ok) {
            return CsgAddResult{false, operand_validation.message, existing_sectors};
        }
    }

    if (operation != CsgOperation::Add && existing_sectors.empty()) {
        return CsgAddResult{true, "Ok", {}};
    }

    std::vector<Segment> segments;
    segments.reserve((existing_sectors.size() + (operand != nullptr ? 1 : 0)) * 4);
    for (const SectorPlane& sector : existing_sectors) {
        const TriangulationResult validation = triangulate_polygon(sector.outer);
        if (validation.status != TriangulationStatus::Ok) {
            return CsgAddResult{false, validation.message, existing_sectors};
        }
        add_sector_segments(segments, sector);
    }
    if (operand != nullptr) {
        add_loop_segments(segments, *operand);
        if (operation == CsgOperation::Subtract) {
            add_subtract_bridge_if_needed(segments, existing_sectors, *operand);
        }
    }

    for (std::size_t i = 0; i < segments.size(); ++i) {
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            split_at_intersection(segments[i], segments[j]);
        }
    }

    std::map<GridPoint, int> ids;
    std::vector<GridPoint> points;
    std::set<std::pair<int, int>> undirected_edges;

    for (Segment& segment : segments) {
        std::sort(segment.splits.begin(), segment.splits.end());
        for (std::size_t i = 0; i + 1 < segment.splits.size(); ++i) {
            const GridPoint a = point_at(segment, segment.splits[i]);
            const GridPoint b = point_at(segment, segment.splits[i + 1]);
            if (a == b) {
                continue;
            }

            const int a_id = point_id(ids, points, a);
            const int b_id = point_id(ids, points, b);
            undirected_edges.insert({std::min(a_id, b_id), std::max(a_id, b_id)});
        }
    }

    std::vector<std::vector<EdgeCandidate>> adjacency(points.size());
    for (const auto [a, b] : undirected_edges) {
        const GridPoint pa = points[static_cast<std::size_t>(a)];
        const GridPoint pb = points[static_cast<std::size_t>(b)];
        adjacency[static_cast<std::size_t>(a)].push_back(EdgeCandidate{
            b,
            std::atan2(static_cast<double>(pb.y - pa.y), static_cast<double>(pb.x - pa.x)),
        });
        adjacency[static_cast<std::size_t>(b)].push_back(EdgeCandidate{
            a,
            std::atan2(static_cast<double>(pa.y - pb.y), static_cast<double>(pa.x - pb.x)),
        });
    }

    for (std::vector<EdgeCandidate>& edges : adjacency) {
        std::sort(edges.begin(), edges.end(), [](const EdgeCandidate& a, const EdgeCandidate& b) {
            return a.angle < b.angle;
        });
    }

    std::set<std::pair<int, int>> visited;
    std::vector<SectorPlane> output;

    for (int start = 0; start < static_cast<int>(adjacency.size()); ++start) {
        for (const EdgeCandidate& start_edge : adjacency[static_cast<std::size_t>(start)]) {
            const std::pair<int, int> first_half_edge{start, start_edge.to};
            if (visited.contains(first_half_edge)) {
                continue;
            }

            std::vector<GridPoint> face_points;
            int from = start;
            int to = start_edge.to;
            std::size_t guard = 0;

            while (!visited.contains({from, to}) && guard < undirected_edges.size() * 4 + 8) {
                visited.insert({from, to});
                face_points.push_back(points[static_cast<std::size_t>(from)]);

                const std::vector<EdgeCandidate>& next_edges = adjacency[static_cast<std::size_t>(to)];
                const auto incoming = std::find_if(next_edges.begin(), next_edges.end(), [from](const EdgeCandidate& edge) {
                    return edge.to == from;
                });
                if (incoming == next_edges.end()) {
                    break;
                }

                const std::ptrdiff_t incoming_index = incoming - next_edges.begin();
                const std::ptrdiff_t next_index =
                    (incoming_index - 1 + static_cast<std::ptrdiff_t>(next_edges.size())) %
                    static_cast<std::ptrdiff_t>(next_edges.size());
                const int next_to = next_edges[static_cast<std::size_t>(next_index)].to;

                from = to;
                to = next_to;
                ++guard;

                if (from == first_half_edge.first && to == first_half_edge.second) {
                    break;
                }
            }

            if (face_points.size() < 3 || signed_area(face_points) <= kCsgAreaEpsilon) {
                continue;
            }

            PolygonLoop loop = clean_loop(std::move(face_points));
            if (loop.vertices.size() < 3) {
                continue;
            }

            const Vec2 sample = sample_point_for_loop(loop);
            if (!keep_face(existing_sectors, operand, operation, sample)) {
                continue;
            }

            SectorPlane sector;
            sector.outer = loop;
            if (const SectorPlane* source = source_sector_for_point(existing_sectors, sample)) {
                sector.floor_height = source->floor_height;
                sector.height = source->height;
            }
            const TriangulationResult triangulated = triangulate_polygon(sector.outer);
            if (triangulated.status != TriangulationStatus::Ok) {
                continue;
            }

            sector.triangles = triangulated.triangles;
            sector.status = triangulated.status;
            sector.status_message = triangulated.message;
            sector.edge_neighbors.assign(sector.outer.vertices.size(), -1);
            output.push_back(std::move(sector));
        }
    }

    if (output.empty() && operation != CsgOperation::Subtract) {
        return CsgAddResult{false, "CSG operation produced no valid sectors.", existing_sectors};
    }

    std::sort(output.begin(), output.end(), [](const SectorPlane& a, const SectorPlane& b) {
        const Vec2 as = sample_point_for_loop(a.outer);
        const Vec2 bs = sample_point_for_loop(b.outer);
        if (std::abs(as.x - bs.x) > 0.001F) {
            return as.x < bs.x;
        }
        return as.y < bs.y;
    });

    rebuild_edges(output);
    return CsgAddResult{true, "Ok", std::move(output)};
}

CsgAddResult contained_hole_subtract_fallback(
    const std::vector<SectorPlane>& existing_sectors,
    const PolygonLoop& cut_loop
) {
    std::vector<SectorPlane> output;
    output.reserve(existing_sectors.size() + cut_loop.vertices.size());
    bool changed = false;

    for (const SectorPlane& sector : existing_sectors) {
        const bool inside_outer = std::all_of(cut_loop.vertices.begin(), cut_loop.vertices.end(), [&sector](const Vec2 point) {
            return point_in_polygon_or_on(sector.outer, point);
        });
        if (changed || !inside_outer) {
            output.push_back(sector);
            continue;
        }

        const bool overlaps_existing_hole = std::any_of(sector.holes.begin(), sector.holes.end(), [&cut_loop](const PolygonLoop& hole) {
            return std::any_of(cut_loop.vertices.begin(), cut_loop.vertices.end(), [&hole](const Vec2 point) {
                return point_in_polygon_or_on(hole, point);
            });
        });
        if (overlaps_existing_hole) {
            output.push_back(sector);
            continue;
        }

        std::vector<PolygonLoop> holes = sector.holes;
        holes.push_back(cut_loop);
        const TriangulationResult triangulated = triangulate_polygon(sector.outer, holes);
        if (triangulated.status != TriangulationStatus::Ok) {
            return CsgAddResult{false, triangulated.message, existing_sectors};
        }

        SectorPlane holed_sector = sector;
        holed_sector.holes = std::move(holes);
        holed_sector.triangles = triangulated.triangles;
        holed_sector.status = triangulated.status;
        holed_sector.status_message = triangulated.message;
        output.push_back(std::move(holed_sector));
        changed = true;
    }

    if (!changed) {
        return CsgAddResult{true, "Ok", {}};
    }

    rebuild_edges(output);
    return CsgAddResult{true, "Ok", std::move(output)};
}

double polygon_area(const PolygonLoop& loop) {
    std::vector<GridPoint> points;
    points.reserve(loop.vertices.size());
    for (const Vec2 point : loop.vertices) {
        points.push_back(to_grid(point));
    }
    return signed_area(points) / (static_cast<double>(kCsgScale) * static_cast<double>(kCsgScale));
}

void force_winding(PolygonLoop& loop, const bool counter_clockwise) {
    const double area = polygon_area(loop);
    if ((counter_clockwise && area < 0.0) || (!counter_clockwise && area > 0.0)) {
        std::reverse(loop.vertices.begin(), loop.vertices.end());
    }
}

std::vector<int> normalize_selection(const std::vector<SectorPlane>& sectors, const std::vector<int>& selected_indices) {
    std::vector<int> selected;
    selected.reserve(selected_indices.size());
    for (const int index : selected_indices) {
        if (index < 0 || index >= static_cast<int>(sectors.size())) {
            continue;
        }
        if (std::find(selected.begin(), selected.end(), index) == selected.end()) {
            selected.push_back(index);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

bool selected_sectors_are_connected(const std::vector<SectorPlane>& sectors, const std::vector<int>& selected) {
    if (selected.empty()) {
        return false;
    }

    const std::set<int> selected_set(selected.begin(), selected.end());
    std::set<int> visited;
    std::vector<int> stack{selected.front()};
    visited.insert(selected.front());

    while (!stack.empty()) {
        const int sector_index = stack.back();
        stack.pop_back();
        const SectorPlane& sector = sectors[static_cast<std::size_t>(sector_index)];
        for (const int neighbor : sector.edge_neighbors) {
            if (!selected_set.contains(neighbor) || visited.contains(neighbor)) {
                continue;
            }
            visited.insert(neighbor);
            stack.push_back(neighbor);
        }
    }

    return visited.size() == selected.size();
}

std::vector<PolygonLoop> trace_boundary_loops(const std::vector<std::pair<GridPoint, GridPoint>>& edges) {
    std::map<GridPoint, std::vector<GridPoint>> outgoing;
    for (const auto& edge : edges) {
        outgoing[edge.first].push_back(edge.second);
    }
    for (auto& [_, targets] : outgoing) {
        std::sort(targets.begin(), targets.end());
    }

    std::set<std::pair<GridPoint, GridPoint>> visited;
    std::vector<PolygonLoop> loops;
    for (const auto& edge : edges) {
        if (visited.contains(edge)) {
            continue;
        }

        std::vector<GridPoint> points;
        GridPoint start = edge.first;
        GridPoint from = edge.first;
        GridPoint to = edge.second;
        bool closed = false;

        for (std::size_t guard = 0; guard < edges.size() + 8; ++guard) {
            if (visited.contains({from, to})) {
                break;
            }

            visited.insert({from, to});
            points.push_back(from);
            if (to == start) {
                closed = true;
                break;
            }

            const auto found = outgoing.find(to);
            if (found == outgoing.end()) {
                break;
            }

            const auto next = std::find_if(found->second.begin(), found->second.end(), [&visited, to](const GridPoint candidate) {
                return !visited.contains({to, candidate});
            });
            if (next == found->second.end()) {
                break;
            }

            from = to;
            to = *next;
        }

        if (!closed || points.size() < 3) {
            continue;
        }

        PolygonLoop loop = clean_loop(std::move(points));
        if (loop.vertices.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }

    return loops;
}

CsgAddResult merge_selected_sectors(
    const std::vector<SectorPlane>& sectors,
    const std::vector<int>& selected_indices
) {
    const std::vector<int> selected = normalize_selection(sectors, selected_indices);
    if (selected.size() < 2) {
        return CsgAddResult{false, "Select at least two sectors to merge.", sectors};
    }
    if (!selected_sectors_are_connected(sectors, selected)) {
        return CsgAddResult{false, "Selected sectors must be one connected island.", sectors};
    }

    const std::set<int> selected_set(selected.begin(), selected.end());
    std::vector<std::pair<GridPoint, GridPoint>> boundary_edges;
    std::vector<PolygonLoop> boundary_loops;

    for (const int sector_index : selected) {
        const SectorPlane& sector = sectors[static_cast<std::size_t>(sector_index)];
        for (std::size_t edge_index = 0; edge_index < sector.outer.vertices.size(); ++edge_index) {
            const int neighbor = edge_index < sector.edge_neighbors.size() ? sector.edge_neighbors[edge_index] : -1;
            if (selected_set.contains(neighbor)) {
                continue;
            }
            boundary_edges.push_back({
                to_grid(sector.outer.vertices[edge_index]),
                to_grid(sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()]),
            });
        }
        for (PolygonLoop hole : sector.holes) {
            boundary_loops.push_back(std::move(hole));
        }
    }

    std::vector<PolygonLoop> traced_loops = trace_boundary_loops(boundary_edges);
    boundary_loops.insert(
        boundary_loops.end(),
        std::make_move_iterator(traced_loops.begin()),
        std::make_move_iterator(traced_loops.end())
    );
    if (boundary_loops.empty()) {
        return CsgAddResult{false, "Merge could not trace a boundary.", sectors};
    }

    auto outer = std::max_element(boundary_loops.begin(), boundary_loops.end(), [](const PolygonLoop& a, const PolygonLoop& b) {
        return std::abs(polygon_area(a)) < std::abs(polygon_area(b));
    });
    if (outer == boundary_loops.end()) {
        return CsgAddResult{false, "Merge could not choose an outer boundary.", sectors};
    }

    SectorPlane merged;
    merged.floor_height = sectors[static_cast<std::size_t>(selected.front())].floor_height;
    merged.height = sectors[static_cast<std::size_t>(selected.front())].height;
    merged.outer = *outer;
    force_winding(merged.outer, true);
    for (std::size_t i = 0; i < boundary_loops.size(); ++i) {
        if (&boundary_loops[i] == &(*outer)) {
            continue;
        }
        force_winding(boundary_loops[i], false);
        merged.holes.push_back(boundary_loops[i]);
    }

    const TriangulationResult triangulated = triangulate_polygon(merged.outer, merged.holes);
    if (triangulated.status != TriangulationStatus::Ok) {
        return CsgAddResult{false, triangulated.message, sectors};
    }

    merged.triangles = triangulated.triangles;
    merged.status = triangulated.status;
    merged.status_message = triangulated.message;
    merged.edge_neighbors.assign(merged.outer.vertices.size(), -1);

    std::vector<SectorPlane> output;
    output.reserve(sectors.size() - selected.size() + 1);
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        if (!selected_set.contains(static_cast<int>(i))) {
            output.push_back(sectors[i]);
        }
    }
    output.push_back(std::move(merged));
    rebuild_edges(output);
    return CsgAddResult{true, "Ok", std::move(output)};
}

} // namespace

CsgAddResult csg_add_sector(const std::vector<SectorPlane>& existing_sectors, const PolygonLoop& added_outer) {
    return run_csg(existing_sectors, &added_outer, CsgOperation::Add);
}

CsgAddResult csg_subtract_sector(const std::vector<SectorPlane>& existing_sectors, const PolygonLoop& cut_loop) {
    CsgAddResult result = run_csg(existing_sectors, &cut_loop, CsgOperation::Subtract);
    if (!result.ok || !result.sectors.empty() || cut_touches_existing_edges(existing_sectors, cut_loop)) {
        return result;
    }
    return contained_hole_subtract_fallback(existing_sectors, cut_loop);
}

CsgAddResult csg_rebuild_sectors(const std::vector<SectorPlane>& sectors) {
    return run_csg(sectors, nullptr, CsgOperation::Rebuild);
}

CsgAddResult csg_merge_sectors(const std::vector<SectorPlane>& sectors, const std::vector<int>& selected_indices) {
    return merge_selected_sectors(sectors, selected_indices);
}

} // namespace undecedent
