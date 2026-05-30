#include "undecedent/debug_draw.hpp"

#include "undecedent/screen_draw.hpp"

#include <glad/glad.h>

namespace undecedent {
namespace {

void draw_stroke_segment(
    const int segment,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    const float w = size;
    const float h = size * 1.65F;
    const float mid_y = y + h * 0.5F;
    const float right_x = x + w;
    const float bottom_y = y + h;

    switch (segment) {
        case 0: draw_screen_line(x, y, right_x, y, width, height); break;
        case 1: draw_screen_line(right_x, y, right_x, mid_y, width, height); break;
        case 2: draw_screen_line(right_x, mid_y, right_x, bottom_y, width, height); break;
        case 3: draw_screen_line(x, bottom_y, right_x, bottom_y, width, height); break;
        case 4: draw_screen_line(x, mid_y, x, bottom_y, width, height); break;
        case 5: draw_screen_line(x, y, x, mid_y, width, height); break;
        case 6: draw_screen_line(x, mid_y, right_x, mid_y, width, height); break;
        default: break;
    }
}

void draw_stroke_digit(
    const char digit,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    constexpr unsigned char masks[] = {
        0b00111111,
        0b00000110,
        0b01011011,
        0b01001111,
        0b01100110,
        0b01101101,
        0b01111101,
        0b00000111,
        0b01111111,
        0b01101111,
    };

    const unsigned char mask = masks[digit - '0'];
    for (int segment = 0; segment < 7; ++segment) {
        if ((mask & (1 << segment)) != 0) {
            draw_stroke_segment(segment, x, y, size, width, height);
        }
    }
}

void draw_stroke_u(
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    draw_stroke_segment(2, x, y, size, width, height);
    draw_stroke_segment(3, x, y, size, width, height);
    draw_stroke_segment(4, x, y, size, width, height);
}

void draw_stroke_letter(
    const char letter,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    const float glyph_h = size * 1.65F;
    const auto line = [&](const float x1, const float y1, const float x2, const float y2) {
        draw_screen_line(
            x + (size * x1),
            y + (glyph_h * y1),
            x + (size * x2),
            y + (glyph_h * y2),
            width,
            height
        );
    };

    switch (letter) {
        case 'A':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            break;
        case 'B':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.82F, 0.0F);
            line(0.82F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.82F, 0.5F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            line(0.82F, 0.5F, 1.0F, 0.62F);
            line(1.0F, 0.62F, 1.0F, 0.88F);
            line(1.0F, 0.88F, 0.82F, 1.0F);
            line(0.0F, 1.0F, 0.82F, 1.0F);
            break;
        case 'C':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'D':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.78F, 0.0F);
            line(0.78F, 0.0F, 1.0F, 0.2F);
            line(1.0F, 0.2F, 1.0F, 0.8F);
            line(1.0F, 0.8F, 0.78F, 1.0F);
            line(0.0F, 1.0F, 0.78F, 1.0F);
            break;
        case 'E':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'F':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.0F, 0.5F, 0.82F, 0.5F);
            break;
        case 'G':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.55F);
            line(1.0F, 0.55F, 0.56F, 0.55F);
            break;
        case 'H':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            break;
        case 'I':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.5F, 0.0F, 0.5F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'J':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.72F, 0.0F, 0.72F, 0.82F);
            line(0.72F, 0.82F, 0.54F, 1.0F);
            line(0.54F, 1.0F, 0.12F, 1.0F);
            line(0.12F, 1.0F, 0.0F, 0.84F);
            break;
        case 'K':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(1.0F, 0.0F, 0.0F, 0.55F);
            line(0.0F, 0.55F, 1.0F, 1.0F);
            break;
        case 'L':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        case 'M':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.5F, 0.42F);
            line(0.5F, 0.42F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            break;
        case 'N':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            break;
        case 'O':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 0.0F, 0.0F);
            break;
        case 'P':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.86F, 0.0F);
            line(0.86F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.86F, 0.5F);
            line(0.0F, 0.5F, 0.86F, 0.5F);
            break;
        case 'Q':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 1.0F, 0.82F);
            line(1.0F, 0.82F, 0.82F, 1.0F);
            line(0.82F, 1.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.56F, 0.68F, 1.0F, 1.08F);
            break;
        case 'R':
            line(0.0F, 1.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.84F, 0.0F);
            line(0.84F, 0.0F, 1.0F, 0.16F);
            line(1.0F, 0.16F, 1.0F, 0.42F);
            line(1.0F, 0.42F, 0.84F, 0.5F);
            line(0.0F, 0.5F, 0.84F, 0.5F);
            line(0.42F, 0.5F, 1.0F, 1.0F);
            break;
        case 'S':
            line(1.0F, 0.0F, 0.0F, 0.0F);
            line(0.0F, 0.0F, 0.0F, 0.5F);
            line(0.0F, 0.5F, 1.0F, 0.5F);
            line(1.0F, 0.5F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 0.0F, 1.0F);
            break;
        case 'T':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(0.5F, 0.0F, 0.5F, 1.0F);
            break;
        case 'U':
            line(0.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            line(1.0F, 1.0F, 1.0F, 0.0F);
            break;
        case 'V':
            line(0.0F, 0.0F, 0.5F, 1.0F);
            line(0.5F, 1.0F, 1.0F, 0.0F);
            break;
        case 'W':
            line(0.0F, 0.0F, 0.22F, 1.0F);
            line(0.22F, 1.0F, 0.5F, 0.58F);
            line(0.5F, 0.58F, 0.78F, 1.0F);
            line(0.78F, 1.0F, 1.0F, 0.0F);
            break;
        case 'X':
            line(0.0F, 0.0F, 1.0F, 1.0F);
            line(1.0F, 0.0F, 0.0F, 1.0F);
            break;
        case 'Y':
            line(0.0F, 0.0F, 0.5F, 0.5F);
            line(1.0F, 0.0F, 0.5F, 0.5F);
            line(0.5F, 0.5F, 0.5F, 1.0F);
            break;
        case 'Z':
            line(0.0F, 0.0F, 1.0F, 0.0F);
            line(1.0F, 0.0F, 0.0F, 1.0F);
            line(0.0F, 1.0F, 1.0F, 1.0F);
            break;
        default:
            break;
    }
}

} // namespace

void draw_stroke_text(
    const std::string& label,
    const float x,
    const float y,
    const float size,
    const int width,
    const int height
) {
    float cursor_x = x;
    for (const char ch : label) {
        if (ch >= '0' && ch <= '9') {
            draw_stroke_digit(ch, cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch >= 'A' && ch <= 'Z') {
            draw_stroke_letter(ch, cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch == 'u') {
            draw_stroke_u(cursor_x, y, size, width, height);
            cursor_x += size * 1.45F;
        } else if (ch == '.') {
            draw_screen_line(
                cursor_x + size * 0.35F,
                y + size * 1.55F,
                cursor_x + size * 0.38F,
                y + size * 1.55F,
                width,
                height
            );
            cursor_x += size * 0.75F;
        } else if (ch == '/') {
            draw_screen_line(cursor_x, y + size * 1.65F, cursor_x + size, y, width, height);
            cursor_x += size * 1.05F;
        } else if (ch == '-') {
            draw_screen_line(cursor_x, y + size * 0.82F, cursor_x + size, y + size * 0.82F, width, height);
            cursor_x += size * 1.05F;
        } else {
            cursor_x += size * 0.85F;
        }
    }
}

} // namespace undecedent
