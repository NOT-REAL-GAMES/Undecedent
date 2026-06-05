#include "undecedent/screen_draw.hpp"

#include <glad/glad.h>

#include "undecedent/core_draw.hpp"

namespace undecedent {

float screen_to_ndc_x(const float screen_x, const int width) {
    return (2.0F * screen_x / static_cast<float>(width)) - 1.0F;
}

float screen_to_ndc_y(const float screen_y, const int height) {
    return 1.0F - (2.0F * screen_y / static_cast<float>(height));
}

void draw_screen_line(
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const int width,
    const int height
) {
    core_vertex2f(screen_to_ndc_x(x0, width), screen_to_ndc_y(y0, height));
    core_vertex2f(screen_to_ndc_x(x1, width), screen_to_ndc_y(y1, height));
}

void draw_screen_quad(
    const float x,
    const float y,
    const float quad_width,
    const float quad_height,
    const int width,
    const int height
) {
    core_vertex2f(screen_to_ndc_x(x, width), screen_to_ndc_y(y, height));
    core_vertex2f(screen_to_ndc_x(x + quad_width, width), screen_to_ndc_y(y, height));
    core_vertex2f(screen_to_ndc_x(x + quad_width, width), screen_to_ndc_y(y + quad_height, height));
    core_vertex2f(screen_to_ndc_x(x, width), screen_to_ndc_y(y + quad_height, height));
}

} // namespace undecedent
