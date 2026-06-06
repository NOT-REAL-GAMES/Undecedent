#include "undecedent/core_draw.hpp"

#include <cassert>

using namespace undecedent;

namespace {

void test_primitive_conversion_counts() {
    assert(core_debug_converted_vertex_count(GL_TRIANGLES, 9) == 9);
    assert(core_debug_converted_vertex_count(GL_LINES, 8) == 8);
    assert(core_debug_converted_vertex_count(GL_LINE_STRIP, 4) == 6);
    assert(core_debug_converted_vertex_count(GL_LINE_LOOP, 4) == 8);
    assert(core_debug_converted_vertex_count(kCoreQuads, 8) == 12);
}

void test_thick_screen_line_counts() {
    assert(core_debug_screen_line_triangle_count(GL_LINES, 4, 2.0F) == 12);
    assert(core_debug_screen_line_triangle_count(GL_LINE_STRIP, 4, 2.0F) == 18);
    assert(core_debug_screen_line_triangle_count(GL_LINE_LOOP, 4, 2.0F) == 24);
    assert(core_debug_screen_line_triangle_count(GL_LINES, 4, 1.0F) == 0);
    assert(core_debug_screen_line_triangle_count(GL_TRIANGLES, 6, 3.0F) == 0);
}

} // namespace

int main() {
    test_primitive_conversion_counts();
    test_thick_screen_line_counts();
    return 0;
}
