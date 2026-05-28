#include "undecedent/csg.hpp"

#include "undecedent/triangulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

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
        return point_in_polygon_or_on(sector.outer, point);
    });
}

bool in_added_or_existing(const std::vector<SectorPlane>& sectors, const PolygonLoop& added, const Vec2 point) {
    return point_in_polygon_or_on(added, point) || in_existing_union(sectors, point);
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

} // namespace

CsgAddResult csg_add_sector(const std::vector<SectorPlane>& existing_sectors, const PolygonLoop& added_outer) {
    const TriangulationResult added_validation = triangulate_polygon(added_outer);
    if (added_validation.status != TriangulationStatus::Ok) {
        return CsgAddResult{false, added_validation.message, existing_sectors};
    }

    std::vector<Segment> segments;
    segments.reserve((existing_sectors.size() + 1) * 4);
    for (const SectorPlane& sector : existing_sectors) {
        add_loop_segments(segments, sector.outer);
    }
    add_loop_segments(segments, added_outer);

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
            if (!in_added_or_existing(existing_sectors, added_outer, sample)) {
                continue;
            }

            SectorPlane sector;
            sector.outer = loop;
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

    if (output.empty()) {
        return CsgAddResult{false, "CSG add produced no valid sectors.", existing_sectors};
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

} // namespace undecedent

