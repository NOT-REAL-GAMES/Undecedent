#pragma once

#include "undecedent/geometry.hpp"

#include <vector>

namespace undecedent {

enum class SectorSurfaceKind {
    Floor,
    Ceiling,
};

struct SurfaceSampleVertex {
    Vec2 position;
    float offset = 0.0F;
    float height = 0.0F;
};

struct SurfaceSampleTriangle {
    SurfaceSampleVertex a;
    SurfaceSampleVertex b;
    SurfaceSampleVertex c;
};

struct SurfaceHeightRange {
    float min_height = 0.0F;
    float max_height = 0.0F;
};

int clamped_displacement_resolution(int resolution);
float base_surface_height(const SectorPlane& sector, SectorSurfaceKind surface);
const SectorSurfaceDisplacement& displacement_for_surface(const SectorPlane& sector, SectorSurfaceKind surface);
SectorSurfaceDisplacement& displacement_for_surface(SectorPlane& sector, SectorSurfaceKind surface);

void normalize_displacement(SectorPlane& sector, SectorSurfaceKind surface);
void ensure_displacement_samples(SectorPlane& sector, SectorSurfaceKind surface);
std::vector<SurfaceSampleTriangle> build_surface_sample_triangles(
    const SectorPlane& sector,
    SectorSurfaceKind surface
);
float sample_surface_offset(const SectorPlane& sector, SectorSurfaceKind surface, Vec2 point);
float sample_surface_height(const SectorPlane& sector, SectorSurfaceKind surface, Vec2 point);
SurfaceHeightRange sector_surface_height_range(const SectorPlane& sector, SectorSurfaceKind surface);
bool sector_displacement_enabled(const SectorPlane& sector);

bool sculpt_surface_displacement(
    SectorPlane& sector,
    SectorSurfaceKind surface,
    Vec2 center,
    float radius,
    float delta
);

void resample_displacements_from_sources(SectorPlane& sector, const std::vector<SectorPlane>& sources);

} // namespace undecedent
