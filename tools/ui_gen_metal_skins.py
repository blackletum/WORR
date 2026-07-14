#!/usr/bin/env python3
"""Generate the WORR grimy-metal RmlUi widget sprite sheet.

Produces assets/ui/rml/common/skins/metal/ui-metal.png (a 2x-resolution
sprite sheet) plus assets/ui/rml/common/skins/metal/backdrop.png (a seamless
dark plate tile), and rewrites the generated @spritesheet block inside
assets/ui/rml/common/theme/base.rcss between the GENERATED markers.

The sheet is deterministic for a given seed so re-running the tool produces
stable output. All sprites share one lighting model: state variants (hover,
focus, active) reuse the identical base plate and only change edge lighting,
so RmlUi's hard decorator swaps still read as a soft glow change on top of the
RCSS color transitions.

Slicing model per widget kind:
  - buttons / inputs / tracks: ninepatch (small size variance, stretch is safe)
  - panels / popup / dropdown / screen grime: ninepatch frames with
    transparent (or near-flat) centers; noise smears along the stretch axis
    read as brushed metal, and large-area texture comes from the seamless
    standalone tiles below. RmlUi rejects repeat fit modes on sprites, so
    frames cannot tile — they stretch.
  - backdrop.png / plate.png: standalone seamless textures, periodic on both
    axes, used with `decorator: image(<path> repeat)`. Requires the renderer
    bridge to register RmlUi file textures with IF_REPEAT wrap.

Usage: python tools/ui_gen_metal_skins.py [--seed N] [--out-dir DIR]
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = REPO_ROOT / "assets/ui/rml/common/skins/metal"
BASE_RCSS = REPO_ROOT / "assets/ui/rml/common/theme/base.rcss"

MARK_BEGIN = "/* BEGIN GENERATED worr-metal spritesheet (tools/ui_gen_metal_skins.py) */"
MARK_END = "/* END GENERATED worr-metal spritesheet */"

SCALE = 2  # physical pixels per logical pixel; matches `resolution: 2x`
GUTTER = 4  # physical px between packed sprites, filled with edge replication


def hex_rgb(code: str) -> np.ndarray:
    code = code.lstrip("#")
    return np.array([int(code[i : i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


# Palette anchored to the existing RCSS tokens so the evolved theme stays
# recognizably the same design.
PAL = {
    "bg": hex_rgb("#12110f"),
    "plate": hex_rgb("#24211c"),
    "plate_hi": hex_rgb("#2e2a24"),
    "plate_lo": hex_rgb("#1a1815"),
    "panel": hex_rgb("#1b1916"),
    "well": hex_rgb("#171511"),
    "steel": hex_rgb("#756a5d"),
    "steel_dark": hex_rgb("#5f5548"),
    "steel_darker": hex_rgb("#4b4235"),
    "brass": hex_rgb("#b99b5b"),
    "gold": hex_rgb("#ffd967"),
    "rust": hex_rgb("#7a4326"),
    "rust_hi": hex_rgb("#a05c32"),
    "rust_lo": hex_rgb("#4f2d1a"),
    "green": hex_rgb("#83d18f"),
    "green_deep": hex_rgb("#1f271d"),
    "green_edge": hex_rgb("#5f8765"),
    "red": hex_rgb("#ee6758"),
    "red_deep": hex_rgb("#281b18"),
    "red_edge": hex_rgb("#895047"),
    "teal": hex_rgb("#7ed4d8"),
    "teal_deep": hex_rgb("#426f72"),
    "text": hex_rgb("#f3eee4"),
    "muted": hex_rgb("#c9beb0"),
}


# ---------------------------------------------------------------------------
# Noise helpers (all periodic-capable so tiled edges stay seamless)
# ---------------------------------------------------------------------------

def value_noise(rng, w, h, cell, tile_x=False, tile_y=False):
    """Bilinear value noise in [0,1]; periodic per axis when requested."""
    gw = max(1, w // cell) + (0 if tile_x else 1)
    gh = max(1, h // cell) + (0 if tile_y else 1)
    grid = rng.random((gh, gw), dtype=np.float32)

    xs = np.linspace(0, gw if tile_x else gw - 1, w, endpoint=False, dtype=np.float32)
    ys = np.linspace(0, gh if tile_y else gh - 1, h, endpoint=False, dtype=np.float32)
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    x1 = (x0 + 1) % gw if tile_x else np.minimum(x0 + 1, gw - 1)
    y1 = (y0 + 1) % gh if tile_y else np.minimum(y0 + 1, gh - 1)
    x0 %= gw
    y0 %= gh

    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    fx = fx[None, :]
    fy = fy[:, None]

    a = grid[np.ix_(y0, x0)]
    b = grid[np.ix_(y0, x1)]
    c = grid[np.ix_(y1, x0)]
    d = grid[np.ix_(y1, x1)]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(rng, w, h, cell, octaves=4, tile_x=False, tile_y=False):
    total = np.zeros((h, w), dtype=np.float32)
    amp, norm = 1.0, 0.0
    for _ in range(octaves):
        total += amp * value_noise(rng, w, h, max(2, cell), tile_x, tile_y)
        norm += amp
        amp *= 0.5
        cell = max(2, cell // 2)
    return total / norm


def brushed(rng, w, h, tile_x=False):
    """Horizontal brushed-metal streaks in [-1,1]."""
    line = value_noise(rng, w, h, 2, tile_x=tile_x, tile_y=False)
    kernel = 9
    padded = np.concatenate([line[:, -kernel:], line, line[:, :kernel]], axis=1)
    smooth = np.stack([padded[:, i : i + w] for i in range(2 * kernel + 1)]).mean(axis=0)
    return (smooth - 0.5) * 2.0


# ---------------------------------------------------------------------------
# Canvas: float RGBA in [0,255]
# ---------------------------------------------------------------------------

class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.rgb = np.zeros((h, w, 3), dtype=np.float32)
        self.a = np.zeros((h, w), dtype=np.float32)

    def fill(self, color, alpha=255.0):
        self.rgb[:] = color
        self.a[:] = alpha

    def blend(self, color, alpha_map):
        """alpha_map in [0,1] blends color over current content."""
        m = np.clip(alpha_map, 0.0, 1.0)[..., None]
        self.rgb = self.rgb * (1 - m) + np.asarray(color, dtype=np.float32) * m
        self.a = np.maximum(self.a, np.clip(alpha_map, 0.0, 1.0) * 255.0)

    def to_image(self):
        out = np.concatenate([self.rgb, self.a[..., None]], axis=2)
        return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def edge_distance(w, h):
    xs = np.arange(w, dtype=np.float32)
    ys = np.arange(h, dtype=np.float32)
    dx = np.minimum(xs, w - 1 - xs)[None, :]
    dy = np.minimum(ys, h - 1 - ys)[:, None]
    return np.minimum(np.broadcast_to(dx, (h, w)), np.broadcast_to(dy, (h, w)))


def draw_rect(c: Canvas, x, y, w, h, color, alpha=1.0):
    x, y = int(round(x)), int(round(y))
    w, h = int(round(w)), int(round(h))
    x2, y2 = min(c.w, x + w), min(c.h, y + h)
    x, y = max(0, x), max(0, y)
    if x2 <= x or y2 <= y:
        return
    m = np.zeros((c.h, c.w), dtype=np.float32)
    m[y:y2, x:x2] = alpha
    c.blend(color, m)


def draw_circle(c: Canvas, cx, cy, r, color, alpha=1.0, soft=1.0):
    ys, xs = np.mgrid[0 : c.h, 0 : c.w].astype(np.float32)
    d = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    m = np.clip((r - d) / max(soft, 0.001), 0, 1) * alpha
    c.blend(color, m)


def rivet(c: Canvas, cx, cy, r):
    draw_circle(c, cx, cy + 0.8, r + 0.8, PAL["plate_lo"] * 0.55, alpha=0.8)
    draw_circle(c, cx, cy, r, PAL["steel_dark"], alpha=1.0)
    draw_circle(c, cx - r * 0.3, cy - r * 0.35, r * 0.45, PAL["muted"], alpha=0.5)
    draw_circle(c, cx + r * 0.25, cy + r * 0.3, r * 0.4, PAL["plate_lo"], alpha=0.55)


def scratches(c: Canvas, rng, count, color, alpha=0.16):
    for _ in range(count):
        x0 = rng.uniform(2, c.w - 2)
        y0 = rng.uniform(2, c.h - 2)
        length = rng.uniform(c.w * 0.08, c.w * 0.35)
        ang = rng.uniform(-0.35, 0.35) + (0 if rng.random() < 0.8 else np.pi / 2)
        steps = max(2, int(length))
        m = np.zeros((c.h, c.w), dtype=np.float32)
        for s in range(steps):
            px = int(x0 + np.cos(ang) * s)
            py = int(y0 + np.sin(ang) * s)
            if 0 <= px < c.w and 0 <= py < c.h:
                m[py, px] = alpha
        c.blend(color, m)


def grime_layer(c: Canvas, rng, strength=0.35, edge_bias=1.0, tile_x=False, tile_y=False):
    """Rust + soot mottling, biased toward edges when edge_bias > 0."""
    h, w = c.h, c.w
    n = fbm(rng, w, h, max(4, w // 6), octaves=4, tile_x=tile_x, tile_y=tile_y)
    if edge_bias > 0:
        ed = edge_distance(w, h)
        bias = np.clip(1.0 - ed / (0.35 * min(w, h) + 1e-3), 0, 1) ** 1.5
        n = n * (0.35 + 0.65 * bias * edge_bias)
    rust_m = np.clip((n - 0.52) * 3.0, 0, 1) * strength
    soot_m = np.clip((fbm(rng, w, h, max(4, w // 4), 3, tile_x, tile_y) - 0.58) * 2.6, 0, 1) * strength * 0.8
    c.blend(PAL["rust"], rust_m * 0.85)
    c.blend(PAL["rust_hi"], np.clip((n - 0.68) * 4.0, 0, 1) * strength * 0.6)
    c.blend(PAL["rust_lo"], soot_m)


def pitting(c: Canvas, rng, density=0.05, alpha=0.35):
    m = (rng.random((c.h, c.w)) < density).astype(np.float32) * alpha
    c.blend(PAL["plate_lo"] * 0.6, m)


def base_plate(c: Canvas, rng, tone, mottle=0.5, brush=0.35, tile_x=False, tile_y=False):
    c.fill(tone)
    mot = fbm(rng, c.w, c.h, max(4, c.w // 5), 4, tile_x, tile_y) - 0.5
    c.rgb += mot[..., None] * 18.0 * mottle
    if brush > 0:
        c.rgb += brushed(rng, c.w, c.h, tile_x=tile_x)[..., None] * 10.0 * brush
    c.a[:] = 255.0


def bevel(c: Canvas, inset, width, raised=True, strength=0.5):
    """Edge lighting: raised = light top/left, dark bottom/right."""
    hi = PAL["text"]
    lo = np.array([0, 0, 0], dtype=np.float32)
    top, bottom = (hi, lo) if raised else (lo, hi)
    for i in range(width):
        a = strength * (1.0 - i / max(1, width)) * 0.5
        y = inset + i
        draw_rect(c, inset, y, c.w - 2 * inset, 1, top, a)
        draw_rect(c, inset, c.h - 1 - y, c.w - 2 * inset, 1, bottom, a)
        draw_rect(c, inset + i, inset, 1, c.h - 2 * inset, top, a * 0.8)
        draw_rect(c, c.w - 1 - inset - i, inset, 1, c.h - 2 * inset, bottom, a * 0.8)


def frame(c: Canvas, rng, width, color, wear=0.4):
    """Painted metal frame with per-pixel tone jitter (worn paint)."""
    m = np.zeros((c.h, c.w), dtype=np.float32)
    m[:width, :] = 1
    m[-width:, :] = 1
    m[:, :width] = 1
    m[:, -width:] = 1
    jitter = (rng.random((c.h, c.w), dtype=np.float32) - 0.5) * wear
    c.blend(color, m)
    c.rgb += (m * jitter * 26.0)[..., None]


def edge_glow(c: Canvas, color, width, strength):
    ed = edge_distance(c.w, c.h)
    m = np.clip(1.0 - ed / max(width, 1), 0, 1) ** 1.6 * strength
    c.blend(color, m)


def inner_shadow(c: Canvas, width, strength=0.5):
    ed = edge_distance(c.w, c.h)
    m = np.clip(1.0 - ed / max(width, 1), 0, 1) ** 1.3 * strength
    c.blend(np.array([0, 0, 0], dtype=np.float32), m)


# ---------------------------------------------------------------------------
# Sprite painters. Sizes are LOGICAL px; the painter gets physical px canvas.
# ---------------------------------------------------------------------------

def paint_cta(c, rng, kind="normal"):
    """Topbar PLAY / card CTA slab: slime-green ramp with idle edge light."""
    tones = {
        "normal": PAL["green_deep"],
        "hover": PAL["green_deep"],
        "active": hex_rgb("#2c4028"),
        "disabled": PAL["plate_lo"],
    }
    base_plate(c, rng, tones.get(kind, PAL["green_deep"]), mottle=0.5, brush=0.45)
    if kind != "disabled":
        grime_layer(c, rng, strength=0.22, edge_bias=1.4)
        pitting(c, rng, 0.025, 0.28)
        scratches(c, rng, 3, PAL["muted"], 0.08)
    bevel(c, 2, 3, raised=(kind != "active"), strength=0.55)
    frame(c, rng, 3, PAL["green_edge"] if kind != "disabled" else PAL["steel_darker"],
          wear=0.5)
    glows = {"normal": 0.22, "hover": 0.42, "active": 0.5}
    if kind in glows:
        edge_glow(c, PAL["green"], c.w * 0.14, glows[kind])
    if kind == "active":
        inner_shadow(c, c.w * 0.18, 0.3)
    rivet(c, 7, 7, 2.6)
    rivet(c, c.w - 8, 7, 2.6)
    rivet(c, 7, c.h - 8, 2.6)
    rivet(c, c.w - 8, c.h - 8, 2.6)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.5 + gray * 0.5


def paint_backplate(c, rng, kind="normal"):
    """Square riveted back button with a baked left chevron."""
    base_plate(c, rng, PAL["plate_hi"], mottle=0.5, brush=0.35)
    grime_layer(c, rng, strength=0.3, edge_bias=1.2)
    pitting(c, rng, 0.03, 0.3)
    bevel(c, 2, 3, raised=(kind != "active"), strength=0.55)
    frame(c, rng, 3, PAL["steel_dark"], wear=0.55)
    if kind == "hover":
        edge_glow(c, PAL["brass"], c.w * 0.16, 0.35)
    elif kind == "active":
        edge_glow(c, PAL["gold"], c.w * 0.16, 0.45)
        inner_shadow(c, c.w * 0.2, 0.3)
    rivet(c, 7, 7, 2.4)
    rivet(c, c.w - 8, 7, 2.4)
    rivet(c, 7, c.h - 8, 2.4)
    rivet(c, c.w - 8, c.h - 8, 2.4)
    # Left-pointing chevron.
    color = PAL["gold"] if kind != "normal" else PAL["muted"]
    cx, cy = c.w * 0.54, c.h * 0.5
    arm = c.w * 0.16
    for t in np.linspace(0, 1, int(c.w * 0.5)):
        px = cx - arm * (1 - t)
        draw_circle(c, cx - arm + arm * t, cy - arm * t, SCALE * 1.1, color, 0.95, soft=1.0)
        draw_circle(c, cx - arm + arm * t, cy + arm * t, SCALE * 1.1, color, 0.95, soft=1.0)


def paint_card_frame(c, rng, kind="rest"):
    """Hollow frame for key-art cards: steel rest, brass hover, gold selected."""
    base_plate(c, rng, PAL["plate"], mottle=0.5, brush=0.3)
    grime_layer(c, rng, strength=0.3, edge_bias=1.4)
    bevel(c, 1, 3, raised=True, strength=0.4)
    colors = {"rest": PAL["steel_darker"], "hover": PAL["brass"], "selected": PAL["gold"]}
    frame(c, rng, 4, colors.get(kind, PAL["steel_darker"]), wear=0.45)
    if kind == "hover":
        edge_glow(c, PAL["brass"], 8 * SCALE, 0.4)
    elif kind == "selected":
        edge_glow(c, PAL["gold"], 10 * SCALE, 0.55)
    rivet(c, 8, 8, 2.6)
    rivet(c, c.w - 9, 8, 2.6)
    rivet(c, 8, c.h - 9, 2.6)
    rivet(c, c.w - 9, c.h - 9, 2.6)
    hollow_center(c, 14 * SCALE)


CARD_THEMES = {
    "campaign": {"wash": "green", "glyph": "chevrons"},
    "load": {"wash": "teal", "glyph": "slots"},
    "host": {"wash": "orange", "glyph": "mast"},
    "servers": {"wash": "steel", "glyph": "rack"},
}

CARD_WASHES = {
    "green": (hex_rgb("#1f271d"), hex_rgb("#83d18f")),
    "teal": (hex_rgb("#16262a"), hex_rgb("#7ed4d8")),
    "orange": (hex_rgb("#241811"), hex_rgb("#ed842e")),
    "steel": (hex_rgb("#1d1a16"), hex_rgb("#c9beb0")),
}


def paint_card_art(c, rng, theme):
    """Owned key-art tile for a play card: washed metal + thematic glyph +
    baked bottom gradient band so titles always sit on darkened backing."""
    spec = CARD_THEMES[theme]
    deep, accent = CARD_WASHES[spec["wash"]]
    base_plate(c, rng, deep, mottle=0.6, brush=0.4)
    grime_layer(c, rng, strength=0.35, edge_bias=1.1)
    pitting(c, rng, 0.03, 0.3)
    scratches(c, rng, 6, PAL["muted"], 0.1)

    # Diagonal hazard band across the upper third.
    ys, xs = np.mgrid[0 : c.h, 0 : c.w].astype(np.float32)
    diag = ((xs + ys) / (14.0 * SCALE)) % 2.0
    band = ((ys > c.h * 0.16) & (ys < c.h * 0.22) & (diag < 1.0)).astype(np.float32)
    c.blend(accent * 0.55, band * 0.28)

    cx, cy = c.w / 2.0, c.h * 0.46
    glyph = spec["glyph"]
    if glyph == "chevrons":
        for row in range(3):
            oy = cy + (row - 1) * c.h * 0.13
            for t in np.linspace(0, 1, int(c.w * 0.5)):
                px = c.w * 0.28 + t * c.w * 0.22
                draw_circle(c, px, oy + (1 - t) * c.h * 0.08, SCALE * 2.2, accent, 0.85, soft=1.4)
                draw_circle(c, c.w - px, oy + (1 - t) * c.h * 0.08, SCALE * 2.2, accent, 0.85, soft=1.4)
    elif glyph == "slots":
        for row in range(3):
            oy = cy + (row - 1) * c.h * 0.12
            draw_rect(c, c.w * 0.28, oy - 4 * SCALE, c.w * 0.44, 8 * SCALE, deep * 0.5, 0.9)
            draw_rect(c, c.w * 0.30, oy - 2 * SCALE, c.w * 0.40 * (0.4 + 0.3 * row), 4 * SCALE, accent, 0.8)
    elif glyph == "mast":
        draw_rect(c, cx - 2.5 * SCALE, cy - c.h * 0.18, 5 * SCALE, c.h * 0.3, accent, 0.85)
        for r_mult in (0.10, 0.16, 0.22):
            r = c.w * r_mult
            ys2, xs2 = np.mgrid[0 : c.h, 0 : c.w].astype(np.float32)
            d = np.sqrt((xs2 - cx) ** 2 + (ys2 - (cy - c.h * 0.18)) ** 2)
            ring = (np.abs(d - r) < 1.6 * SCALE).astype(np.float32)
            ring *= (ys2 < (cy - c.h * 0.18)).astype(np.float32)
            c.blend(accent, ring * 0.7)
        draw_circle(c, cx, cy - c.h * 0.18, 3.4 * SCALE, accent, 0.95)
    elif glyph == "rack":
        for row in range(4):
            oy = cy - c.h * 0.17 + row * c.h * 0.095
            draw_rect(c, c.w * 0.28, oy, c.w * 0.44, c.h * 0.07, deep * 0.45, 0.92)
            draw_rect(c, c.w * 0.30, oy + 2 * SCALE, 4 * SCALE, 4 * SCALE, accent, 0.9)
            draw_rect(c, c.w * 0.36, oy + 2 * SCALE, c.w * 0.3, 2 * SCALE, PAL["steel_dark"], 0.8)

    # Bottom gradient band for title backing (baked, per design doc 6.8).
    ys3 = (np.arange(c.h, dtype=np.float32) / c.h)[:, None]
    fade = np.clip((ys3 - 0.55) / 0.45, 0, 1) ** 1.2 * 0.82
    c.blend(np.zeros(3, dtype=np.float32), np.broadcast_to(fade, (c.h, c.w)))
    inner_shadow(c, 10 * SCALE, 0.3)


def paint_gauge_ring(c, rng, quartile):
    """Sprite-ring gauge state: teal arc covering quartile/4 of the circle."""
    c.fill(np.zeros(3, dtype=np.float32), alpha=0.0)
    cx, cy = c.w / 2.0, c.h / 2.0
    outer = c.w * 0.46
    inner = c.w * 0.34
    ys, xs = np.mgrid[0 : c.h, 0 : c.w].astype(np.float32)
    d = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    ring = ((d < outer) & (d > inner)).astype(np.float32)
    # Channel.
    c.blend(PAL["well"], ring * 0.95)
    c.a = np.maximum(c.a, ring * 255.0)
    # Arc: angle from 12 o'clock, clockwise.
    ang = (np.arctan2(xs - cx, -(ys - cy))) % (2 * np.pi)
    frac = quartile / 4.0
    arc = ring * (ang <= frac * 2 * np.pi + 1e-6).astype(np.float32)
    if quartile > 0:
        c.blend(PAL["teal_deep"], arc)
        hot = ring * (np.abs(d - (outer + inner) / 2) < (outer - inner) * 0.25)
        c.blend(PAL["teal"], hot * (ang <= frac * 2 * np.pi + 1e-6) * 0.8)
    # Rim.
    rim = ((np.abs(d - outer) < 1.2 * SCALE) | (np.abs(d - inner) < 1.2 * SCALE)).astype(np.float32)
    c.blend(PAL["steel_darker"], rim * 0.8)
    c.a = np.maximum(c.a, rim * 255.0)


def paint_button(c, rng, kind="normal"):
    tones = {
        "normal": PAL["plate"],
        "quiet": PAL["panel"],
        "primary": PAL["green_deep"],
        "danger": PAL["red_deep"],
        "disabled": PAL["plate_lo"],
    }
    glows = {
        "hover": (PAL["brass"], 0.34),
        "focus": (PAL["gold"], 0.42),
        "active": (PAL["gold"], 0.5),
        "primary-hover": (PAL["green"], 0.4),
        "danger-hover": (PAL["red"], 0.4),
    }
    base_kind = kind.split("-")[0] if kind not in tones else kind
    tone = tones.get(base_kind, PAL["plate"])
    if kind in ("hover", "focus", "active"):
        tone = PAL["plate"]

    base_plate(c, rng, tone, mottle=0.45, brush=0.4)
    if kind != "disabled":
        grime_layer(c, rng, strength=0.26, edge_bias=1.4)
        pitting(c, rng, 0.03, 0.3)
        scratches(c, rng, 3, PAL["muted"], 0.10)
    bevel(c, 2, 3, raised=(kind != "active"), strength=0.55 if kind != "disabled" else 0.25)
    frame_color = {
        "primary": PAL["green_edge"],
        "primary-hover": PAL["green_edge"],
        "danger": PAL["red_edge"],
        "danger-hover": PAL["red_edge"],
        "disabled": PAL["steel_darker"],
    }.get(kind, PAL["steel_dark"])
    frame(c, rng, 3, frame_color, wear=0.5)
    if kind in glows:
        color, strength = glows[kind]
        edge_glow(c, color, c.w * 0.16, strength)
    if kind == "active":
        inner_shadow(c, c.w * 0.2, 0.35)
    rivet(c, 7, 7, 2.6)
    rivet(c, c.w - 8, 7, 2.6)
    rivet(c, 7, c.h - 8, 2.6)
    rivet(c, c.w - 8, c.h - 8, 2.6)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.5 + gray * 0.5


def paint_input(c, rng, kind="normal"):
    base_plate(c, rng, PAL["well"], mottle=0.45, brush=0.2)
    grime_layer(c, rng, strength=0.2, edge_bias=1.2)
    pitting(c, rng, 0.025, 0.3)
    bevel(c, 1, 3, raised=False, strength=0.6)
    frame(c, rng, 2, PAL["steel_darker"], wear=0.45)
    rims = {"hover": (PAL["brass"], 0.3), "focus": (PAL["gold"], 0.42)}
    if kind in rims:
        color, strength = rims[kind]
        edge_glow(c, color, 5 * SCALE, strength)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.55 + gray * 0.45


def hollow_center(c: Canvas, corner, feather=None):
    """Make the ninepatch stretch region transparent so tiled plate texture
    shows through; feather the transition so the frame fades into it."""
    if feather is None:
        feather = corner // 3
    ed = edge_distance(c.w, c.h)
    keep = np.clip((corner - ed) / max(feather, 1), 0, 1)
    c.a *= keep


def paint_panel(c, rng, heavy=False):
    base_plate(c, rng, PAL["panel"], mottle=0.5, brush=0.3)
    grime_layer(c, rng, strength=0.3, edge_bias=1.3)
    pitting(c, rng, 0.03, 0.3)
    scratches(c, rng, 4, PAL["muted"], 0.08)
    if heavy:
        bevel(c, 1, 4, raised=True, strength=0.45)
        frame(c, rng, 5, PAL["steel_dark"], wear=0.55)
        inner_shadow(c, 9 * SCALE, 0.28)
        r = 3.2 * SCALE
        off = 8 * SCALE
        rivet(c, off, off, r)
        rivet(c, c.w - off, off, r)
        rivet(c, off, c.h - off, r)
        rivet(c, c.w - off, c.h - off, r)
    else:
        bevel(c, 1, 3, raised=True, strength=0.3)
        frame(c, rng, 3, PAL["steel_darker"], wear=0.45)


def paint_dropdown_panel(c, rng):
    base_plate(c, rng, PAL["well"], mottle=0.35, brush=0.2)
    grime_layer(c, rng, strength=0.18, edge_bias=1.2)
    bevel(c, 1, 2, raised=True, strength=0.4)
    frame(c, rng, 2, PAL["steel_dark"], wear=0.4)


def paint_grime_frame(c, rng):
    """Transparent-center rust vignette that creeps in from screen edges."""
    c.fill(np.zeros(3, dtype=np.float32), alpha=0.0)
    h, w = c.h, c.w
    ed = edge_distance(w, h)
    falloff = np.clip(1.0 - ed / (0.42 * min(w, h)), 0, 1)
    n = fbm(rng, w, h, max(4, w // 8), 4, tile_x=True, tile_y=True)
    soot = falloff ** 2.2 * (0.55 + 0.45 * n) * 0.55
    rust = np.clip((n - 0.45) * 2.2, 0, 1) * falloff ** 3.0 * 0.5
    c.blend(np.zeros(3, dtype=np.float32), soot)
    c.a = np.maximum(c.a, soot * 255.0)
    c.blend(PAL["rust_lo"], rust)
    c.a = np.maximum(c.a, rust * 255.0)
    streak = np.clip((value_noise(rng, w, h, max(2, w // 14), tile_x=True) - 0.62) * 3.0, 0, 1)
    ys = (np.arange(h, dtype=np.float32) / h)[:, None]
    drip = streak * np.clip(1.0 - ys * 2.4, 0, 1) * 0.35
    c.blend(PAL["rust"], drip)
    c.a = np.maximum(c.a, drip * 255.0)


def paint_plate_tile(c, rng):
    """Seamless panel-interior texture; low contrast so text stays readable."""
    base_plate(c, rng, PAL["panel"], mottle=0.4, brush=0.3, tile_x=True, tile_y=True)
    grime_layer(c, rng, strength=0.14, edge_bias=0.0, tile_x=True, tile_y=True)
    pitting(c, rng, 0.02, 0.2)
    scratches(c, rng, 6, PAL["muted"], 0.06)


def paint_backdrop(c, rng):
    base_plate(c, rng, PAL["bg"], mottle=0.5, brush=0.25, tile_x=True, tile_y=True)
    grime_layer(c, rng, strength=0.16, edge_bias=0.0, tile_x=True, tile_y=True)
    pitting(c, rng, 0.02, 0.22)
    # Faint plate seams on a coarse grid; keep periodic so the tile repeats.
    seam = PAL["plate_lo"] * 0.7
    for frac in (0.0, 0.5):
        x = int(c.w * frac)
        y = int(c.h * frac)
        draw_rect(c, x, 0, 1, c.h, seam, 0.5)
        draw_rect(c, 0, y, c.w, 1, seam, 0.5)
        draw_rect(c, (x + 1) % c.w, 0, 1, c.h, PAL["plate_hi"], 0.12)
        draw_rect(c, 0, (y + 1) % c.h, c.w, 1, PAL["plate_hi"], 0.12)


def paint_rail(c, rng):
    base_plate(c, rng, PAL["steel_darker"], mottle=0.5, brush=0.55, tile_x=True)
    grime_layer(c, rng, strength=0.35, edge_bias=0.6, tile_x=True)
    draw_rect(c, 0, 0, c.w, 1, PAL["brass"], 0.5)
    draw_rect(c, 0, c.h - 1, c.w, 1, np.zeros(3, dtype=np.float32), 0.55)


def paint_checkbox(c, rng, on=False, kind="normal"):
    base_plate(c, rng, PAL["well"], mottle=0.4, brush=0.15)
    grime_layer(c, rng, strength=0.2, edge_bias=1.2)
    bevel(c, 1, 2, raised=False, strength=0.55)
    frame(c, rng, 2, PAL["steel_darker"] if kind != "focus" else PAL["gold"], wear=0.4)
    if kind == "hover":
        edge_glow(c, PAL["brass"], 4 * SCALE, 0.3)
    if on:
        pad = int(c.w * 0.22)
        slot_color = PAL["green_deep"] if kind != "disabled" else PAL["plate_lo"]
        draw_rect(c, pad, pad, c.w - 2 * pad, c.h - 2 * pad, slot_color, 0.9)
        lit = PAL["green"] if kind != "disabled" else PAL["steel"]
        # Etched check mark.
        cx, cy = c.w / 2, c.h / 2
        for t in np.linspace(0, 1, int(c.w * 0.4)):
            px = cx - c.w * 0.18 + t * c.w * 0.14
            py = cy + t * c.h * 0.16
            draw_circle(c, px, py, 1.6 * SCALE / 2, lit, 0.9, soft=1.2)
        for t in np.linspace(0, 1, int(c.w * 0.6)):
            px = cx - c.w * 0.04 + t * c.w * 0.26
            py = cy + c.h * 0.16 - t * c.h * 0.34
            draw_circle(c, px, py, 1.6 * SCALE / 2, lit, 0.9, soft=1.2)
        if kind != "disabled":
            edge_glow(c, PAL["green"], 3 * SCALE, 0.18)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.55 + gray * 0.45


def paint_range_track(c, rng, kind="normal"):
    base_plate(c, rng, PAL["well"], mottle=0.4, brush=0.3)
    grime_layer(c, rng, strength=0.22, edge_bias=1.0)
    bevel(c, 0, 2, raised=False, strength=0.6)
    frame(c, rng, 1, PAL["steel_darker"], wear=0.4)
    rims = {"hover": (PAL["brass"], 0.26), "focus": (PAL["gold"], 0.36)}
    if kind in rims:
        color, strength = rims[kind]
        edge_glow(c, color, 3 * SCALE, strength)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.55 + gray * 0.45


def paint_range_thumb(c, rng, kind="normal"):
    base_plate(c, rng, PAL["plate_hi"], mottle=0.5, brush=0.3)
    grime_layer(c, rng, strength=0.2, edge_bias=0.9)
    bevel(c, 1, 2, raised=True, strength=0.6)
    frame(c, rng, 2, PAL["steel_dark"], wear=0.45)
    # Vertical grip grooves.
    for fx in (0.36, 0.62):
        x = int(c.w * fx)
        draw_rect(c, x, int(c.h * 0.25), 2, int(c.h * 0.5), PAL["plate_lo"], 0.8)
        draw_rect(c, x + 2, int(c.h * 0.25), 1, int(c.h * 0.5), PAL["muted"], 0.3)
    if kind == "hover":
        edge_glow(c, PAL["brass"], 4 * SCALE, 0.32)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.55 + gray * 0.45


def paint_progress_fill(c, rng, disabled=False):
    core = PAL["teal_deep"] if not disabled else PAL["plate_lo"]
    hot = (PAL["teal"] * 0.6 + PAL["teal_deep"] * 0.4) if not disabled else PAL["steel"]
    base_plate(c, rng, core, mottle=0.35, brush=0.4)
    ys = np.abs(np.arange(c.h, dtype=np.float32) - c.h / 2) / (c.h / 2)
    band = np.clip(1.0 - ys * 1.7, 0, 1)[:, None] ** 1.6
    c.blend(hot, np.broadcast_to(band * (0.5 if not disabled else 0.3), (c.h, c.w)))
    grime_layer(c, rng, strength=0.12, edge_bias=0.8)
    bevel(c, 0, 2, raised=True, strength=0.35)


def paint_scroll_thumb(c, rng, kind="normal"):
    base_plate(c, rng, PAL["steel_darker"], mottle=0.5, brush=0.25)
    grime_layer(c, rng, strength=0.25, edge_bias=1.0)
    bevel(c, 0, 2, raised=True, strength=0.5)
    frame(c, rng, 1, PAL["steel_darker"], wear=0.4)
    for fy in (0.4, 0.5, 0.6):
        y = int(c.h * fy)
        draw_rect(c, int(c.w * 0.25), y, int(c.w * 0.5), 1, PAL["plate_lo"], 0.7)
    if kind == "hover":
        edge_glow(c, PAL["brass"], 3 * SCALE, 0.3)
    if kind == "disabled":
        gray = c.rgb.mean(axis=2, keepdims=True)
        c.rgb = c.rgb * 0.55 + gray * 0.45


def paint_scroll_track(c, rng):
    base_plate(c, rng, PAL["bg"], mottle=0.35, brush=0.2, tile_y=True)
    grime_layer(c, rng, strength=0.15, edge_bias=0.8, tile_y=True)
    bevel(c, 0, 1, raised=False, strength=0.5)


def paint_arrow(c, rng, hover=False):
    base_plate(c, rng, PAL["plate"], mottle=0.5, brush=0.3)
    grime_layer(c, rng, strength=0.22, edge_bias=1.0)
    bevel(c, 1, 2, raised=True, strength=0.5)
    frame(c, rng, 2, PAL["steel_dark"], wear=0.45)
    color = PAL["gold"] if hover else PAL["brass"]
    cx, cy = c.w / 2, c.h / 2 - 1
    half = c.w * 0.2
    for t in np.linspace(0, 1, int(c.w * 0.5)):
        px1 = cx - half + t * half
        px2 = cx + half - t * half
        py = cy - half * 0.5 + t * half
        draw_circle(c, px1, py, SCALE * 0.9, color, 0.95, soft=1.0)
        draw_circle(c, px2, py, SCALE * 0.9, color, 0.95, soft=1.0)
    if hover:
        edge_glow(c, PAL["brass"], 3 * SCALE, 0.25)


# ---------------------------------------------------------------------------
# Sheet packing + RCSS emission
# ---------------------------------------------------------------------------

class Sheet:
    def __init__(self, width_px):
        self.width = width_px
        self.entries = []  # (name, x, y, w, h, canvas)
        self.inner_entries = []  # (name, x, y, w, h) ninepatch stretch rects
        self.cursor_x = GUTTER
        self.cursor_y = GUTTER
        self.row_h = 0

    def add(self, name, canvas):
        w, h = canvas.w, canvas.h
        if self.cursor_x + w + GUTTER > self.width:
            self.cursor_x = GUTTER
            self.cursor_y += self.row_h + GUTTER
            self.row_h = 0
        self.entries.append((name, self.cursor_x, self.cursor_y, w, h, canvas))
        self.cursor_x += w + GUTTER
        self.row_h = max(self.row_h, h)

    def height(self):
        return self.cursor_y + self.row_h + GUTTER

    def compose(self):
        h = self.height()
        out = np.zeros((h, self.width, 4), dtype=np.uint8)
        for _, x, y, w, hh, canvas in self.entries:
            tile = np.asarray(canvas.to_image(), dtype=np.uint8)
            out[y : y + hh, x : x + w] = tile
            # Edge-replicate into the gutter (corners included) so linear
            # filtering at sprite borders never blends transparent black.
            g = GUTTER // 2
            y0 = max(0, y - g)
            x0 = max(0, x - g)
            out[y0:y, x : x + w] = tile[0:1]
            out[y + hh : y + hh + g, x : x + w] = tile[-1:]
            out[y : y + hh, x0:x] = tile[:, 0:1]
            out[y : y + hh, x + w : x + w + g] = tile[:, -1:]
            out[y0:y, x0:x] = tile[0, 0]
            out[y0:y, x + w : x + w + g] = tile[0, -1]
            out[y + hh : y + hh + g, x0:x] = tile[-1, 0]
            out[y + hh : y + hh + g, x + w : x + w + g] = tile[-1, -1]
        return Image.fromarray(out)

    def add_ninepatch(self, name, canvas, corner):
        """Register a sprite plus its ninepatch inner rect (sheet coords)."""
        self.add(name, canvas)
        _, x, y, w, h, _ = self.entries[-1]
        cs = int(corner)
        self.inner_entries.append(
            (f"{name}-inner", x + cs, y + cs, w - 2 * cs, h - 2 * cs))

    def rcss_block(self, src_rel):
        lines = [
            MARK_BEGIN,
            "@spritesheet worr-metal",
            "{",
            f"\tsrc: {src_rel};",
            f"\tresolution: {SCALE}x;",
        ]
        for name, x, y, w, h, _ in self.entries:
            lines.append(f"\t{name}: {x}px {y}px {w}px {h}px;")
        for name, x, y, w, h in self.inner_entries:
            lines.append(f"\t{name}: {x}px {y}px {w}px {h}px;")
        lines.append("}")
        lines.append(MARK_END)
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Icon PNGs: render the flat-shape icon SVGs (single source of truth) into
# supersampled, antialiased PNGs sized at 2x their on-screen display size and
# never below the renderer's 64px standalone-texture threshold.
# The runtime SVG rasterizer stays as the renderer-neutral fallback; shipped
# documents reference the PNGs.
# ---------------------------------------------------------------------------

ICONS_ROOT = REPO_ROOT / "assets/ui/rml/common/icons"
ICON_SUPERSAMPLE = 8

# Output pixel size = viewBox * factor. The 64px floor prevents these UI
# textures from entering the legacy scrap atlas, which RmlUi cannot address
# as standalone texture handles.
ICON_SCALE_OVERRIDES = {
    "gear": 64 / 24,
    "power": 64 / 24,
    "worr-mark": 126 / 72,
}
ICON_DEFAULT_SCALE = 2.0  # 32px viewBox -> 64px standalone PNG


def _icon_color(value):
    value = (value or "").strip()
    if not value or value == "none":
        return None
    if value.startswith("#") and len(value) == 7:
        return (int(value[1:3], 16), int(value[3:5], 16), int(value[5:7], 16), 255)
    if value.startswith("#") and len(value) == 4:
        return (int(value[1] * 2, 16), int(value[2] * 2, 16), int(value[3] * 2, 16), 255)
    raise SystemExit(f"unsupported SVG color: {value}")


def _icon_points(value):
    nums = [float(v) for v in re.split(r"[\s,]+", value.strip()) if v]
    return list(zip(nums[0::2], nums[1::2]))


def render_icon_png(svg_path, factor):
    root = ET.parse(svg_path).getroot()
    view = root.get("viewBox")
    if view:
        _, _, vw, vh = (float(v) for v in view.split())
    else:
        vw, vh = float(root.get("width")), float(root.get("height"))

    s = factor * ICON_SUPERSAMPLE
    img = Image.new("RGBA", (int(round(vw * s)), int(round(vh * s))), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    def stroke_of(el):
        color = _icon_color(el.get("stroke"))
        width = float(el.get("stroke-width", "1")) * s
        return color, width

    for el in root:
        tag = el.tag.split("}")[-1]
        fill = _icon_color(el.get("fill", "#000000" if tag not in ("line", "polyline") else "none"))
        stroke, sw = stroke_of(el)

        if tag == "rect":
            x = float(el.get("x", "0")) * s
            y = float(el.get("y", "0")) * s
            w = float(el.get("width")) * s
            h = float(el.get("height")) * s
            if fill:
                draw.rectangle([x, y, x + w, y + h], fill=fill)
            if stroke:
                draw.rectangle([x - sw / 2, y - sw / 2, x + w + sw / 2, y + h + sw / 2],
                               outline=stroke, width=int(round(sw)))
        elif tag == "circle":
            cx = float(el.get("cx")) * s
            cy = float(el.get("cy")) * s
            r = float(el.get("r")) * s
            if fill:
                draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
            if stroke:
                draw.ellipse([cx - r - sw / 2, cy - r - sw / 2, cx + r + sw / 2, cy + r + sw / 2],
                             outline=stroke, width=int(round(sw)))
        elif tag == "line":
            pts = [(float(el.get("x1")) * s, float(el.get("y1")) * s),
                   (float(el.get("x2")) * s, float(el.get("y2")) * s)]
            if stroke:
                draw.line(pts, fill=stroke, width=int(round(sw)))
        elif tag == "polyline":
            pts = [(x * s, y * s) for x, y in _icon_points(el.get("points"))]
            if stroke:
                draw.line(pts, fill=stroke, width=int(round(sw)), joint="curve")
        elif tag == "polygon":
            pts = [(x * s, y * s) for x, y in _icon_points(el.get("points"))]
            if fill:
                draw.polygon(pts, fill=fill)
            if stroke:
                draw.line(pts + [pts[0]], fill=stroke, width=int(round(sw)), joint="curve")
        else:
            raise SystemExit(f"{svg_path.name}: unsupported SVG element <{tag}>")

    out_size = (int(round(vw * factor)), int(round(vh * factor)))
    img = img.resize(out_size, Image.LANCZOS)
    out_path = svg_path.with_suffix(".png")
    img.save(out_path)
    return out_path, out_size


def render_all_icon_pngs():
    count = 0
    for svg_path in sorted(ICONS_ROOT.rglob("*.svg")):
        if svg_path.name == "README.md":
            continue
        factor = ICON_SCALE_OVERRIDES.get(svg_path.stem, ICON_DEFAULT_SCALE)
        render_icon_png(svg_path, factor)
        count += 1
    print(f"Rendered {count} icon PNGs under {ICONS_ROOT}")


def build(seed, out_dir):
    rng_root = np.random.default_rng(seed)

    def rng():
        return np.random.default_rng(rng_root.integers(0, 2**63))

    def canvas(w_logical, h_logical):
        return Canvas(w_logical * SCALE, h_logical * SCALE)

    sheet = Sheet(1024)

    # Buttons: one shared base seed per family so states don't jump.
    button_kinds = [
        ("btn", "normal"), ("btn-hover", "hover"), ("btn-focus", "focus"),
        ("btn-active", "active"), ("btn-disabled", "disabled"),
        ("btn-quiet", "quiet"),
        ("btn-primary", "primary"), ("btn-primary-hover", "primary-hover"),
        ("btn-danger", "danger"), ("btn-danger-hover", "danger-hover"),
    ]
    btn_seed = rng_root.integers(0, 2**63)
    for name, kind in button_kinds:
        c = canvas(48, 36)
        paint_button(c, np.random.default_rng(btn_seed), kind)
        sheet.add_ninepatch(name, c, 12 * SCALE)

    input_seed = rng_root.integers(0, 2**63)
    for name, kind in [("field", "normal"), ("field-hover", "hover"),
                       ("field-focus", "focus"), ("field-disabled", "disabled")]:
        c = canvas(48, 36)
        paint_input(c, np.random.default_rng(input_seed), kind)
        sheet.add_ninepatch(name, c, 10 * SCALE)

    cta_seed = rng_root.integers(0, 2**63)
    for name, kind in [("cta", "normal"), ("cta-hover", "hover"),
                       ("cta-active", "active"), ("cta-disabled", "disabled")]:
        c = canvas(64, 44)
        paint_cta(c, np.random.default_rng(cta_seed), kind)
        sheet.add_ninepatch(name, c, 12 * SCALE)

    bp_seed = rng_root.integers(0, 2**63)
    for name, kind in [("backplate", "normal"), ("backplate-hover", "hover"),
                       ("backplate-active", "active")]:
        c = canvas(40, 40)
        paint_backplate(c, np.random.default_rng(bp_seed), kind)
        sheet.add(name, c)

    card_seed = rng_root.integers(0, 2**63)
    for name, kind in [("card", "rest"), ("card-hover", "hover"),
                       ("card-selected", "selected")]:
        c = canvas(64, 64)
        paint_card_frame(c, np.random.default_rng(card_seed), kind)
        sheet.add_ninepatch(name, c, 16 * SCALE)

    for q in range(5):
        c = canvas(48, 48)
        paint_gauge_ring(c, rng(), q)
        sheet.add(f"gauge-q{q}", c)

    for name, hover in [("selectarrow", False), ("selectarrow-hover", True)]:
        c = canvas(15, 14)
        paint_arrow(c, rng(), hover)
        sheet.add(name, c)

    cb_seed = rng_root.integers(0, 2**63)
    for name, on, kind in [
        ("check-off", False, "normal"), ("check-off-hover", False, "hover"),
        ("check-off-focus", False, "focus"), ("check-on", True, "normal"),
        ("check-on-hover", True, "hover"), ("check-on-focus", True, "focus"),
        ("check-disabled", False, "disabled"), ("check-on-disabled", True, "disabled"),
    ]:
        c = canvas(32, 28)
        paint_checkbox(c, np.random.default_rng(cb_seed), on, kind)
        sheet.add(name, c)

    track_seed = rng_root.integers(0, 2**63)
    for name, kind in [("track", "normal"), ("track-hover", "hover"),
                       ("track-focus", "focus"), ("track-disabled", "disabled")]:
        c = canvas(48, 12)
        paint_range_track(c, np.random.default_rng(track_seed), kind)
        sheet.add_ninepatch(name, c, 5 * SCALE)

    thumb_seed = rng_root.integers(0, 2**63)
    for name, kind in [("thumb", "normal"), ("thumb-hover", "hover"),
                       ("thumb-disabled", "disabled")]:
        c = canvas(24, 28)
        paint_range_thumb(c, np.random.default_rng(thumb_seed), kind)
        sheet.add(name, c)

    ptrack_seed = rng_root.integers(0, 2**63)
    for name, kind in [("ptrack", "normal"), ("ptrack-hover", "hover"),
                       ("ptrack-disabled", "disabled")]:
        c = canvas(48, 28)
        paint_range_track(c, np.random.default_rng(ptrack_seed), kind)
        sheet.add_ninepatch(name, c, 8 * SCALE)

    for name, dis in [("pfill", False), ("pfill-disabled", True)]:
        c = canvas(40, 20)
        paint_progress_fill(c, rng(), dis)
        sheet.add_ninepatch(name, c, 6 * SCALE)

    st_seed = rng_root.integers(0, 2**63)
    for name, kind in [("sthumb", "normal"), ("sthumb-hover", "hover"),
                       ("sthumb-disabled", "disabled")]:
        c = canvas(12, 32)
        paint_scroll_thumb(c, np.random.default_rng(st_seed), kind)
        sheet.add_ninepatch(name, c, 5 * SCALE)

    c = canvas(12, 48)
    paint_scroll_track(c, rng())
    sheet.add_ninepatch("strack", c, 5 * SCALE)

    c = canvas(96, 6)
    paint_rail(c, rng())
    sheet.add_ninepatch("rail", c, 2 * SCALE)

    # Ninepatch frames: transparent stretch centers; tiled plate/backdrop
    # textures supply the interior material.
    c = canvas(72, 72)
    paint_panel(c, rng(), heavy=False)
    hollow_center(c, 14 * SCALE)
    sheet.add_ninepatch("panel", c, 12 * SCALE)

    c = canvas(96, 96)
    paint_panel(c, rng(), heavy=True)
    hollow_center(c, 24 * SCALE)
    sheet.add_ninepatch("popup", c, 20 * SCALE)

    c = canvas(48, 48)
    paint_dropdown_panel(c, rng())
    sheet.add_ninepatch("drop", c, 8 * SCALE)

    c = canvas(128, 128)
    paint_grime_frame(c, rng())
    sheet.add_ninepatch("grime", c, 40 * SCALE)

    out_dir.mkdir(parents=True, exist_ok=True)
    sheet_img = sheet.compose()
    sheet_path = out_dir / "ui-metal.png"
    sheet_img.save(sheet_path)

    b = Canvas(256 * SCALE, 256 * SCALE)
    paint_backdrop(b, rng())
    backdrop_path = out_dir / "backdrop.png"
    b.to_image().convert("RGB").save(backdrop_path)

    p = Canvas(192 * SCALE, 192 * SCALE)
    paint_plate_tile(p, rng())
    plate_path = out_dir / "plate.png"
    p.to_image().convert("RGB").save(plate_path)

    for theme in CARD_THEMES:
        a = Canvas(200 * SCALE, 280 * SCALE)
        paint_card_art(a, rng(), theme)
        a.to_image().convert("RGB").save(out_dir / f"cardart-{theme}.png")
    print(f"Wrote {len(CARD_THEMES)} card art tiles")

    render_all_icon_pngs()

    if out_dir.resolve() != DEFAULT_OUT_DIR.resolve():
        print(f"NOTE: --out-dir {out_dir} differs from the theme's asset root;"
              " leaving base.rcss untouched.")
    else:
        block = sheet.rcss_block("../skins/metal/ui-metal.png")
        text = BASE_RCSS.read_text(encoding="utf-8")
        pattern = re.compile(
            re.escape(MARK_BEGIN) + r".*?" + re.escape(MARK_END), re.DOTALL)
        if pattern.search(text):
            # Lambda replacement: the block must not be treated as a regex
            # template (backslashes would corrupt the file).
            text = pattern.sub(lambda _: block, text)
        else:
            insert_at = text.find("body\n")
            if insert_at < 0:
                insert_at = len(text)
            text = text[:insert_at] + block + "\n\n" + text[insert_at:]
        BASE_RCSS.write_text(text, encoding="utf-8", newline="\n")

    print(f"Wrote {sheet_path} ({sheet_img.width}x{sheet_img.height})")
    print(f"Wrote {backdrop_path}")
    print(f"Wrote {plate_path}")
    print(f"Updated @spritesheet block in {BASE_RCSS}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20260711)
    parser.add_argument("--out-dir", type=pathlib.Path, default=DEFAULT_OUT_DIR)
    args = parser.parse_args()
    return build(args.seed, args.out_dir)


if __name__ == "__main__":
    sys.exit(main())
