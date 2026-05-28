#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <SDL3/SDL.h>

#include "undecedent/csg.hpp"
#include "undecedent/triangulator.hpp"

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kEditorMajorGridEvery = 4;
constexpr float kEditorBaseGridWorldUnits = 1.0F;
constexpr float kEditorMinGridPixels = 28.0F;
constexpr float kEditorMaxGridPixels = 112.0F;
constexpr float kEditorMinZoom = 1.0F / 65536.0F;
constexpr float kEditorMaxZoom = 16.0F;
constexpr float kEditorZoomStep = 1.08F;
constexpr float kEditorScrollSensitivity = 0.55F;
constexpr float kEditorScrollDeadzone = 0.02F;
constexpr float kEditorScrollLogStrength = 4.0F;
constexpr float kEditorMaxScrollDelta = 64.0F;
constexpr float kEditorCameraSmoothRate = 14.0F;
constexpr float kScaleIndicatorTargetPixels = 160.0F;
constexpr float kScaleIndicatorMinPixels = 80.0F;
constexpr float kScaleIndicatorMaxPixels = 190.0F;
constexpr float kCloseVertexPixels = 12.0F;

struct EditorCamera {
    float x = 0.0F;
    float y = 0.0F;
    float zoom = 1.0F;
    float target_x = 0.0F;
    float target_y = 0.0F;
    float target_zoom = 1.0F;
    bool panning = false;
};

enum class PlaneToolMode {
    None,
    DrawOuter,
    DrawHole,
};

struct EditorWorld {
    std::vector<undecedent::SectorPlane> sectors;
    std::vector<undecedent::Vec2> draft_vertices;
    undecedent::TriangulationResult draft_result;
    undecedent::Vec2 snapped_mouse;
    int dragged_draft_vertex = -1;
    int selected_sector = -1;
    PlaneToolMode plane_tool = PlaneToolMode::None;
    bool dragged_draft_vertex_moved = false;
};

bool configure_gl_attributes() {
    bool ok = true;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) && ok;
    ok = SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) && ok;
    return ok;
}

void log_sdl_error(const char* action) {
    std::cerr << action << ": " << SDL_GetError() << '\n';
}

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
    glVertex2f(screen_to_ndc_x(x0, width), screen_to_ndc_y(y0, height));
    glVertex2f(screen_to_ndc_x(x1, width), screen_to_ndc_y(y1, height));
}

void draw_screen_quad(
    const float x,
    const float y,
    const float quad_width,
    const float quad_height,
    const int width,
    const int height
) {
    glVertex2f(screen_to_ndc_x(x, width), screen_to_ndc_y(y, height));
    glVertex2f(screen_to_ndc_x(x + quad_width, width), screen_to_ndc_y(y, height));
    glVertex2f(screen_to_ndc_x(x + quad_width, width), screen_to_ndc_y(y + quad_height, height));
    glVertex2f(screen_to_ndc_x(x, width), screen_to_ndc_y(y + quad_height, height));
}

float screen_to_world_x(const float screen_x, const int width, const EditorCamera& camera) {
    const float center_x = static_cast<float>(width) * 0.5F;
    return ((screen_x - center_x) / camera.zoom) + camera.x;
}

float screen_to_world_y(const float screen_y, const int height, const EditorCamera& camera) {
    const float center_y = static_cast<float>(height) * 0.5F;
    return ((screen_y - center_y) / camera.zoom) + camera.y;
}

float world_to_screen_x(const float world_x, const int width, const EditorCamera& camera) {
    const float center_x = static_cast<float>(width) * 0.5F;
    return center_x + ((world_x - camera.x) * camera.zoom);
}

float world_to_screen_y(const float world_y, const int height, const EditorCamera& camera) {
    const float center_y = static_cast<float>(height) * 0.5F;
    return center_y + ((world_y - camera.y) * camera.zoom);
}

float world_to_ndc_x(const float world_x, const int width, const EditorCamera& camera) {
    return screen_to_ndc_x(world_to_screen_x(world_x, width, camera), width);
}

float world_to_ndc_y(const float world_y, const int height, const EditorCamera& camera) {
    return screen_to_ndc_y(world_to_screen_y(world_y, height, camera), height);
}

float editor_grid_world_step(const float zoom) {
    float world_step = kEditorBaseGridWorldUnits;
    float screen_step = world_step * zoom;

    while (screen_step < kEditorMinGridPixels) {
        world_step *= 2.0F;
        screen_step *= 2.0F;
    }

    while (screen_step > kEditorMaxGridPixels && world_step > kEditorBaseGridWorldUnits) {
        world_step *= 0.5F;
        screen_step *= 0.5F;
    }

    return world_step;
}

undecedent::Vec2 snap_to_grid(const undecedent::Vec2 point, const float grid_step) {
    return undecedent::Vec2{
        std::round(point.x / grid_step) * grid_step,
        std::round(point.y / grid_step) * grid_step,
    };
}

undecedent::PolygonLoop draft_loop(const EditorWorld& editor_world) {
    return undecedent::PolygonLoop{editor_world.draft_vertices};
}

bool rebuild_sector(undecedent::SectorPlane& sector) {
    const undecedent::TriangulationResult result = undecedent::triangulate_polygon(sector.outer, sector.holes);
    sector.status = result.status;
    sector.status_message = result.message;
    sector.triangles = result.triangles;
    return result.status == undecedent::TriangulationStatus::Ok;
}

bool point_in_triangle(
    const undecedent::Vec2 p,
    const undecedent::Vec2 a,
    const undecedent::Vec2 b,
    const undecedent::Vec2 c
) {
    const auto cross2 = [](const undecedent::Vec2 p0, const undecedent::Vec2 p1, const undecedent::Vec2 p2) {
        return ((p1.x - p0.x) * (p2.y - p0.y)) - ((p1.y - p0.y) * (p2.x - p0.x));
    };
    const float ab = cross2(a, b, p);
    const float bc = cross2(b, c, p);
    const float ca = cross2(c, a, p);
    return (ab >= -undecedent::kGeometryEpsilon && bc >= -undecedent::kGeometryEpsilon &&
        ca >= -undecedent::kGeometryEpsilon) ||
        (ab <= undecedent::kGeometryEpsilon && bc <= undecedent::kGeometryEpsilon &&
            ca <= undecedent::kGeometryEpsilon);
}

float distance_squared(const undecedent::Vec2 a, const undecedent::Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool same_editor_point(const undecedent::Vec2 a, const undecedent::Vec2 b) {
    return distance_squared(a, b) <= (undecedent::kGeometryEpsilon * undecedent::kGeometryEpsilon);
}

bool draft_contains_point(const EditorWorld& editor_world, const undecedent::Vec2 point) {
    return std::any_of(
        editor_world.draft_vertices.begin(),
        editor_world.draft_vertices.end(),
        [point](const undecedent::Vec2 vertex) {
            return same_editor_point(vertex, point);
        }
    );
}

bool draft_contains_point_except(
    const EditorWorld& editor_world,
    const undecedent::Vec2 point,
    const int ignored_index
) {
    for (std::size_t i = 0; i < editor_world.draft_vertices.size(); ++i) {
        if (static_cast<int>(i) == ignored_index) {
            continue;
        }

        if (same_editor_point(editor_world.draft_vertices[i], point)) {
            return true;
        }
    }
    return false;
}

int draft_vertex_at_screen(
    const EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float screen_x,
    const float screen_y
) {
    int nearest = -1;
    float nearest_distance = kCloseVertexPixels * kCloseVertexPixels;

    for (std::size_t i = 0; i < editor_world.draft_vertices.size(); ++i) {
        const undecedent::Vec2 vertex = editor_world.draft_vertices[i];
        const float vertex_x = world_to_screen_x(vertex.x, width, camera);
        const float vertex_y = world_to_screen_y(vertex.y, height, camera);
        const float dx = vertex_x - screen_x;
        const float dy = vertex_y - screen_y;
        const float distance = (dx * dx) + (dy * dy);
        if (distance <= nearest_distance) {
            nearest = static_cast<int>(i);
            nearest_distance = distance;
        }
    }

    return nearest;
}

int sector_at_point(const EditorWorld& editor_world, const undecedent::Vec2 point) {
    for (int i = static_cast<int>(editor_world.sectors.size()) - 1; i >= 0; --i) {
        const undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(i)];
        for (const undecedent::Triangle& triangle : sector.triangles) {
            if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
                return i;
            }
        }
    }
    return -1;
}

bool point_in_sector(const undecedent::SectorPlane& sector, const undecedent::Vec2 point) {
    for (const undecedent::Triangle& triangle : sector.triangles) {
        if (point_in_triangle(point, triangle.a, triangle.b, triangle.c)) {
            return true;
        }
    }
    return false;
}

undecedent::TriangulationResult draft_preview_result(const EditorWorld& editor_world) {
    if (editor_world.plane_tool == PlaneToolMode::None) {
        return {};
    }

    if (editor_world.dragged_draft_vertex >= 0) {
        return editor_world.draft_result;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawHole) {
        if (editor_world.selected_sector < 0 ||
            editor_world.selected_sector >= static_cast<int>(editor_world.sectors.size())) {
            return undecedent::TriangulationResult{
                undecedent::TriangulationStatus::TriangulationFailed,
                "No selected sector for hole drawing.",
                {}
            };
        }

        const undecedent::SectorPlane& sector =
            editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)];
        if (!point_in_sector(sector, editor_world.snapped_mouse)) {
            return undecedent::TriangulationResult{
                undecedent::TriangulationStatus::HoleOutsideOuter,
                "Preview vertex is outside the selected sector.",
                {}
            };
        }
    }

    std::vector<undecedent::Vec2> preview_vertices = editor_world.draft_vertices;
    const bool closes_loop =
        preview_vertices.size() >= 3 &&
        same_editor_point(editor_world.snapped_mouse, preview_vertices.front());

    if (!closes_loop) {
        if (draft_contains_point(editor_world, editor_world.snapped_mouse)) {
            return undecedent::TriangulationResult{
                undecedent::TriangulationStatus::DuplicateVertex,
                "Preview vertex duplicates an existing vertex.",
                {}
            };
        }

        preview_vertices.push_back(editor_world.snapped_mouse);
    }

    if (preview_vertices.size() < 3) {
        return undecedent::TriangulationResult{
            undecedent::TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
    }

    const undecedent::PolygonLoop preview_loop{preview_vertices};
    if (editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        return undecedent::triangulate_polygon(preview_loop);
    }

    const undecedent::SectorPlane& sector =
        editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)];
    std::vector<undecedent::PolygonLoop> holes = sector.holes;
    holes.push_back(preview_loop);
    return undecedent::triangulate_polygon(sector.outer, holes);
}

void refresh_draft(EditorWorld& editor_world) {
    if (editor_world.draft_vertices.size() < 3) {
        editor_world.draft_result = undecedent::TriangulationResult{
            undecedent::TriangulationStatus::NotEnoughVertices,
            "Loop needs at least three vertices.",
            {}
        };
        return;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        editor_world.draft_result = undecedent::triangulate_polygon(draft_loop(editor_world));
        return;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawHole &&
        editor_world.selected_sector >= 0 &&
        editor_world.selected_sector < static_cast<int>(editor_world.sectors.size())) {
        const undecedent::SectorPlane& sector =
            editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)];
        std::vector<undecedent::PolygonLoop> holes = sector.holes;
        holes.push_back(draft_loop(editor_world));
        editor_world.draft_result = undecedent::triangulate_polygon(sector.outer, holes);
        return;
    }

    editor_world.draft_result = undecedent::TriangulationResult{
        undecedent::TriangulationStatus::TriangulationFailed,
        "No selected sector for hole drawing.",
        {}
    };
}

void update_snapped_mouse(
    EditorWorld& editor_world,
    const EditorCamera& camera,
    const int width,
    const int height,
    const float mouse_x,
    const float mouse_y
) {
    const undecedent::Vec2 world{
        screen_to_world_x(mouse_x, width, camera),
        screen_to_world_y(mouse_y, height, camera),
    };
    editor_world.snapped_mouse = snap_to_grid(world, editor_grid_world_step(camera.zoom));
}

void cancel_plane_tool(EditorWorld& editor_world) {
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.dragged_draft_vertex = -1;
    editor_world.dragged_draft_vertex_moved = false;
    editor_world.plane_tool = PlaneToolMode::None;
}

void start_outer_plane(EditorWorld& editor_world) {
    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.plane_tool = PlaneToolMode::DrawOuter;
    std::cout << "Plane tool: draw outer loop\n";
}

void start_hole_plane(EditorWorld& editor_world) {
    if (editor_world.selected_sector < 0 ||
        editor_world.selected_sector >= static_cast<int>(editor_world.sectors.size())) {
        std::cout << "Select a sector before drawing a hole.\n";
        return;
    }

    editor_world.draft_vertices.clear();
    editor_world.draft_result = {};
    editor_world.plane_tool = PlaneToolMode::DrawHole;
    std::cout << "Plane tool: draw hole loop\n";
}

bool commit_plane_tool(EditorWorld& editor_world) {
    refresh_draft(editor_world);
    if (editor_world.draft_result.status != undecedent::TriangulationStatus::Ok) {
        std::cout << "Cannot commit loop: " << editor_world.draft_result.message << '\n';
        return false;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        const undecedent::CsgAddResult csg_result =
            undecedent::csg_add_sector(editor_world.sectors, draft_loop(editor_world));
        if (!csg_result.ok) {
            std::cout << "Cannot commit CSG add: " << csg_result.message << '\n';
            return false;
        }

        editor_world.sectors = csg_result.sectors;
        editor_world.selected_sector = editor_world.sectors.empty()
            ? -1
            : static_cast<int>(editor_world.sectors.size()) - 1;
        cancel_plane_tool(editor_world);
        std::cout << "Committed CSG add; sectors: " << editor_world.sectors.size() << '\n';
        return true;
    }

    if (editor_world.plane_tool == PlaneToolMode::DrawHole) {
        undecedent::SectorPlane& sector = editor_world.sectors[static_cast<std::size_t>(editor_world.selected_sector)];
        sector.holes.push_back(draft_loop(editor_world));
        rebuild_sector(sector);
        cancel_plane_tool(editor_world);
        std::cout << "Committed hole for sector " << editor_world.selected_sector << '\n';
        return true;
    }

    return false;
}

std::uint64_t scale_indicator_world_units(const float zoom) {
    const double safe_zoom = std::max(static_cast<double>(zoom), static_cast<double>(kEditorMinZoom));
    const double target_world_units = std::max(1.0, kScaleIndicatorTargetPixels / safe_zoom);
    double world_units = std::pow(2.0, std::floor(std::log2(target_world_units)));
    double screen_pixels = world_units * safe_zoom;

    while (screen_pixels < kScaleIndicatorMinPixels) {
        world_units *= 2.0;
        screen_pixels *= 2.0;
    }

    while (screen_pixels > kScaleIndicatorMaxPixels && world_units > 1.0) {
        world_units *= 0.5;
        screen_pixels *= 0.5;
    }

    return static_cast<std::uint64_t>(std::max(1.0, world_units));
}

float editor_scroll_zoom_delta(const float scroll_y) {
    const float magnitude = std::abs(scroll_y);
    if (magnitude <= kEditorScrollDeadzone) {
        return 0.0F;
    }

    const float clamped = std::min(magnitude, kEditorMaxScrollDelta);
    const float curved = std::log1p(clamped * kEditorScrollLogStrength) / std::log1p(kEditorScrollLogStrength);
    const float sign = scroll_y < 0.0F ? -1.0F : 1.0F;
    return sign * curved * kEditorScrollSensitivity;
}

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

void draw_scale_label(
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
        } else if (ch == 'u') {
            draw_stroke_u(cursor_x, y, size, width, height);
        }
        cursor_x += size * 1.45F;
    }
}

void apply_editor_scroll_zoom(
    EditorCamera& camera,
    const float scroll_y,
    const float mouse_x,
    const float mouse_y,
    const int width,
    const int height
) {
    if (scroll_y == 0.0F || width <= 0 || height <= 0) {
        return;
    }

    EditorCamera target_camera = camera;
    target_camera.x = camera.target_x;
    target_camera.y = camera.target_y;
    target_camera.zoom = camera.target_zoom;

    const float before_x = screen_to_world_x(mouse_x, width, target_camera);
    const float before_y = screen_to_world_y(mouse_y, height, target_camera);
    const float scroll_delta = editor_scroll_zoom_delta(scroll_y);
    if (scroll_delta == 0.0F) {
        return;
    }

    const float zoom_factor = std::pow(kEditorZoomStep, scroll_delta);
    target_camera.zoom = std::clamp(target_camera.zoom * zoom_factor, kEditorMinZoom, kEditorMaxZoom);
    const float after_x = screen_to_world_x(mouse_x, width, target_camera);
    const float after_y = screen_to_world_y(mouse_y, height, target_camera);

    camera.target_zoom = target_camera.zoom;
    camera.target_x += before_x - after_x;
    camera.target_y += before_y - after_y;
}

void update_editor_camera(EditorCamera& camera, const float dt) {
    const float t = 1.0F - std::exp(-kEditorCameraSmoothRate * dt);
    camera.x += (camera.target_x - camera.x) * t;
    camera.y += (camera.target_y - camera.y) * t;

    const float current_zoom_log = std::log(camera.zoom);
    const float target_zoom_log = std::log(camera.target_zoom);
    camera.zoom = std::exp(current_zoom_log + ((target_zoom_log - current_zoom_log) * t));
}

void draw_editor_grid(const int width, const int height, const EditorCamera& camera) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0F);

    const float grid_step = editor_grid_world_step(camera.zoom);
    const float min_world_x = screen_to_world_x(0.0F, width, camera);
    const float max_world_x = screen_to_world_x(static_cast<float>(width), width, camera);
    const float min_world_y = screen_to_world_y(0.0F, height, camera);
    const float max_world_y = screen_to_world_y(static_cast<float>(height), height, camera);

    glBegin(GL_LINES);
    const int first_vertical = static_cast<int>(std::floor(min_world_x / grid_step)) - 1;
    const int last_vertical = static_cast<int>(std::floor(max_world_x / grid_step)) + 1;
    for (int index = first_vertical; index <= last_vertical; ++index) {
        const bool major = (index % kEditorMajorGridEvery) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_x = static_cast<float>(index) * grid_step;
        const float screen_x = world_to_screen_x(world_x, width, camera);
        const float ndc_x = screen_to_ndc_x(screen_x, width);

        glColor4f(0.18F, 0.78F, 0.68F, alpha);
        glVertex2f(ndc_x, -1.0F);
        glVertex2f(ndc_x, 1.0F);
    }

    const int first_horizontal = static_cast<int>(std::floor(min_world_y / grid_step)) - 1;
    const int last_horizontal = static_cast<int>(std::floor(max_world_y / grid_step)) + 1;
    for (int index = first_horizontal; index <= last_horizontal; ++index) {
        const bool major = (index % kEditorMajorGridEvery) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_y = static_cast<float>(index) * grid_step;
        const float screen_y = world_to_screen_y(world_y, height, camera);
        const float ndc_y = screen_to_ndc_y(screen_y, height);

        glColor4f(0.18F, 0.78F, 0.68F, alpha);
        glVertex2f(-1.0F, ndc_y);
        glVertex2f(1.0F, ndc_y);
    }

    glColor4f(0.90F, 0.96F, 0.76F, 0.55F);
    const float origin_x = screen_to_ndc_x(world_to_screen_x(0.0F, width, camera), width);
    const float origin_y = screen_to_ndc_y(world_to_screen_y(0.0F, height, camera), height);
    glVertex2f(origin_x, -1.0F);
    glVertex2f(origin_x, 1.0F);
    glVertex2f(-1.0F, origin_y);
    glVertex2f(1.0F, origin_y);
    glEnd();

    glDisable(GL_BLEND);
}

void draw_loop_outline(
    const undecedent::PolygonLoop& loop,
    const int width,
    const int height,
    const EditorCamera& camera,
    const float red,
    const float green,
    const float blue,
    const float alpha
) {
    if (loop.vertices.empty()) {
        return;
    }

    glColor4f(red, green, blue, alpha);
    glBegin(GL_LINE_LOOP);
    for (const undecedent::Vec2 vertex : loop.vertices) {
        glVertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    glEnd();
}

void draw_vertex_marker(
    const undecedent::Vec2 point,
    const int width,
    const int height,
    const EditorCamera& camera,
    const float red,
    const float green,
    const float blue,
    const float alpha
) {
    const float screen_x = world_to_screen_x(point.x, width, camera);
    const float screen_y = world_to_screen_y(point.y, height, camera);
    glColor4f(red, green, blue, alpha);
    draw_screen_quad(screen_x - 3.0F, screen_y - 3.0F, 6.0F, 6.0F, width, height);
}

void draw_sector_planes(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_TRIANGLES);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        const bool selected = static_cast<int>(i) == editor_world.selected_sector;
        if (selected) {
            glColor4f(0.36F, 0.78F, 0.68F, 0.28F);
        } else {
            glColor4f(0.22F, 0.58F, 0.68F, 0.20F);
        }

        for (const undecedent::Triangle& triangle : editor_world.sectors[i].triangles) {
            glVertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
    }
    glEnd();

    glLineWidth(2.0F);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        const bool selected = static_cast<int>(i) == editor_world.selected_sector;
        const float alpha = selected ? 0.95F : 0.72F;
        draw_loop_outline(editor_world.sectors[i].outer, width, height, camera, 0.72F, 0.95F, 0.86F, alpha);
        for (const undecedent::PolygonLoop& hole : editor_world.sectors[i].holes) {
            draw_loop_outline(hole, width, height, camera, 0.95F, 0.78F, 0.42F, alpha);
        }
    }

    glBegin(GL_LINES);
    glColor4f(0.30F, 0.62F, 1.0F, 0.78F);
    for (const undecedent::SectorPlane& sector : editor_world.sectors) {
        for (std::size_t edge_index = 0; edge_index < sector.edge_neighbors.size(); ++edge_index) {
            if (sector.edge_neighbors[edge_index] < 0) {
                continue;
            }

            const undecedent::Vec2 a = sector.outer.vertices[edge_index];
            const undecedent::Vec2 b = sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()];
            glVertex2f(world_to_ndc_x(a.x, width, camera), world_to_ndc_y(a.y, height, camera));
            glVertex2f(world_to_ndc_x(b.x, width, camera), world_to_ndc_y(b.y, height, camera));
        }
    }
    glEnd();

    glBegin(GL_QUADS);
    for (const undecedent::SectorPlane& sector : editor_world.sectors) {
        for (const undecedent::Vec2 vertex : sector.outer.vertices) {
            draw_vertex_marker(vertex, width, height, camera, 0.90F, 0.96F, 0.76F, 0.85F);
        }
        for (const undecedent::PolygonLoop& hole : sector.holes) {
            for (const undecedent::Vec2 vertex : hole.vertices) {
                draw_vertex_marker(vertex, width, height, camera, 0.95F, 0.78F, 0.42F, 0.85F);
            }
        }
    }
    glEnd();

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_draft_plane(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    if (editor_world.plane_tool == PlaneToolMode::None || editor_world.draft_vertices.empty()) {
        return;
    }

    const undecedent::TriangulationResult preview_result = draft_preview_result(editor_world);
    const bool valid = preview_result.status == undecedent::TriangulationStatus::Ok;
    const bool pending = preview_result.status == undecedent::TriangulationStatus::NotEnoughVertices;
    const float red = valid || pending ? 0.90F : 1.0F;
    const float green = valid ? 0.96F : (pending ? 0.78F : 0.25F);
    const float blue = valid ? 0.76F : (pending ? 0.42F : 0.20F);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (valid && editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        glBegin(GL_TRIANGLES);
        glColor4f(0.90F, 0.96F, 0.76F, 0.14F);
        for (const undecedent::Triangle& triangle : preview_result.triangles) {
            glVertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            glVertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
        glEnd();
    }

    glLineWidth(2.0F);
    glBegin(GL_LINE_STRIP);
    glColor4f(red, green, blue, 0.92F);
    for (const undecedent::Vec2 vertex : editor_world.draft_vertices) {
        glVertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    glVertex2f(
        world_to_ndc_x(editor_world.snapped_mouse.x, width, camera),
        world_to_ndc_y(editor_world.snapped_mouse.y, height, camera)
    );
    glEnd();

    if (editor_world.draft_vertices.size() >= 3) {
        glBegin(GL_LINES);
        glColor4f(red, green, blue, 0.42F);
        const undecedent::Vec2 first = editor_world.draft_vertices.front();
        glVertex2f(world_to_ndc_x(editor_world.snapped_mouse.x, width, camera), world_to_ndc_y(editor_world.snapped_mouse.y, height, camera));
        glVertex2f(world_to_ndc_x(first.x, width, camera), world_to_ndc_y(first.y, height, camera));
        glEnd();
    }

    glBegin(GL_QUADS);
    for (const undecedent::Vec2 vertex : editor_world.draft_vertices) {
        draw_vertex_marker(vertex, width, height, camera, red, green, blue, 0.95F);
    }
    draw_vertex_marker(editor_world.snapped_mouse, width, height, camera, red, green, blue, 0.55F);
    glEnd();

    glLineWidth(1.0F);
    glDisable(GL_BLEND);
}

void draw_scale_indicator(const int width, const int height, const EditorCamera& camera) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const std::uint64_t world_units = scale_indicator_world_units(camera.zoom);
    const float bar_pixels = static_cast<float>(static_cast<double>(world_units) * camera.zoom);
    const float origin_x = 18.0F;
    const float origin_y = static_cast<float>(height) - 28.0F;
    const float label_y = static_cast<float>(height) - 56.0F;
    const std::string label = std::to_string(world_units) + "u";

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.0F, 0.0F, 0.0F, 0.34F);
    draw_screen_quad(10.0F, static_cast<float>(height) - 70.0F, bar_pixels + 34.0F, 58.0F, width, height);
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    glColor4f(0.90F, 0.96F, 0.76F, 0.88F);
    draw_screen_line(origin_x, origin_y, origin_x + bar_pixels, origin_y, width, height);
    draw_screen_line(origin_x, origin_y - 7.0F, origin_x, origin_y + 7.0F, width, height);
    draw_screen_line(origin_x + bar_pixels, origin_y - 7.0F, origin_x + bar_pixels, origin_y + 7.0F, width, height);
    glEnd();

    glLineWidth(1.0F);
    glBegin(GL_LINES);
    draw_scale_label(label, origin_x, label_y, 7.0F, width, height);
    glEnd();

    glDisable(GL_BLEND);
}

void set_editor_enabled(SDL_Window* window, const bool enabled) {
    SDL_SetWindowTitle(window, enabled ? "Undecedent - Editor" : "Undecedent");
    std::cout << "Editor " << (enabled ? "enabled" : "disabled") << '\n';
}
} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl_error("SDL_Init failed");
        return EXIT_FAILURE;
    }

    if (!configure_gl_attributes()) {
        log_sdl_error("OpenGL attribute setup failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Undecedent",
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr) {
        log_sdl_error("SDL_CreateWindow failed");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        log_sdl_error("SDL_GL_CreateContext failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "gladLoadGLLoader failed\n";
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GL_SetSwapInterval(0);

    bool running = true;
    bool editor_enabled = false;
    EditorCamera editor_camera{};
    EditorWorld editor_world{};
    Uint64 previous_ticks = SDL_GetTicksNS();
    while (running) {
        const Uint64 current_ticks = SDL_GetTicksNS();
        const float dt = std::min(
            static_cast<float>(current_ticks - previous_ticks) / 1'000'000'000.0F,
            0.1F
        );
        previous_ticks = current_ticks;

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = event.key.key;

                if (editor_enabled && key == SDLK_P && !event.key.repeat) {
                    start_outer_plane(editor_world);
                    continue;
                }

                if (editor_enabled && key == SDLK_H && !event.key.repeat) {
                    start_hole_plane(editor_world);
                    continue;
                }

                if (editor_enabled && key == SDLK_RETURN && editor_world.plane_tool != PlaneToolMode::None) {
                    commit_plane_tool(editor_world);
                    continue;
                }

                if (editor_enabled && key == SDLK_BACKSPACE && editor_world.plane_tool != PlaneToolMode::None) {
                    if (!editor_world.draft_vertices.empty()) {
                        editor_world.draft_vertices.pop_back();
                        refresh_draft(editor_world);
                    }
                    continue;
                }

                if (editor_enabled && key == SDLK_ESCAPE && editor_world.plane_tool != PlaneToolMode::None) {
                    cancel_plane_tool(editor_world);
                    continue;
                }

                if (key == SDLK_ESCAPE || key == SDLK_Q) {
                    running = false;
                }

                if (key == SDLK_F1 && !event.key.repeat) {
                    editor_enabled = !editor_enabled;
                    editor_camera.panning = false;
                    if (!editor_enabled) {
                        cancel_plane_tool(editor_world);
                    }
                    set_editor_enabled(window, editor_enabled);
                }
            }

            if (!editor_enabled) {
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                    editor_camera.panning = true;
                }

                if (event.button.button == SDL_BUTTON_LEFT) {
                    int width = 0;
                    int height = 0;
                    SDL_GetWindowSizeInPixels(window, &width, &height);
                    update_snapped_mouse(editor_world, editor_camera, width, height, event.button.x, event.button.y);

                    if (editor_world.plane_tool != PlaneToolMode::None) {
                        const int vertex_index = draft_vertex_at_screen(
                            editor_world,
                            editor_camera,
                            width,
                            height,
                            event.button.x,
                            event.button.y
                        );

                        if (vertex_index >= 0) {
                            const bool double_clicked_last_vertex =
                                event.button.clicks >= 2 &&
                                editor_world.draft_vertices.size() >= 3 &&
                                vertex_index == static_cast<int>(editor_world.draft_vertices.size()) - 1;

                            if (double_clicked_last_vertex) {
                                commit_plane_tool(editor_world);
                            } else {
                                editor_world.dragged_draft_vertex = vertex_index;
                                editor_world.dragged_draft_vertex_moved = false;
                            }
                        } else if (draft_contains_point(editor_world, editor_world.snapped_mouse)) {
                            std::cout << "Vertex already exists in active loop.\n";
                        } else {
                            editor_world.draft_vertices.push_back(editor_world.snapped_mouse);
                            refresh_draft(editor_world);
                        }
                    } else {
                        const undecedent::Vec2 world{
                            screen_to_world_x(event.button.x, width, editor_camera),
                            screen_to_world_y(event.button.y, height, editor_camera),
                        };
                        editor_world.selected_sector = sector_at_point(editor_world, world);
                        if (editor_world.selected_sector >= 0) {
                            std::cout << "Selected sector " << editor_world.selected_sector << '\n';
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                    editor_camera.panning = false;
                }

                if (event.button.button == SDL_BUTTON_LEFT && editor_world.dragged_draft_vertex >= 0) {
                    const bool should_close =
                        !editor_world.dragged_draft_vertex_moved &&
                        editor_world.dragged_draft_vertex == 0 &&
                        editor_world.draft_vertices.size() >= 3;

                    if (should_close) {
                        commit_plane_tool(editor_world);
                    } else {
                        refresh_draft(editor_world);
                    }

                    editor_world.dragged_draft_vertex = -1;
                    editor_world.dragged_draft_vertex_moved = false;
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && editor_camera.panning) {
                const float pan_x = event.motion.xrel / editor_camera.zoom;
                const float pan_y = event.motion.yrel / editor_camera.zoom;
                editor_camera.x -= pan_x;
                editor_camera.y -= pan_y;
                editor_camera.target_x -= pan_x;
                editor_camera.target_y -= pan_y;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);
                update_snapped_mouse(editor_world, editor_camera, width, height, event.motion.x, event.motion.y);

                if (editor_world.dragged_draft_vertex >= 0 &&
                    editor_world.dragged_draft_vertex < static_cast<int>(editor_world.draft_vertices.size()) &&
                    !draft_contains_point_except(
                        editor_world,
                        editor_world.snapped_mouse,
                        editor_world.dragged_draft_vertex
                    )) {
                    undecedent::Vec2& dragged =
                        editor_world.draft_vertices[static_cast<std::size_t>(editor_world.dragged_draft_vertex)];
                    if (!same_editor_point(dragged, editor_world.snapped_mouse)) {
                        dragged = editor_world.snapped_mouse;
                        editor_world.dragged_draft_vertex_moved = true;
                        refresh_draft(editor_world);
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSizeInPixels(window, &width, &height);

                float scroll_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    scroll_y *= -1.0F;
                }

                apply_editor_scroll_zoom(
                    editor_camera,
                    scroll_y,
                    event.wheel.mouse_x,
                    event.wheel.mouse_y,
                    width,
                    height
                );
            }
        }

        if (editor_enabled) {
            update_editor_camera(editor_camera, dt);
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (editor_enabled) {
            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            update_snapped_mouse(editor_world, editor_camera, width, height, mouse_x, mouse_y);
        }
        glViewport(0, 0, width, height);
        if (editor_enabled) {
            glClearColor(0.025F, 0.035F, 0.04F, 1.0F);
        } else {
            glClearColor(0.02F, 0.025F, 0.03F, 1.0F);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (editor_enabled) {
            draw_editor_grid(width, height, editor_camera);
            draw_sector_planes(editor_world, width, height, editor_camera);
            draw_draft_plane(editor_world, width, height, editor_camera);
            draw_scale_indicator(width, height, editor_camera);
        }

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
