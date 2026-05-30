#pragma once

namespace undecedent {

float screen_to_ndc_x(float screen_x, int width);
float screen_to_ndc_y(float screen_y, int height);

void draw_screen_line(float x0, float y0, float x1, float y1, int width, int height);
void draw_screen_quad(float x, float y, float quad_width, float quad_height, int width, int height);

} // namespace undecedent
