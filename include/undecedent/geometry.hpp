#pragma once

#include <string>
#include <vector>

namespace undecedent {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct PolygonLoop {
    std::vector<Vec2> vertices;
};

struct Triangle {
    Vec2 a;
    Vec2 b;
    Vec2 c;
};

constexpr int kMaterialCount = 8;
constexpr int kDefaultMaterialId = 0;
constexpr int kDefaultDisplacementResolution = 4;
constexpr int kMinDisplacementResolution = 1;
constexpr int kMaxDisplacementResolution = 16;
constexpr float kSectorMinHeight = 8.0F;

inline int clamped_material_id(const int material_id) {
    if (material_id < 0 || material_id >= kMaterialCount) {
        return kDefaultMaterialId;
    }
    return material_id;
}

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

struct SectorDisplacementSample {
    Vec2 position;
    float offset = 0.0F;
};

struct SectorSurfaceDisplacement {
    bool enabled = false;
    int resolution = kDefaultDisplacementResolution;
    std::vector<SectorDisplacementSample> samples;
};

struct SectorPlane {
    PolygonLoop outer;
    std::vector<PolygonLoop> holes;
    std::vector<Triangle> triangles;
    std::vector<int> edge_neighbors;
    std::vector<int> wall_materials;
    std::vector<std::vector<int>> hole_wall_materials;
    int floor_material = kDefaultMaterialId;
    int ceiling_material = kDefaultMaterialId;
    float floor_height = 0.0F;
    float height = 96.0F;
    SectorSurfaceDisplacement floor_displacement;
    SectorSurfaceDisplacement ceiling_displacement;
    TriangulationStatus status = TriangulationStatus::Ok;
    std::string status_message;
};

struct MeshPlaneSource {
    PolygonLoop outer;
    std::vector<PolygonLoop> holes;
};

struct PlayerSpawn {
    Vec3 position{0.0F, 48.0F, 0.0F};
    float yaw = 0.0F;
    bool set = false;
};

struct PointLight {
    Vec3 position{0.0F, 64.0F, 0.0F};
    Vec3 color{1.0F, 0.86F, 0.62F};
    float radius = 384.0F;
    float intensity = 1.5F;
};

} // namespace undecedent
