#pragma once

#include <string>
#include <vector>

namespace undecedent {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct PolygonLoop {
    std::vector<Vec2> vertices;
};

struct Triangle {
    Vec2 a;
    Vec2 b;
    Vec2 c;
};

enum class TriangulationStatus {
    Ok,
    NotEnoughVertices,
    DuplicateVertex,
    DegeneratePolygon,
    SelfIntersection,
    HoleOutsideOuter,
    HoleIntersectsOuter,
    HoleIntersectsHole,
    BridgeFailed,
    TriangulationFailed,
};

struct TriangulationResult {
    TriangulationStatus status = TriangulationStatus::Ok;
    std::string message;
    std::vector<Triangle> triangles;
};

struct SectorPlane {
    PolygonLoop outer;
    std::vector<PolygonLoop> holes;
    std::vector<Triangle> triangles;
    std::vector<int> edge_neighbors;
    TriangulationStatus status = TriangulationStatus::Ok;
    std::string status_message;
};

struct MeshPlaneSource {
    PolygonLoop outer;
    std::vector<PolygonLoop> holes;
};

} // namespace undecedent
