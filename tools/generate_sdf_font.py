#!/usr/bin/env python3
import argparse
import math
import pathlib
import tempfile

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont


def write_ppm(path, image):
    image = image.convert("RGB")
    with open(path, "wb") as output:
        output.write(f"P6\n{image.width} {image.height}\n255\n".encode("ascii"))
        output.write(image.tobytes())


def distance_to(mask):
    h = len(mask)
    w = len(mask[0]) if h else 0
    inf = 1.0e20
    dist = [[0.0 if mask[y][x] else inf for x in range(w)] for y in range(h)]
    diag = math.sqrt(2.0)
    for y in range(h):
        for x in range(w):
            best = dist[y][x]
            if x > 0:
                best = min(best, dist[y][x - 1] + 1.0)
            if y > 0:
                best = min(best, dist[y - 1][x] + 1.0)
                if x > 0:
                    best = min(best, dist[y - 1][x - 1] + diag)
                if x + 1 < w:
                    best = min(best, dist[y - 1][x + 1] + diag)
            dist[y][x] = best
    for y in range(h - 1, -1, -1):
        for x in range(w - 1, -1, -1):
            best = dist[y][x]
            if x + 1 < w:
                best = min(best, dist[y][x + 1] + 1.0)
            if y + 1 < h:
                best = min(best, dist[y + 1][x] + 1.0)
                if x > 0:
                    best = min(best, dist[y + 1][x - 1] + diag)
                if x + 1 < w:
                    best = min(best, dist[y + 1][x + 1] + diag)
            dist[y][x] = best
    return dist


def sdf_from_alpha(alpha, spread):
    pixels = alpha.load()
    inside = [[pixels[x, y] > 127 for x in range(alpha.width)] for y in range(alpha.height)]
    outside_dist = distance_to(inside)
    inside_dist = distance_to([[not value for value in row] for row in inside])
    sdf = Image.new("L", alpha.size, 0)
    out = sdf.load()
    for y in range(alpha.height):
        for x in range(alpha.width):
            signed = outside_dist[y][x] - inside_dist[y][x]
            value = 0.5 - signed / (2.0 * spread)
            out[x, y] = max(0, min(255, int(round(value * 255.0))))
    return sdf


def edge_color_masks(alpha):
    pixels = alpha.load()
    width = alpha.width
    height = alpha.height
    binary = [[pixels[x, y] > 127 for x in range(width)] for y in range(height)]
    masks = [
        [[False for _ in range(width)] for _ in range(height)],
        [[False for _ in range(width)] for _ in range(height)],
        [[False for _ in range(width)] for _ in range(height)],
    ]
    boundary = [[False for _ in range(width)] for _ in range(height)]

    for y in range(height):
        for x in range(width):
            center = binary[y][x]
            is_boundary = False
            for oy, ox in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                nx = x + ox
                ny = y + oy
                if 0 <= nx < width and 0 <= ny < height and binary[ny][nx] != center:
                    is_boundary = True
                    break
            if not is_boundary:
                continue

            left = pixels[max(0, x - 1), y]
            right = pixels[min(width - 1, x + 1), y]
            up = pixels[x, max(0, y - 1)]
            down = pixels[x, min(height - 1, y + 1)]
            gx = float(right) - float(left)
            gy = float(down) - float(up)
            angle = math.atan2(gy, gx)
            if angle < 0.0:
                angle += math.pi
            if angle >= math.pi:
                angle -= math.pi

            if angle < math.pi / 3.0:
                channel = 0
            elif angle < (2.0 * math.pi) / 3.0:
                channel = 1
            else:
                channel = 2
            masks[channel][y][x] = True
            boundary[y][x] = True

    for mask in masks:
        has_edge = any(any(row) for row in mask)
        if not has_edge:
            for y in range(height):
                for x in range(width):
                    mask[y][x] = boundary[y][x]
    return binary, boundary, masks


def msdf_from_alpha(alpha, spread):
    binary, boundary, masks = edge_color_masks(alpha)
    full_distance = distance_to(boundary)
    channel_distances = [distance_to(mask) for mask in masks]
    image = Image.new("RGB", alpha.size, (0, 0, 0))
    out = image.load()
    for y in range(alpha.height):
        for x in range(alpha.width):
            values = []
            for channel in range(3):
                dist = channel_distances[channel][y][x]
                if dist > 1.0e18:
                    dist = full_distance[y][x]
                signed = -dist if binary[y][x] else dist
                value = 0.5 - signed / (2.0 * spread)
                values.append(max(0, min(255, int(round(value * 255.0)))))
            out[x, y] = tuple(values)
    return image


def convert_woff2_to_ttf(woff2_path, ttf_path):
    font = TTFont(str(woff2_path))
    font.flavor = None
    font.save(str(ttf_path))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True)
    parser.add_argument("--atlas", required=True)
    parser.add_argument("--metrics", required=True)
    parser.add_argument("--size", type=int, default=48)
    parser.add_argument("--spread", type=int, default=8)
    parser.add_argument("--padding", type=int, default=6)
    parser.add_argument("--atlas-size", type=int, default=1024)
    args = parser.parse_args()

    font_path = pathlib.Path(args.font)
    atlas_path = pathlib.Path(args.atlas)
    metrics_path = pathlib.Path(args.metrics)
    atlas_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        ttf_path = pathlib.Path(tmp) / "font.ttf"
        convert_woff2_to_ttf(font_path, ttf_path)
        font = ImageFont.truetype(str(ttf_path), args.size)

        ascent, descent = font.getmetrics()
        line_height = ascent + descent
        glyphs = []
        for code in range(32, 127):
            ch = chr(code)
            bbox = font.getbbox(ch)
            advance = float(font.getlength(ch))
            if code == 32:
                glyphs.append({
                    "code": code,
                    "image": Image.new("RGB", (1, 1), (0, 0, 0)),
                    "w": 1,
                    "h": 1,
                    "xoffset": 0.0,
                    "yoffset": 0.0,
                    "advance": advance,
                })
                continue

            left, top, right, bottom = bbox
            glyph_w = max(1, right - left)
            glyph_h = max(1, bottom - top)
            bitmap = Image.new("L", (glyph_w + args.spread * 2, glyph_h + args.spread * 2), 0)
            draw = ImageDraw.Draw(bitmap)
            draw.text((args.spread - left, args.spread - top), ch, font=font, fill=255)
            glyphs.append({
                "code": code,
                "image": msdf_from_alpha(bitmap, args.spread),
                "w": bitmap.width,
                "h": bitmap.height,
                "xoffset": float(left - args.spread),
                "yoffset": float(top - args.spread),
                "advance": advance,
            })

        atlas_size = args.atlas_size
        atlas = Image.new("RGB", (atlas_size, atlas_size), (0, 0, 0))
        x = args.padding
        y = args.padding
        row_h = 0
        for glyph in glyphs:
            gw = glyph["w"]
            gh = glyph["h"]
            if x + gw + args.padding > atlas_size:
                x = args.padding
                y += row_h + args.padding
                row_h = 0
            if y + gh + args.padding > atlas_size:
                raise RuntimeError("Atlas too small for glyph set")
            atlas.paste(glyph["image"], (x, y))
            glyph["x"] = x
            glyph["y"] = y
            x += gw + args.padding
            row_h = max(row_h, gh)

        write_ppm(atlas_path, atlas)
        with open(metrics_path, "w", encoding="utf-8", newline="\n") as output:
            output.write("msdf_font 1\n")
            output.write(f"atlas {atlas_path.name} {atlas_size} {atlas_size}\n")
            output.write(f"size {args.size}\n")
            output.write(f"spread {args.spread}\n")
            output.write(f"ascent {ascent}\n")
            output.write(f"descent {descent}\n")
            output.write(f"line_height {line_height}\n")
            output.write(f"glyphs {len(glyphs)}\n")
            for glyph in glyphs:
                output.write(
                    "glyph {code} {x} {y} {w} {h} {xoffset:.6f} {yoffset:.6f} {advance:.6f}\n".format(**glyph)
                )


if __name__ == "__main__":
    main()
