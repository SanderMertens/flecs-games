#!/usr/bin/env python3
"""Generate the seamless terrain dirt tiles for Ore Else.

Run from anywhere; writes into projects/ore_else/etc/assets by default:

    terrain_dirt.svg         albedo, a grey multiplier around 1.0
    terrain_dirt_normal.svg  tangent-space normals: grooves, pebbles, swells
    terrain_dirt_rough.svg   metallic-roughness, g = roughness, b = metallic

    python3 tools/gen_terrain.py [output_dir]

The three tiles are authored on a 1024 canvas and declared 256x256 in
etc/materials/terrain.flecs, so the engine's raster is a 4x supersample of
this geometry. 256 is not negotiable: texture arrays bucket by size and a
render group may reference only one bucket.

One tile covers 6 world units, which is what the TextureTransform scales in
etc/materials/terrain.flecs assume - 43.3333 over the 260-unit ground and
17.3333 over the 104-unit basin.

The albedo tile is a near-white grey that multiplies the material's Rgba, so
the ground keeps its authored colour and the tile only carries variation. Its
mean is 0.786 in linear light, which is why GroundSurface and BasinSurface
carry the scene's ochres divided by that.

Everything is explicit geometry from a seeded PCG32. No SVG filter primitives:
the engine rasterizes with resvg, and feTurbulence is not something to bet a
ground plane on. Re-running reproduces the tiles byte for byte.

Seams: emit() takes an element's bounding box and repeats the element at
whichever of the eight neighbouring tile offsets still overlaps the canvas, so
anything crossing an edge comes back on the other side. The crack tiers are
built on torus-wrapped jittered grids, so their vertices meet across the seam
by construction.

Tiling repetition, not the seam, is the risk at 43 repeats. Three things fight
it: every scatter is a jittered grid rather than free random, so the tile has
no dense corner and no bare one to recognize; the cracks come in two cell sizes
so no single motif carries the frame; and the large soft features stay near
zero contrast, because broad light and dark on this ground is the view's
cloud shadow layer, not the texture's job.
"""

import math
import os
import sys

T = 1024.0
SEED = 0x51ED7EA1

BASE = 232
CRACK = 0.86
CRACK_CORE = 0.76

SWELL_TILT = 0.04
PEBBLE_TILT = 0.26
FACET_TILT = 0.11
GROOVE_TILT = 0.42
GREEN = -1.0


class Rng:
    def __init__(self, seed):
        self.state = 0
        self.inc = 0x14057B7EF767814F
        self._step()
        self.state = (self.state + seed) & 0xFFFFFFFFFFFFFFFF
        self._step()

    def _step(self):
        self.state = (self.state * 6364136223846793005 + self.inc) \
            & 0xFFFFFFFFFFFFFFFF

    def u32(self):
        old = self.state
        self._step()
        xorshifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rot = (old >> 59) & 31
        return ((xorshifted >> rot) | (xorshifted << ((-rot) & 31))) \
            & 0xFFFFFFFF

    def f(self):
        return self.u32() / 4294967296.0

    def r(self, lo, hi):
        return lo + (hi - lo) * self.f()


def n(v):
    s = "%.1f" % v
    if s.endswith(".0"):
        s = s[:-2]
    if s == "-0":
        s = "0"
    return s


def offsets(x0, y0, x1, y1):
    out = []
    for dx in (-T, 0.0, T):
        if x1 + dx <= 0 or x0 + dx >= T:
            continue
        for dy in (-T, 0.0, T):
            if y1 + dy <= 0 or y0 + dy >= T:
                continue
            out.append((dx, dy))
    return out


def srgb_to_linear(c):
    c = c / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    if c <= 0.0031308:
        v = c * 12.92
    else:
        v = 1.055 * (c ** (1.0 / 2.4)) - 0.055
    return max(0, min(255, int(round(v * 255.0))))


def grey(mul, base):
    return linear_to_srgb(srgb_to_linear(base) * mul)


def hexc(r, g, b):
    return "#%02x%02x%02x" % (int(r), int(g), int(b))


def nrm(nx, ny, nz=None):
    if nz is None:
        nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
    ln = math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / ln, ny / ln, nz / ln
    return hexc(round((nx * 0.5 + 0.5) * 255),
                round((ny * GREEN * 0.5 + 0.5) * 255),
                round((nz * 0.5 + 0.5) * 255))


def mr(rough, metal=0.0):
    return hexc(0, max(0, min(255, round(rough * 255))),
                max(0, min(255, round(metal * 255))))


def build_features():
    rng = Rng(SEED)
    f = {}

    patches = []
    grid = 4
    cell = T / grid
    for i in range(grid):
        for j in range(grid):
            if rng.f() < 0.18:
                continue
            patches.append((
                (i + rng.r(0.05, 0.95)) * cell,
                (j + rng.r(0.05, 0.95)) * cell,
                rng.r(0.55, 1.25) * cell,
                rng.f() < 0.5,
                rng.r(0.7, 1.0),
            ))
    f["patches"] = patches

    def crack_tier(gn, drop, jitter, wmin, wmax, depth):
        gcell = T / gn
        verts = [[(
            (i + 0.5 + rng.r(-jitter, jitter)) * gcell,
            (j + 0.5 + rng.r(-jitter, jitter)) * gcell,
        ) for j in range(gn)] for i in range(gn)]

        out = []
        for i in range(gn):
            for j in range(gn):
                for axis in (0, 1):
                    if rng.f() < drop:
                        continue
                    ax, ay = verts[i][j]
                    if axis == 0:
                        bx, by = verts[(i + 1) % gn][j]
                        if i + 1 == gn:
                            bx += T
                    else:
                        bx, by = verts[i][(j + 1) % gn]
                        if j + 1 == gn:
                            by += T
                    dx, dy = bx - ax, by - ay
                    ln = math.hypot(dx, dy)
                    if ln < 1e-3:
                        continue
                    px, py = -dy / ln, dx / ln
                    steps = 5
                    pts = []
                    for s in range(steps + 1):
                        t = s / steps
                        lat = 0.0
                        if 0 < s < steps:
                            lat = rng.r(-0.11, 0.11) * ln
                        pts.append(
                            (ax + dx * t + px * lat, ay + dy * t + py * lat))
                    out.append((pts, rng.r(wmin, wmax), depth))
        return out

    f["cracks"] = (crack_tier(5, 0.5, 0.34, 6.0, 11.0, 0.8)
                   + crack_tier(9, 0.42, 0.36, 3.2, 6.0, 0.78))

    pebbles = []
    pg = 14
    pcell = T / pg
    for i in range(pg):
        for j in range(pg):
            if rng.f() < 0.42:
                continue
            rad = rng.r(4.0, 6.0) + rng.r(0.0, 11.0) * rng.f()
            pebbles.append((
                (i + rng.r(-0.15, 1.15)) * pcell,
                (j + rng.r(-0.15, 1.15)) * pcell,
                rad,
                rad * rng.r(0.6, 1.0),
                rng.r(0, 180),
                rng.r(-1.0, 1.0),
            ))
    f["pebbles"] = pebbles

    grit = []
    gg = 36
    gcell = T / gg
    for i in range(gg):
        for j in range(gg):
            if rng.f() < 0.2:
                continue
            grit.append((
                (i + rng.r(0.05, 0.95)) * gcell,
                (j + rng.r(0.05, 0.95)) * gcell,
                rng.r(4.0, 10.0),
                rng.r(-1.0, 1.0),
            ))
    f["grit"] = grit

    grain = []
    ng = 52
    ncell = T / ng
    for i in range(ng):
        for j in range(ng):
            if rng.f() < 0.25:
                continue
            grain.append((
                (i + rng.r(0.0, 0.8)) * ncell,
                (j + rng.r(0.0, 0.8)) * ncell,
                rng.r(4.0, 8.0),
                rng.u32() % 4,
            ))
    f["grain"] = grain

    swells = []
    sg = 6
    scell = T / sg
    for i in range(sg):
        for j in range(sg):
            swells.append((
                (i + rng.r(0.0, 1.0)) * scell,
                (j + rng.r(0.0, 1.0)) * scell,
                rng.r(0.5, 1.1) * scell,
                rng.r(0.35, 1.0),
                rng.f() < 0.5,
            ))
    f["swells"] = swells

    f["facets"] = [rng.r(0, 360) for _ in range(len(grit))]
    return f


class Svg:
    def __init__(self, title):
        self.parts = [
            '<svg xmlns="http://www.w3.org/2000/svg" width="1024" '
            'height="1024" viewBox="0 0 1024 1024">',
            "<title>%s</title>" % title,
        ]

    def raw(self, s):
        self.parts.append(s)

    def emit(self, bbox, make):
        x0, y0, x1, y1 = bbox
        for dx, dy in offsets(x0, y0, x1, y1):
            self.parts.append(make(dx, dy))

    def out(self):
        self.parts.append("</svg>")
        return "\n".join(self.parts) + "\n"


def ellipse(cx, cy, rx, ry, rot, fill):
    return ('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="%s" '
            'transform="rotate(%s %s %s)"/>' % (
                n(cx), n(cy), n(rx), n(ry), fill, n(rot), n(cx), n(cy)))


def polyline(pts, stroke, width):
    d = "M" + " ".join("%s %s" % (n(x), n(y)) for x, y in pts)
    return ('<path d="%s" fill="none" stroke="%s" stroke-width="%s" '
            'stroke-linecap="round" stroke-linejoin="round"/>' % (
                d, stroke, n(width)))


def square(x, y, sz):
    return "M%s %sh%sv%sh-%sz" % (n(x), n(y), n(sz), n(sz), n(sz))


def wedges(cx, cy, rad, count, encode):
    out = []
    rr = rad / math.cos(math.pi / count)
    for k in range(count):
        a0 = 2.0 * math.pi * k / count
        a1 = 2.0 * math.pi * (k + 1) / count
        out.append('<path d="M%s %sL%s %sL%s %sZ" fill="%s"/>' % (
            n(cx), n(cy),
            n(cx + rr * math.cos(a0)), n(cy + rr * math.sin(a0)),
            n(cx + rr * math.cos(a1)), n(cy + rr * math.sin(a1)),
            encode(0.5 * (a0 + a1))))
    return "".join(out)


def gen_albedo(f):
    s = Svg("Ore Else terrain dirt - albedo")
    light = hexc(grey(1.035, BASE), grey(1.03, BASE), grey(1.01, BASE))
    dark = hexc(grey(0.945, BASE), grey(0.945, BASE), grey(0.955, BASE))
    s.raw('<defs>%s%s</defs>' % (
        '<radialGradient id="pL"><stop offset="0" stop-color="%s" '
        'stop-opacity="0.5"/><stop offset="0.55" stop-color="%s" '
        'stop-opacity="0.32"/><stop offset="1" stop-color="%s" '
        'stop-opacity="0"/></radialGradient>' % (light, light, light),
        '<radialGradient id="pD"><stop offset="0" stop-color="%s" '
        'stop-opacity="0.5"/><stop offset="0.55" stop-color="%s" '
        'stop-opacity="0.32"/><stop offset="1" stop-color="%s" '
        'stop-opacity="0"/></radialGradient>' % (dark, dark, dark)))
    s.raw('<rect width="1024" height="1024" fill="%s"/>' % hexc(
        BASE, BASE - 1, BASE - 4))

    for cx, cy, rad, packed, amt in f["patches"]:
        gid = "pD" if packed else "pL"
        s.emit((cx - rad, cy - rad, cx + rad, cy + rad),
               lambda dx, dy, cx=cx, cy=cy, rad=rad, gid=gid, amt=amt:
               '<circle cx="%s" cy="%s" r="%s" fill="url(#%s)" '
               'opacity="%s"/>' % (
                   n(cx + dx), n(cy + dy), n(rad), gid, n(amt)))

    for pts, w, depth in f["cracks"]:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        bb = (min(xs) - w, min(ys) - w, max(xs) + w, max(ys) + w)
        rim = 1.0 - (1.0 - CRACK) * depth
        core = 1.0 - (1.0 - CRACK_CORE) * depth
        s.emit(bb, lambda dx, dy, pts=pts, w=w, rim=rim: polyline(
            [(x + dx, y + dy) for x, y in pts],
            hexc(grey(rim, BASE), grey(rim, BASE), grey(rim - 0.02, BASE)), w))
        s.emit(bb, lambda dx, dy, pts=pts, w=w, core=core: polyline(
            [(x + dx, y + dy) for x, y in pts],
            hexc(grey(core, BASE), grey(core, BASE), grey(core - 0.02, BASE)),
            w * 0.42))

    for cx, cy, rx, ry, rot, tone in f["pebbles"]:
        mul = 1.0 + tone * 0.065
        col = hexc(grey(mul, BASE), grey(mul, BASE), grey(mul - 0.015, BASE))
        rr = max(rx, ry)
        s.emit((cx - rr, cy - rr, cx + rr, cy + rr),
               lambda dx, dy, cx=cx, cy=cy, rx=rx, ry=ry, rot=rot, col=col:
               ellipse(cx + dx, cy + dy, rx, ry, rot, col))

    buckets = {}
    for x, y, sz, tone in f["grit"]:
        key = int(round((1.0 + tone * 0.06) * 200))
        for dx, dy in offsets(x - sz, y - sz, x + sz, y + sz):
            buckets.setdefault(key, []).append(
                square(x + dx - sz * 0.5, y + dy - sz * 0.5, sz))
    for key, ds in sorted(buckets.items()):
        mul = key / 200.0
        s.raw('<path d="%s" fill="%s"/>' % (
            "".join(ds), hexc(grey(mul, BASE), grey(mul, BASE),
                              grey(mul, BASE))))

    gmul = (0.94, 0.972, 1.028, 1.06)
    gbuckets = {0: [], 1: [], 2: [], 3: []}
    for x, y, sz, k in f["grain"]:
        for dx, dy in offsets(x, y, x + sz, y + sz):
            gbuckets[k].append(square(x + dx, y + dy, sz))
    for k in range(4):
        m = gmul[k]
        s.raw('<path d="%s" fill="%s"/>' % (
            "".join(gbuckets[k]),
            hexc(grey(m, BASE), grey(m, BASE), grey(m, BASE))))

    return s.out()


def gen_normal(f):
    s = Svg("Ore Else terrain dirt - normal")
    s.raw('<rect width="1024" height="1024" fill="%s"/>' % nrm(0, 0, 1))

    for cx, cy, rad, amt, up in f["swells"]:
        t = SWELL_TILT * amt * (1.0 if up else -1.0)
        enc = lambda a, t=t: nrm(t * math.cos(a), t * math.sin(a))
        s.emit((cx - rad, cy - rad, cx + rad, cy + rad),
               lambda dx, dy, cx=cx, cy=cy, rad=rad, enc=enc:
               wedges(cx + dx, cy + dy, rad, 10, enc))

    for pts, w, depth in f["cracks"]:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        bb = (min(xs) - w, min(ys) - w, max(xs) + w, max(ys) + w)

        def groove(dx, dy, pts=pts, w=w, depth=depth):
            out = []
            tilt = GROOVE_TILT * depth
            for k in range(len(pts) - 1):
                ax, ay = pts[k][0] + dx, pts[k][1] + dy
                bx, by = pts[k + 1][0] + dx, pts[k + 1][1] + dy
                sx, sy = bx - ax, by - ay
                ln = math.hypot(sx, sy)
                if ln < 1e-3:
                    continue
                px, py = -sy / ln, sx / ln
                for side in (1, -1):
                    o = w * 0.5 * side
                    out.append(
                        '<path d="M%s %sL%s %sL%s %sL%s %sZ" fill="%s"/>' % (
                            n(ax + px * o), n(ay + py * o),
                            n(bx + px * o), n(by + py * o),
                            n(bx), n(by), n(ax), n(ay),
                            nrm(-px * side * tilt, -py * side * tilt)))
            return "".join(out)

        s.emit(bb, groove)

    for cx, cy, rx, ry, rot, tone in f["pebbles"]:
        rad = 0.5 * (rx + ry)
        t = PEBBLE_TILT * (0.6 + 0.4 * min(1.0, rad / 16.0))
        enc = lambda a, t=t: nrm(t * math.cos(a), t * math.sin(a))
        count = 6 if rad < 9 else (8 if rad < 15 else 12)
        s.emit((cx - rad, cy - rad, cx + rad, cy + rad),
               lambda dx, dy, cx=cx, cy=cy, rad=rad, enc=enc, count=count:
               wedges(cx + dx, cy + dy, rad, count, enc))

    buckets = {}
    for idx, (x, y, sz, tone) in enumerate(f["grit"]):
        key = int(f["facets"][idx] / 45.0) % 8
        for dx, dy in offsets(x - sz, y - sz, x + sz, y + sz):
            buckets.setdefault(key, []).append(
                square(x + dx - sz * 0.5, y + dy - sz * 0.5, sz))
    for key, ds in sorted(buckets.items()):
        a = math.radians(key * 45.0 + 22.5)
        s.raw('<path d="%s" fill="%s"/>' % (
            "".join(ds), nrm(FACET_TILT * math.cos(a),
                             FACET_TILT * math.sin(a))))

    return s.out()


def gen_rough(f):
    s = Svg("Ore Else terrain dirt - metallic roughness")
    s.raw('<defs><radialGradient id="rP">'
          '<stop offset="0" stop-color="%s" stop-opacity="0.85"/>'
          '<stop offset="0.6" stop-color="%s" stop-opacity="0.5"/>'
          '<stop offset="1" stop-color="%s" stop-opacity="0"/>'
          '</radialGradient></defs>' % (mr(0.78), mr(0.78), mr(0.78)))
    s.raw('<rect width="1024" height="1024" fill="%s"/>' % mr(1.0))

    for cx, cy, rad, packed, amt in f["patches"]:
        if not packed:
            continue
        s.emit((cx - rad, cy - rad, cx + rad, cy + rad),
               lambda dx, dy, cx=cx, cy=cy, rad=rad:
               '<circle cx="%s" cy="%s" r="%s" fill="url(#rP)"/>' % (
                   n(cx + dx), n(cy + dy), n(rad)))

    for pts, w, depth in f["cracks"]:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        bb = (min(xs) - w, min(ys) - w, max(xs) + w, max(ys) + w)
        s.emit(bb, lambda dx, dy, pts=pts, w=w: polyline(
            [(x + dx, y + dy) for x, y in pts], mr(1.0), w))

    for cx, cy, rx, ry, rot, tone in f["pebbles"]:
        r = 0.62 + 0.12 * (tone * 0.5 + 0.5)
        rr = max(rx, ry)
        s.emit((cx - rr, cy - rr, cx + rr, cy + rr),
               lambda dx, dy, cx=cx, cy=cy, rx=rx, ry=ry, rot=rot, r=r:
               ellipse(cx + dx, cy + dy, rx, ry, rot, mr(r)))

    buckets = {}
    for x, y, sz, tone in f["grit"]:
        key = int(round((0.80 + tone * 0.10) * 40))
        for dx, dy in offsets(x - sz, y - sz, x + sz, y + sz):
            buckets.setdefault(key, []).append(
                square(x + dx - sz * 0.5, y + dy - sz * 0.5, sz))
    for key, ds in sorted(buckets.items()):
        s.raw('<path d="%s" fill="%s"/>' % ("".join(ds), mr(key / 40.0)))

    return s.out()


def main():
    if len(sys.argv) > 1:
        out_dir = sys.argv[1]
    else:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "etc", "assets")
    out_dir = os.path.abspath(out_dir)

    f = build_features()
    for name, data in (
        ("terrain_dirt.svg", gen_albedo(f)),
        ("terrain_dirt_normal.svg", gen_normal(f)),
        ("terrain_dirt_rough.svg", gen_rough(f)),
    ):
        path = os.path.join(out_dir, name)
        with open(path, "w") as fp:
            fp.write(data)
        print("%s  %d bytes" % (path, len(data)))


if __name__ == "__main__":
    main()
