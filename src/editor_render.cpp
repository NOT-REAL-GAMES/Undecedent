#include "undecedent/editor_render.hpp"

#include "undecedent/debug_draw.hpp"
#include "undecedent/screen_draw.hpp"
#include "undecedent/sdf_text.hpp"

#include <glad/glad.h>

#include "undecedent/core_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace undecedent {
namespace {

std::uint64_t scale_indicator_world_units(const float zoom, const Editor2DRenderConfig& config) {
    const double raw_units = static_cast<double>(config.scale_indicator_target_pixels) / static_cast<double>(zoom);
    const double exponent = std::floor(std::log2(std::max(raw_units, 1.0)));
    std::uint64_t units = 1ULL << static_cast<int>(std::clamp(exponent, 0.0, 63.0));
    units = std::max<std::uint64_t>(1ULL, units);

    const auto pixels_for = [&](const std::uint64_t value) {
        return static_cast<float>(static_cast<double>(value) * zoom);
    };

    while (units > 1 && pixels_for(units) > config.scale_indicator_max_pixels) {
        units /= 2ULL;
    }
    while (units < (1ULL << 63) && pixels_for(units) < config.scale_indicator_min_pixels) {
        units *= 2ULL;
    }
    return units;
}

void draw_editor_grid(
    const int width,
    const int height,
    const EditorCamera& camera,
    const Editor2DRenderConfig& config
) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_set_line_width(1.0F);

    const float grid_step = editor_grid_world_step(camera.zoom);
    const float min_world_x = screen_to_world_x(0.0F, width, camera);
    const float max_world_x = screen_to_world_x(static_cast<float>(width), width, camera);
    const float min_world_y = screen_to_world_y(0.0F, height, camera);
    const float max_world_y = screen_to_world_y(static_cast<float>(height), height, camera);

    core_begin(GL_LINES);
    const int first_vertical = static_cast<int>(std::floor(min_world_x / grid_step)) - 1;
    const int last_vertical = static_cast<int>(std::floor(max_world_x / grid_step)) + 1;
    for (int index = first_vertical; index <= last_vertical; ++index) {
        const bool major = (index % config.major_grid_every) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_x = static_cast<float>(index) * grid_step;
        const float screen_x = world_to_screen_x(world_x, width, camera);
        const float ndc_x = screen_to_ndc_x(screen_x, width);

        core_color4f(0.18F, 0.78F, 0.68F, alpha);
        core_vertex2f(ndc_x, -1.0F);
        core_vertex2f(ndc_x, 1.0F);
    }

    const int first_horizontal = static_cast<int>(std::floor(min_world_y / grid_step)) - 1;
    const int last_horizontal = static_cast<int>(std::floor(max_world_y / grid_step)) + 1;
    for (int index = first_horizontal; index <= last_horizontal; ++index) {
        const bool major = (index % config.major_grid_every) == 0;
        const float alpha = major ? 0.30F : 0.13F;
        const float world_y = static_cast<float>(index) * grid_step;
        const float screen_y = world_to_screen_y(world_y, height, camera);
        const float ndc_y = screen_to_ndc_y(screen_y, height);

        core_color4f(0.18F, 0.78F, 0.68F, alpha);
        core_vertex2f(-1.0F, ndc_y);
        core_vertex2f(1.0F, ndc_y);
    }

    core_color4f(0.90F, 0.96F, 0.76F, 0.55F);
    const float origin_x = screen_to_ndc_x(world_to_screen_x(0.0F, width, camera), width);
    const float origin_y = screen_to_ndc_y(world_to_screen_y(0.0F, height, camera), height);
    core_vertex2f(origin_x, -1.0F);
    core_vertex2f(origin_x, 1.0F);
    core_vertex2f(-1.0F, origin_y);
    core_vertex2f(1.0F, origin_y);
    core_end();

    glDisable(GL_BLEND);
}

void draw_loop_outline(
    const PolygonLoop& loop,
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

    core_color4f(red, green, blue, alpha);
    core_begin(GL_LINE_LOOP);
    for (const Vec2 vertex : loop.vertices) {
        core_vertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    core_end();
}

void draw_vertex_marker(
    const Vec2 point,
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
    core_color4f(red, green, blue, alpha);
    draw_screen_quad(screen_x - 3.0F, screen_y - 3.0F, 6.0F, 6.0F, width, height);
}

void draw_sector_planes(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    core_begin(GL_TRIANGLES);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        if (!sector_visible_in_slice(editor_world, editor_world.sectors[i])) {
            continue;
        }
        const bool selected = is_sector_selected(editor_world, static_cast<int>(i));
        if (selected) {
            core_color4f(0.36F, 0.78F, 0.68F, 0.28F);
        } else {
            core_color4f(0.22F, 0.58F, 0.68F, 0.20F);
        }

        for (const Triangle& triangle : editor_world.sectors[i].triangles) {
            core_vertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            core_vertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            core_vertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
    }
    core_end();

    core_set_line_width(2.0F);
    for (std::size_t i = 0; i < editor_world.sectors.size(); ++i) {
        if (!sector_visible_in_slice(editor_world, editor_world.sectors[i])) {
            continue;
        }
        const bool selected = is_sector_selected(editor_world, static_cast<int>(i));
        const float alpha = selected ? 0.95F : 0.72F;
        draw_loop_outline(editor_world.sectors[i].outer, width, height, camera, 0.72F, 0.95F, 0.86F, alpha);
        for (const PolygonLoop& hole : editor_world.sectors[i].holes) {
            draw_loop_outline(hole, width, height, camera, 0.95F, 0.78F, 0.42F, alpha);
        }
    }

    core_begin(GL_LINES);
    core_color4f(0.30F, 0.62F, 1.0F, 0.78F);
    for (const SectorPlane& sector : editor_world.sectors) {
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (std::size_t edge_index = 0; edge_index < sector.edge_neighbors.size(); ++edge_index) {
            if (sector.edge_neighbors[edge_index] < 0) {
                continue;
            }

            const Vec2 a = sector.outer.vertices[edge_index];
            const Vec2 b = sector.outer.vertices[(edge_index + 1) % sector.outer.vertices.size()];
            core_vertex2f(world_to_ndc_x(a.x, width, camera), world_to_ndc_y(a.y, height, camera));
            core_vertex2f(world_to_ndc_x(b.x, width, camera), world_to_ndc_y(b.y, height, camera));
        }
    }
    core_end();

    core_begin(kCoreQuads);
    for (std::size_t sector_index = 0; sector_index < editor_world.sectors.size(); ++sector_index) {
        const SectorPlane& sector = editor_world.sectors[sector_index];
        if (!sector_visible_in_slice(editor_world, sector)) {
            continue;
        }
        for (std::size_t vertex_index = 0; vertex_index < sector.outer.vertices.size(); ++vertex_index) {
            const Vec2 vertex = sector.outer.vertices[vertex_index];
            if (is_dragged_committed_ref(editor_world, sector_index, vertex_index)) {
                draw_vertex_marker(vertex, width, height, camera, 1.0F, 0.28F, 0.22F, 0.98F);
            } else if (editor_world.has_hovered_committed_vertex &&
                same_editor_point(editor_world.hovered_committed_vertex, vertex)) {
                draw_vertex_marker(vertex, width, height, camera, 0.36F, 0.78F, 1.0F, 0.98F);
            } else {
                draw_vertex_marker(vertex, width, height, camera, 0.90F, 0.96F, 0.76F, 0.85F);
            }
        }
        for (const PolygonLoop& hole : sector.holes) {
            for (const Vec2 vertex : hole.vertices) {
                draw_vertex_marker(vertex, width, height, camera, 0.95F, 0.78F, 0.42F, 0.85F);
            }
        }
    }
    core_end();

    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_draft_plane(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    if (editor_world.plane_tool == PlaneToolMode::None || editor_world.draft_vertices.empty()) {
        return;
    }

    const TriangulationResult preview_result = draft_preview_result(editor_world);
    const bool knife = editor_world.plane_tool == PlaneToolMode::DrawKnife;
    const bool valid = preview_result.status == TriangulationStatus::Ok || knife;
    const bool pending = preview_result.status == TriangulationStatus::NotEnoughVertices;
    const float red = knife ? 1.0F : (valid || pending ? 0.90F : 1.0F);
    const float green = knife ? 0.82F : (valid ? 0.96F : (pending ? 0.78F : 0.25F));
    const float blue = knife ? 0.25F : (valid ? 0.76F : (pending ? 0.42F : 0.20F));

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (valid && editor_world.plane_tool == PlaneToolMode::DrawOuter) {
        core_begin(GL_TRIANGLES);
        core_color4f(0.90F, 0.96F, 0.76F, 0.14F);
        for (const Triangle& triangle : preview_result.triangles) {
            core_vertex2f(world_to_ndc_x(triangle.a.x, width, camera), world_to_ndc_y(triangle.a.y, height, camera));
            core_vertex2f(world_to_ndc_x(triangle.b.x, width, camera), world_to_ndc_y(triangle.b.y, height, camera));
            core_vertex2f(world_to_ndc_x(triangle.c.x, width, camera), world_to_ndc_y(triangle.c.y, height, camera));
        }
        core_end();
    }

    core_set_line_width(2.0F);
    core_begin(GL_LINE_STRIP);
    core_color4f(red, green, blue, 0.92F);
    for (const Vec2 vertex : editor_world.draft_vertices) {
        core_vertex2f(world_to_ndc_x(vertex.x, width, camera), world_to_ndc_y(vertex.y, height, camera));
    }
    core_vertex2f(
        world_to_ndc_x(editor_world.snapped_mouse.x, width, camera),
        world_to_ndc_y(editor_world.snapped_mouse.y, height, camera)
    );
    core_end();

    if (!knife && editor_world.draft_vertices.size() >= 3) {
        core_begin(GL_LINES);
        core_color4f(red, green, blue, 0.42F);
        const Vec2 first = editor_world.draft_vertices.front();
        core_vertex2f(world_to_ndc_x(editor_world.snapped_mouse.x, width, camera), world_to_ndc_y(editor_world.snapped_mouse.y, height, camera));
        core_vertex2f(world_to_ndc_x(first.x, width, camera), world_to_ndc_y(first.y, height, camera));
        core_end();
    }

    core_begin(kCoreQuads);
    for (const Vec2 vertex : editor_world.draft_vertices) {
        draw_vertex_marker(vertex, width, height, camera, red, green, blue, 0.95F);
    }
    draw_vertex_marker(editor_world.snapped_mouse, width, height, camera, red, green, blue, 0.55F);
    core_end();

    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_scale_indicator(
    const int width,
    const int height,
    const EditorCamera& camera,
    const float slice_z,
    const Editor2DRenderConfig& config
) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const std::uint64_t world_units = scale_indicator_world_units(camera.zoom, config);
    const float bar_pixels = static_cast<float>(static_cast<double>(world_units) * camera.zoom);
    const float origin_x = 18.0F;
    const float origin_y = static_cast<float>(height) - 28.0F;
    const float label_y = static_cast<float>(height) - 56.0F;
    const std::string label = std::to_string(world_units) + "u";
    const std::string z_label = "Z " + format_world_units(slice_z) + "u";

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    core_begin(kCoreQuads);
    core_color4f(0.0F, 0.0F, 0.0F, 0.34F);
    draw_screen_quad(10.0F, static_cast<float>(height) - 88.0F, std::max(bar_pixels + 34.0F, 112.0F), 76.0F, width, height);
    core_end();

    core_set_line_width(2.0F);
    core_begin(GL_LINES);
    core_color4f(0.90F, 0.96F, 0.76F, 0.88F);
    draw_screen_line(origin_x, origin_y, origin_x + bar_pixels, origin_y, width, height);
    draw_screen_line(origin_x, origin_y - 7.0F, origin_x, origin_y + 7.0F, width, height);
    draw_screen_line(origin_x + bar_pixels, origin_y - 7.0F, origin_x + bar_pixels, origin_y + 7.0F, width, height);
    core_end();

    core_set_line_width(1.0F);
    if (!draw_sdf_text(label, origin_x, label_y - 1.0F, 15.4F, width, height)) {
        core_begin(GL_LINES);
        core_color4f(1.0F, 1.0F, 1.0F, 0.92F);
        draw_stroke_text(label, origin_x, label_y, 7.0F, width, height);
        core_end();
    }
    if (!draw_sdf_text(z_label, origin_x, label_y - 19.0F, 15.4F, width, height)) {
        core_begin(GL_LINES);
        core_color4f(1.0F, 1.0F, 1.0F, 0.92F);
        draw_stroke_text(z_label, origin_x, label_y - 18.0F, 7.0F, width, height);
        core_end();
    }

    glDisable(GL_BLEND);
}

void draw_player_spawn_2d(
    const EditorWorld& editor_world,
    const int width,
    const int height,
    const EditorCamera& camera,
    const Editor2DRenderConfig& config
) {
    const PlayerSpawn spawn = player_spawn_from_entities(editor_world.entities);
    if (!spawn.set || width <= 0 || height <= 0) {
        return;
    }

    const float spawn_floor = spawn.position.y - config.player_eye_height;
    const float visible_band = std::max(1.0F, editor_grid_world_step(camera.zoom) * 0.5F);
    if (std::abs(spawn_floor - editor_world.slice_z) > visible_band) {
        return;
    }

    const float x = world_to_screen_x(spawn.position.x, width, camera);
    const float y = world_to_screen_y(spawn.position.z, height, camera);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_color4f(1.0F, 0.82F, 0.25F, 0.95F);
    core_set_line_width(2.0F);
    core_begin(GL_LINES);
    draw_screen_line(x - 10.0F, y, x + 10.0F, y, width, height);
    draw_screen_line(x, y - 10.0F, x, y + 10.0F, width, height);
    const float facing_x = x - std::sin(spawn.yaw) * 18.0F;
    const float facing_y = y - std::cos(spawn.yaw) * 18.0F;
    draw_screen_line(x, y, facing_x, facing_y, width, height);
    core_end();
    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

void draw_point_lights_2d(const EditorWorld& editor_world, const int width, const int height, const EditorCamera& camera) {
    const std::vector<PointLight> point_lights = point_lights_from_entities(editor_world.entities);
    if (width <= 0 || height <= 0 || point_lights.empty()) {
        return;
    }

    const float visible_band = std::max(1.0F, editor_grid_world_step(camera.zoom) * 0.5F);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    core_set_line_width(2.0F);
    core_begin(GL_LINES);
    core_color4f(1.0F, 0.86F, 0.35F, 0.92F);
    for (const PointLight& light : point_lights) {
        if (std::abs(light.position.y - editor_world.slice_z) > visible_band) {
            continue;
        }
        const float x = world_to_screen_x(light.position.x, width, camera);
        const float y = world_to_screen_y(light.position.z, height, camera);
        draw_screen_line(x - 8.0F, y, x + 8.0F, y, width, height);
        draw_screen_line(x, y - 8.0F, x, y + 8.0F, width, height);
        draw_screen_line(x - 5.0F, y - 5.0F, x + 5.0F, y + 5.0F, width, height);
        draw_screen_line(x - 5.0F, y + 5.0F, x + 5.0F, y - 5.0F, width, height);
    }
    core_end();
    core_set_line_width(1.0F);
    glDisable(GL_BLEND);
}

} // namespace

void draw_editor_2d_view(
    const EditorWorld& editor_world,
    const int width,
    const int height,
    const EditorCamera& camera,
    const Editor2DRenderConfig& config
) {
    draw_editor_grid(width, height, camera, config);
    draw_sector_planes(editor_world, width, height, camera);
    draw_draft_plane(editor_world, width, height, camera);
    draw_point_lights_2d(editor_world, width, height, camera);
    draw_player_spawn_2d(editor_world, width, height, camera, config);
    draw_scale_indicator(width, height, camera, editor_world.slice_z, config);
}

} // namespace undecedent
