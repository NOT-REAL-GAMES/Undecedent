#pragma once

#include <string>

namespace undecedent {

struct SdfTextColor {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 0.92F;
};

struct SdfTextMetrics {
    float width = 0.0F;
    float height = 0.0F;
    int lines = 0;
};

struct SdfTextClip {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool enabled = false;
};

bool sdf_text_ready();
void sdf_text_shutdown();
void sdf_text_begin_frame();
void sdf_text_flush();
bool load_sdf_text_font(const std::string& metrics_path);
SdfTextMetrics measure_sdf_text(const std::string& text, float pixel_size);
bool draw_sdf_text(
    const std::string& text,
    float x,
    float y,
    float pixel_size,
    int screen_width,
    int screen_height,
    SdfTextColor color = {},
    SdfTextClip clip = {}
);

} // namespace undecedent
