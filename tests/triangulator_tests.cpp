#include "undecedent/triangulator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using undecedent::PolygonLoop;
using undecedent::TriangulationStatus;
using undecedent::Vec2;

PolygonLoop loop(std::initializer_list<Vec2> vertices) {
    return PolygonLoop{std::vector<Vec2>(vertices)};
}

float triangle_area(const undecedent::Triangle& triangle) {
    return std::abs(
        ((triangle.a.x * (triangle.b.y - triangle.c.y)) +
            (triangle.b.x * (triangle.c.y - triangle.a.y)) +
            (triangle.c.x * (triangle.a.y - triangle.b.y))) *
        0.5F
    );
}

float result_area(const undecedent::TriangulationResult& result) {
    float area = 0.0F;
    for (const undecedent::Triangle& triangle : result.triangles) {
        area += triangle_area(triangle);
    }
    return area;
}

void expect_status(
    const char* name,
    const undecedent::TriangulationResult& result,
    const TriangulationStatus expected
) {
    if (result.status != expected) {
        std::cerr << name << " expected " << undecedent::triangulation_status_name(expected)
                  << " got " << undecedent::triangulation_status_name(result.status)
                  << ": " << result.message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_area(
    const char* name,
    const undecedent::TriangulationResult& result,
    const float expected
) {
    const float area = result_area(result);
    if (std::abs(area - expected) > 0.01F) {
        std::cerr << name << " expected area " << expected << " got " << area << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    {
        const auto result = undecedent::triangulate_polygon(loop({{0, 0}, {128, 0}, {128, 128}, {0, 128}}));
        expect_status("convex", result, TriangulationStatus::Ok);
        expect_area("convex", result, 16384.0F);
    }

    {
        const auto result = undecedent::triangulate_polygon(loop({{0, 0}, {128, 0}, {128, 64}, {64, 32}, {0, 64}}));
        expect_status("concave", result, TriangulationStatus::Ok);
        expect_area("concave", result, 6144.0F);
    }

    {
        const auto outer = loop({{0, 0}, {256, 0}, {256, 256}, {0, 256}});
        const auto hole = loop({{96, 96}, {96, 160}, {160, 160}, {160, 96}});
        const auto result = undecedent::triangulate_polygon(outer, {hole});
        expect_status("hole", result, TriangulationStatus::Ok);
        expect_area("hole", result, 61440.0F);
    }

    {
        const auto outer = loop({{0, 0}, {256, 0}, {256, 256}, {0, 256}});
        const auto hole = loop({{300, 96}, {300, 160}, {360, 160}, {360, 96}});
        const auto result = undecedent::triangulate_polygon(outer, {hole});
        expect_status("outside hole", result, TriangulationStatus::HoleOutsideOuter);
    }

    {
        const auto outer = loop({{0, 0}, {256, 0}, {256, 256}, {0, 256}});
        const auto hole = loop({{220, 96}, {220, 160}, {280, 160}, {280, 96}});
        const auto result = undecedent::triangulate_polygon(outer, {hole});
        expect_status("outer crossing hole", result, TriangulationStatus::HoleOutsideOuter);
    }

    {
        const auto outer = loop({{0, 0}, {256, 0}, {256, 256}, {0, 256}});
        const auto hole_a = loop({{64, 64}, {64, 128}, {128, 128}, {128, 64}});
        const auto hole_b = loop({{96, 96}, {96, 160}, {160, 160}, {160, 96}});
        const auto result = undecedent::triangulate_polygon(outer, {hole_a, hole_b});
        expect_status("intersecting holes", result, TriangulationStatus::HoleIntersectsHole);
    }

    {
        const auto result = undecedent::triangulate_polygon(loop({{0, 0}, {128, 128}, {0, 128}, {128, 0}}));
        expect_status("self intersection", result, TriangulationStatus::SelfIntersection);
    }

    {
        const auto result = undecedent::triangulate_polygon(loop({{0, 0}, {128, 0}, {128, 0}, {0, 128}}));
        expect_status("duplicate", result, TriangulationStatus::DuplicateVertex);
    }

    {
        const auto result = undecedent::triangulate_polygon(loop({{0, 0}, {0, 128}, {128, 128}, {128, 0}}));
        expect_status("winding", result, TriangulationStatus::Ok);
        expect_area("winding", result, 16384.0F);
    }

    return EXIT_SUCCESS;
}
