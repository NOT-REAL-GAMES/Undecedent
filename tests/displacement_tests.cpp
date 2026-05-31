#include "undecedent/displacement.hpp"
#include "undecedent/triangulator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using undecedent::PolygonLoop;
using undecedent::SectorPlane;
using undecedent::Vec2;

PolygonLoop loop(std::initializer_list<Vec2> vertices) {
    return PolygonLoop{std::vector<Vec2>(vertices)};
}

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_near(const float actual, const float expected, const float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << message << " (actual=" << actual << ", expected=" << expected << ")\n";
        std::exit(EXIT_FAILURE);
    }
}

SectorPlane sector_with_triangles() {
    SectorPlane sector;
    sector.outer = loop({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
    const undecedent::TriangulationResult result = undecedent::triangulate_polygon(sector.outer, sector.holes);
    expect(result.status == undecedent::TriangulationStatus::Ok, "test sector should triangulate");
    sector.triangles = result.triangles;
    return sector;
}

} // namespace

int main() {
    {
        SectorPlane sector = sector_with_triangles();
        const std::vector<undecedent::SurfaceSampleTriangle> flat =
            undecedent::build_surface_sample_triangles(sector, undecedent::SectorSurfaceKind::Floor);
        expect(flat.size() == sector.triangles.size(), "flat sector should keep source triangle count");
        expect(flat.front().a.height == 0.0F, "flat floor should use base floor height");
    }

    {
        SectorPlane sector = sector_with_triangles();
        undecedent::set_displacement_resolution(sector, undecedent::SectorSurfaceKind::Floor, 2);
        expect(sector.floor_displacement.enabled, "ensuring samples should enable floor displacement");
        expect(!sector.floor_displacement.samples.empty(), "enabled displacement should create control samples");
        const std::vector<undecedent::SurfaceSampleTriangle> displaced =
            undecedent::build_surface_sample_triangles(sector, undecedent::SectorSurfaceKind::Floor);
        expect(displaced.size() == sector.triangles.size() * 4, "resolution 2 should split each triangle into four");
    }

    {
        SectorPlane sector = sector_with_triangles();
        sector.floor_displacement.resolution = 3;
        undecedent::ensure_displacement_samples(sector, undecedent::SectorSurfaceKind::Floor);
        const std::size_t fine_count = sector.floor_displacement.samples.size();
        for (undecedent::SectorDisplacementSample& sample : sector.floor_displacement.samples) {
            sample.offset = sample.position.x;
        }

        undecedent::set_displacement_resolution(sector, undecedent::SectorSurfaceKind::Floor, 2);
        expect(sector.floor_displacement.samples.size() < fine_count, "lower subdivision should reduce control samples");

        bool found_midpoint = false;
        for (const undecedent::SectorDisplacementSample& sample : sector.floor_displacement.samples) {
            if (std::abs(sample.position.x - 8.0F) <= 0.001F && std::abs(sample.position.y) <= 0.001F) {
                found_midpoint = true;
                expect_near(sample.offset, 8.0F, 0.01F, "decimated midpoint should interpolate old surface offset");
            }
        }
        expect(found_midpoint, "lower subdivision should generate edge midpoint sample");
    }

    {
        SectorPlane sector = sector_with_triangles();
        sector.height = 64.0F;
        const bool floor_changed = undecedent::sculpt_surface_displacement(
            sector,
            undecedent::SectorSurfaceKind::Floor,
            Vec2{8, 8},
            32.0F,
            12.0F
        );
        expect(floor_changed, "floor sculpt should change nearby samples");
        expect(undecedent::sample_surface_height(sector, undecedent::SectorSurfaceKind::Floor, Vec2{8, 8}) > 0.0F,
            "floor sample should rise after sculpt");
        expect(undecedent::sample_surface_height(sector, undecedent::SectorSurfaceKind::Ceiling, Vec2{8, 8}) == 64.0F,
            "ceiling should stay independent from floor sculpt");
    }

    {
        SectorPlane sector = sector_with_triangles();
        sector.height = 16.0F;
        const bool ceiling_changed = undecedent::sculpt_surface_displacement(
            sector,
            undecedent::SectorSurfaceKind::Ceiling,
            Vec2{8, 8},
            32.0F,
            -64.0F
        );
        expect(ceiling_changed, "ceiling sculpt should change samples");
        expect(undecedent::sample_surface_height(sector, undecedent::SectorSurfaceKind::Ceiling, Vec2{8, 8}) >=
                undecedent::sample_surface_height(sector, undecedent::SectorSurfaceKind::Floor, Vec2{8, 8}) +
                    undecedent::kSectorMinHeight,
            "ceiling sculpt should clamp above floor");
    }

    return EXIT_SUCCESS;
}
