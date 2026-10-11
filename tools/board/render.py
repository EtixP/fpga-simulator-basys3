"""Renders the board's images inside Blender; tools/board_render.py runs it.

Outputs, written to the directory given after `--`:
  board.png          the board with every live part off (RGBA: its shadow is translucent)
  sprites/*.png      each live part's other state (switch on, LED lit, button pressed,
                     segment lit), cut from full renders of that state with feathered
                     edges, so any combination composites cleanly over board.png
  layout.json        where each part and sprite goes, in board units (0.1 mm)

Every variant is a full render of the same scene, camera and seed, with only the
parts concerned changed. Within one variant, neighbouring parts stay off, so each
sprite shows its part alone.

  --verify  also renders one random state outright and compares it with the
            composite of the base image and the sprites (printed as PSNR).
"""
import json
import math
import os
import random
import struct
import sys
import time
import zlib

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measured as M  # noqa: E402
import scene as S  # noqa: E402

UNITS_PER_MM = 10
FEATHER_MM = 0.6


def log(*args):
    print('board_render:', *args, flush=True)


# ----------------------------------------------------------------------------- images

def render_to(path):
    bpy.context.scene.render.filepath = path
    start = time.time()
    bpy.ops.render.render(write_still=True)
    log(f'rendered {os.path.basename(path)} in {time.time() - start:.0f} s')
    return read_png(path)


def read_png(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    # Blender's rows run bottom-up; byte images expose their stored (sRGB) values
    return np.flipud(np.round(buf.reshape(h, w, 4) * 255).astype(np.uint8))


def write_png(path, rgba):
    """A deterministic RGBA PNG: 'Sub' row filters, zlib level 9."""
    h, w, _ = rgba.shape
    data = rgba.astype(np.int16)
    sub = data.copy()
    sub[:, 1:] = (data[:, 1:] - data[:, :-1]) & 0xFF
    rows = np.concatenate([np.ones((h, 1), np.uint8), sub.reshape(h, w * 4).astype(np.uint8)], axis=1)

    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload
                + struct.pack('>I', zlib.crc32(kind + payload) & 0xFFFFFFFF))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(rows.tobytes(), 9)) + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def gaussian(img, sigma):
    """Separable Gaussian blur of an (h, w) or (h, w, c) float array."""
    r = max(1, int(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = img.astype(np.float32)
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim
        pad[axis] = (r, r)
        p = np.pad(out, pad, mode='edge')
        acc = np.zeros_like(out)
        for i, kv in enumerate(k):
            sl = [slice(None)] * out.ndim
            sl[axis] = slice(i, i + out.shape[axis])
            acc += kv * p[tuple(sl)]
        out = acc
    return out


# ----------------------------------------------------------------------------- geometry

def px(x, y):
    return S.world_to_pixel(x, y)


def pixel_rect(x0, y0, x1, y1):
    """mm rectangle -> integer pixel box (left, top, right, bottom)."""
    a, b = px(x0, y1)
    c, d = px(x1, y0)
    return int(math.floor(a)), int(math.floor(b)), int(math.ceil(c)), int(math.ceil(d))


def feather(w, h, f):
    yy = np.minimum(np.arange(h), np.arange(h)[::-1])[:, None]
    xx = np.minimum(np.arange(w), np.arange(w)[::-1])[None, :]
    return np.clip(np.minimum(xx, yy) / max(f, 1), 0, 1).astype(np.float32)


def polygon_coverage(poly_px, box, grow):
    """Coverage of a convex polygon (pixel coords) over box, grown by `grow` px, antialiased."""
    l, t, r, b = box
    ys, xs = np.mgrid[t:b, l:r].astype(np.float32) + 0.5
    d = np.full(xs.shape, -1e9, np.float32)
    n = len(poly_px)
    # signed distance to a convex polygon: max over edge half-planes (outside > 0)
    area = sum(poly_px[i][0] * poly_px[(i + 1) % n][1] - poly_px[(i + 1) % n][0] * poly_px[i][1] for i in range(n))
    sign = 1 if area > 0 else -1
    for i in range(n):
        (x0, y0), (x1, y1) = poly_px[i], poly_px[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        L = math.hypot(ex, ey)
        nx, ny = sign * ey / L, -sign * ex / L
        d = np.maximum(d, (xs - x0) * nx + (ys - y0) * ny)
    return np.clip(grow + 0.5 - d, 0, 1)


def circle_coverage(cx, cy, r, box, grow):
    l, t, rr, b = box
    ys, xs = np.mgrid[t:b, l:rr].astype(np.float32) + 0.5
    d = np.hypot(xs - cx, ys - cy) - r
    return np.clip(grow + 0.5 - d, 0, 1)


# ----------------------------------------------------------------------------- variants

def all_off():
    for i in range(16):
        S.set_switch(i, False)
        S.set_led(i, False)
    for i in range(5):
        S.set_button(i, False)
    for (digit, seg) in M.SEGMENTS:
        S.set_segment(digit, seg, False)
    for digit in range(4):
        S.set_segment(digit, 'dp', False)
    S.set_led_named('ld19', False)


def apply(state):
    all_off()
    for i in state.get('switches', []):
        S.set_switch(i, True)
    for i in state.get('leds', []):
        S.set_led(i, True)
    for i in state.get('buttons', []):
        S.set_button(i, True)
    for digit, seg in state.get('segments', []):
        S.set_segment(digit, seg, True)
    if state.get('done'):
        S.set_led_named('ld19', True)


SEG_ORDER = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'dp']


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out_dir = argv[0]
    samples = int(argv[1]) if len(argv) > 1 else 256
    verify = '--verify' in argv
    # --reuse: take the variants already rendered into <out>/renders (development)
    reuse = '--reuse' in argv
    work = os.path.join(out_dir, 'renders')
    os.makedirs(work, exist_ok=True)
    scene = S.build(1.0, samples)
    W, H = scene.render.resolution_x, scene.render.resolution_y
    log(f'{W} x {H} px at {S.PX_PER_MM} px/mm, {samples} samples')

    variants = {
        'base': {},
        'sw_even': {'switches': list(range(0, 16, 2))},
        'sw_odd': {'switches': list(range(1, 16, 2))},
        'led_even': {'leds': list(range(0, 16, 2)), 'done': True},
        'led_odd': {'leds': list(range(1, 16, 2))},
        'buttons': {'buttons': list(range(5))},
        'segments': {'segments': [(d, s) for d in range(4) for s in SEG_ORDER]},
    }
    images = {}
    for name, state in variants.items():
        path = os.path.join(work, name + '.png')
        if reuse and os.path.exists(path):
            images[name] = read_png(path)
            continue
        apply(state)
        images[name] = render_to(path)
    base = images['base']
    sprites, layout = build_sprites(images, W, H, bloom=True)
    write_png(os.path.join(out_dir, 'board.png'), base)
    os.makedirs(os.path.join(out_dir, 'sprites'), exist_ok=True)
    for key, rgba in sprites.items():
        write_png(os.path.join(out_dir, 'sprites', sprite_name(key)), rgba)
    with open(os.path.join(out_dir, 'layout.json'), 'w') as fh:
        json.dump(layout, fh, indent=1)
    log(f'wrote board.png, {len(sprites)} sprites and layout.json')
    if verify:
        verify_composite(images, W, H, work)


def sprite_name(key):
    """switch-3.png, led-0.png, button-4.png, done.png, segment-2-dp.png"""
    return 'done.png' if key[0] == 'done' else '-'.join(str(k) for k in key) + '.png'


def build_sprites(images, W, H, bloom):
    base = images['base']
    sprites = []      # (key, rgba)
    layout = {'width': round(S.CANVAS_W * UNITS_PER_MM), 'height': round(S.CANVAS_H * UNITS_PER_MM)}
    f = FEATHER_MM * S.PX_PER_MM

    def cut(variant, box, alpha=None, glow=None, glow_at=None):
        l, t, r, b = box
        rgba = images[variant][t:b, l:r].astype(np.float32)
        if glow is not None and bloom:
            # light the part adds near itself, spread as a soft bloom; farther
            # differences between the renders are sampling noise, not light
            added = np.clip(rgba[..., :3] - base[t:b, l:r, :3].astype(np.float32), 0, None)
            ys, xs = np.mgrid[t:b, l:r].astype(np.float32) + 0.5
            near = np.hypot(xs - glow_at[0], ys - glow_at[1]) / (1.3 * S.PX_PER_MM)
            added *= np.exp(-near ** 4)[..., None]
            halo = gaussian(added, glow) * 1.6
            rgba[..., :3] = np.clip(rgba[..., :3] + halo, 0, 255)
        a = feather(r - l, b - t, f) if alpha is None else alpha
        rgba[..., 3] = rgba[..., 3] * a
        return np.round(rgba).astype(np.uint8)

    def units(box):
        l, t, r, b = box
        k = UNITS_PER_MM / S.PX_PER_MM
        return [round(l * k, 2), round(t * k, 2), round((r - l) * k, 2), round((b - t) * k, 2)]

    # switches: the hit area is the housing; the sprite adds a margin for the lever's shadow
    layout['switches'] = []
    for i, x in enumerate(M.SWITCH_X):
        hit = pixel_rect(x - 2.85, M.SWITCH_Y - 5.75, x + 2.85, M.SWITCH_Y + 5.75)
        box = pixel_rect(x - 3.25, M.SWITCH_Y - 6.4, x + 3.25, M.SWITCH_Y + 6.6)
        sprites.append((('switch', i), cut('sw_even' if i % 2 == 0 else 'sw_odd', box)))
        layout['switches'].append({'rect': units(hit), 'sprite': units(box)})
    layout['leds'] = []
    for i, x in enumerate(M.LED_X):
        hit = pixel_rect(x - 1.2, M.LED_Y - 1.4, x + 1.2, M.LED_Y + 1.4)
        box = pixel_rect(x - 3.0, M.LED_Y - 3.0, x + 3.0, M.LED_Y + 3.0)
        sprites.append((('led', i), cut('led_even' if i % 2 == 0 else 'led_odd', box,
                                        glow=0.55 * S.PX_PER_MM, glow_at=px(x, M.LED_Y))))
        layout['leds'].append({'rect': units(hit), 'sprite': units(box)})
    dx, dy = M.STATUS_LEDS['LD19']
    box = pixel_rect(dx - 3.0, dy - 2.6, dx + 3.0, dy + 2.6)
    sprites.append((('done', 0), cut('led_even', box, glow=0.55 * S.PX_PER_MM, glow_at=px(dx, dy))))
    layout['done'] = {'sprite': units(box)}
    # the display module, veiled when the design leaves it unbound
    cx, cy, w, h = M.DISPLAY
    layout['display'] = {'rect': units(pixel_rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2))}
    layout['buttons'] = []
    for i, (x, y, along_x) in enumerate(M.BUTTONS):
        hit = pixel_rect(x - 2.75, y - 2.75, x + 2.75, y + 2.75)
        box = pixel_rect(x - 3.0, y - 3.0, x + 3.0, y + 3.0)
        sprites.append((('button', i), cut('buttons', box)))
        layout['buttons'].append({'rect': units(hit), 'sprite': units(box)})

    # segments: the lit render through the segment's own shape, plus a red bloom
    layout['digits'] = []
    grow = 0.12 * S.PX_PER_MM
    for digit in range(4):
        shapes = {}
        for seg in SEG_ORDER:
            if seg == 'dp':
                cx, cy = M.DECIMAL_POINTS[digit]
                shapes[seg] = ('circle', px(cx, cy), M.DOT_RADIUS * S.PX_PER_MM)
            else:
                shapes[seg] = ('poly', [px(x, y) for x, y in M.SEGMENTS[(digit, seg)]], None)
        pts = []
        for kind, geo, r in shapes.values():
            pts += [(geo[0] - r, geo[1] - r), (geo[0] + r, geo[1] + r)] if kind == 'circle' else geo
        margin = 0.9 * S.PX_PER_MM
        cell = (int(min(p[0] for p in pts) - margin), int(min(p[1] for p in pts) - margin),
                int(max(p[0] for p in pts) + margin) + 1, int(max(p[1] for p in pts) + margin) + 1)
        entry = {'rect': units(cell), 'segments': []}
        for seg in SEG_ORDER:
            kind, geo, r = shapes[seg]
            gpts = [(geo[0] - r, geo[1] - r), (geo[0] + r, geo[1] + r)] if kind == 'circle' else geo
            m = 0.6 * S.PX_PER_MM
            box = (int(min(p[0] for p in gpts) - m), int(min(p[1] for p in gpts) - m),
                   int(max(p[0] for p in gpts) + m) + 1, int(max(p[1] for p in gpts) + m) + 1)
            if kind == 'circle':
                shape = circle_coverage(geo[0], geo[1], r, box, grow)
            else:
                shape = polygon_coverage(geo, box, grow)
            spread = gaussian(shape, 0.28 * S.PX_PER_MM)
            l, t, rr, b = box
            lit = images['segments'][t:b, l:rr].astype(np.float32)
            glow_rgb = np.array([255, 36, 16], np.float32)
            if not bloom:
                spread = np.zeros_like(spread)
            alpha = np.maximum(shape, np.clip(spread * 0.7, 0, 1) * (1 - shape))
            rgb = lit[..., :3] * shape[..., None] + glow_rgb * (1 - shape[..., None])
            rgba = np.dstack([np.clip(rgb, 0, 255), alpha * 255])
            sprites.append((('segment', digit, seg), np.round(rgba).astype(np.uint8)))
            sprite = units(box)
            cu = units(cell)
            entry['segments'].append({'rect': [round(sprite[0] - cu[0], 2), round(sprite[1] - cu[1], 2),
                                               sprite[2], sprite[3]]})
        layout['digits'].append(entry)

    # make the switch, LED and button sprites relative to their hit rectangles
    for group in ('switches', 'leds', 'buttons'):
        for entry in layout[group]:
            r, s = entry['rect'], entry['sprite']
            entry['sprite'] = [round(s[0] - r[0], 2), round(s[1] - r[1], 2), s[2], s[3]]

    return dict(sprites), layout


def verify_composite(images, W, H, work):
    """Composites a random state from glow-free sprites and compares it with a full render."""
    sprites, layout = build_sprites(images, W, H, bloom=False)
    rng = random.Random(11)
    state = {'switches': [i for i in range(16) if rng.random() < 0.5],
             'leds': [i for i in range(16) if rng.random() < 0.5],
             'buttons': [i for i in range(5) if rng.random() < 0.5],
             'segments': [(d, s) for d in range(4) for s in SEG_ORDER if rng.random() < 0.5],
             'done': True}
    apply(state)
    truth = render_to(os.path.join(work, 'verify_truth.png')).astype(np.float32)
    comp = images['base'].astype(np.float32)
    k = S.PX_PER_MM / UNITS_PER_MM

    def over(key, rect, parent=(0, 0)):
        spr = sprites[key].astype(np.float32)
        h, w = spr.shape[:2]
        left, top = round((parent[0] + rect[0]) * k), round((parent[1] + rect[1]) * k)
        a = spr[..., 3:4] / 255
        region = comp[top:top + h, left:left + w, :3]
        comp[top:top + h, left:left + w, :3] = spr[..., :3] * a + region * (1 - a)

    for group, key, chosen in (('switches', 'switch', state['switches']), ('leds', 'led', state['leds']),
                               ('buttons', 'button', state['buttons'])):
        for i in chosen:
            over((key, i), layout[group][i]['sprite'], layout[group][i]['rect'])
    over(('done', 0), layout['done']['sprite'])
    for d, seg in state['segments']:
        cell = layout['digits'][d]
        over(('segment', d, seg), cell['segments'][SEG_ORDER.index(seg)]['rect'], cell['rect'])
    diff = comp[..., :3] - truth[..., :3]
    mse = float(np.mean(diff ** 2))
    psnr = 10 * math.log10(255 ** 2 / mse) if mse else float('inf')
    # The sprites' own areas are what compositing can get wrong; elsewhere the two
    # renders differ only by sampling noise.
    regions = []
    for group in ('switches', 'leds', 'buttons'):
        regions += [(e['rect'], e['sprite']) for e in layout[group]]
    regions += [(e['rect'], [0, 0, e['rect'][2], e['rect'][3]]) for e in layout['digits']]
    worst_rms = 0.0
    for parent, rect in regions:
        left, top = round((parent[0] + rect[0]) * k), round((parent[1] + rect[1]) * k)
        sub = diff[top:top + round(rect[3] * k), left:left + round(rect[2] * k)]
        worst_rms = max(worst_rms, float(np.sqrt(np.mean(sub ** 2))))
    write_png(os.path.join(work, 'verify_composite.png'),
              np.dstack([np.clip(comp, 0, 255)[..., :3], truth[..., 3]]).astype(np.uint8))
    log(f'verify: composite vs full render PSNR {psnr:.1f} dB; worst part area RMS {worst_rms:.1f}/255')


main()
