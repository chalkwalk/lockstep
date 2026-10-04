#!/usr/bin/env python3
"""Vectorise a binary mask into SVG path data.

Marching squares to get closed iso-contours at the 0.5 level, then
Ramer-Douglas-Peucker to drop the points that are not carrying any shape.
The artwork is mostly straight edges (gear teeth, chevrons, letterforms), so a
simplified polygon is visually identical to a curve fit and far easier to
reason about than a pile of beziers.
"""
import numpy as np


def contours(mask):
    """All closed boundary loops of a binary mask, as lists of (x, y)."""
    m = np.pad(mask.astype(np.uint8), 1)
    h, w = m.shape

    # Edge crossings per cell, keyed by the 4-bit corner code.
    # Each entry maps the cell to the segment(s) it contributes, as
    # (from_edge, to_edge) with edges numbered N=0 E=1 S=2 W=3.
    seg = {
        1: [(3, 2)], 2: [(2, 1)], 3: [(3, 1)], 4: [(1, 0)],
        5: [(3, 0), (1, 2)], 6: [(2, 0)], 7: [(3, 0)], 8: [(0, 3)],
        9: [(0, 2)], 10: [(0, 1), (2, 3)], 11: [(0, 1)], 12: [(1, 3)],
        13: [(1, 2)], 14: [(2, 3)],
    }

    def mid(cell, e):
        y, x = cell
        return {0: (x + 0.5, y), 1: (x + 1.0, y + 0.5),
                2: (x + 0.5, y + 1.0), 3: (x, y + 0.5)}[e]

    # Build an adjacency of segment endpoints.
    links = {}
    for y in range(h - 1):
        row0, row1 = m[y], m[y + 1]
        for x in range(w - 1):
            code = (row0[x] << 3) | (row0[x + 1] << 2) | (row1[x + 1] << 1) | row1[x]
            if code in (0, 15):
                continue
            for a, b in seg[code]:
                pa, pb = mid((y, x), a), mid((y, x), b)
                links.setdefault(pa, []).append(pb)

    loops = []
    while links:
        start = next(iter(links))
        loop = [start]
        cur = start
        while True:
            nxts = links.get(cur)
            if not nxts:
                break
            nxt = nxts.pop()
            if not nxts:
                del links[cur]
            if nxt == start:
                break
            loop.append(nxt)
            cur = nxt
        if len(loop) > 8:
            loops.append(loop)
    return loops


def rdp(pts, eps):
    if len(pts) < 3:
        return pts
    pts = np.asarray(pts, dtype=float)
    keep = np.zeros(len(pts), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        if j <= i + 1:
            continue
        a, b = pts[i], pts[j]
        ab = b - a
        n = np.hypot(*ab)
        seg = pts[i + 1:j]
        if n == 0:
            d = np.hypot(*(seg - a).T)
        else:
            d = np.abs(np.cross(ab, seg - a)) / n
        k = int(np.argmax(d))
        if d[k] > eps:
            k += i + 1
            keep[k] = True
            stack += [(i, k), (k, j)]
    return [tuple(p) for p in pts[keep]]


def to_path(mask, scale, ox, oy, eps=1.0, prec=2):
    out = []
    for loop in contours(mask):
        s = rdp(loop + [loop[0]], eps)
        if len(s) < 4:
            continue
        pts = [((x - 1) * scale + ox, (y - 1) * scale + oy) for x, y in s]
        d = "M" + " ".join("%.*f,%.*f" % (prec, x, prec, y) for x, y in pts) + "Z"
        out.append(d)
    return " ".join(out)


def region_paths(mask, scale, ox, oy, eps=1.0, prec=2, min_area=40):
    """Trace each connected region on its own, holes included.

    The generic contour follower merges loops wherever two boundaries share a
    point, which real artwork does constantly. Tracing one simply-connected
    region at a time removes the possibility: a filled region has exactly one
    boundary, so there is nothing to merge it with.

    The mask is padded first. Without it, a shape touching the bounding box
    splits the surrounding background into several regions and none of them
    can be identified as "outside", so the outside gets emitted as a filled
    rectangle. With a border of background all the way round, the outside is
    exactly the region containing the corner.

    Returned subpaths are meant to be filled with fill-rule="evenodd", so the
    holes punch through rather than painting over.
    """
    from scipy import ndimage
    pad = 1
    m = np.pad(np.asarray(mask, dtype=bool), pad)
    out = []

    def emit(binary):
        ls = contours(binary)
        if not ls:
            return
        s_ = rdp(max(ls, key=len) + [max(ls, key=len)[0]], eps)
        if len(s_) < 4:
            return
        # contours() pads by one as well, hence the two subtractions.
        pts = [((x - 1 - pad) * scale + ox, (y - 1 - pad) * scale + oy)
               for x, y in s_]
        out.append("M" + " ".join("%.*f,%.*f" % (prec, x, prec, y)
                                  for x, y in pts) + "Z")

    lab, n = ndimage.label(m)
    for i in range(1, n + 1):
        comp = lab == i
        if comp.sum() < min_area:
            continue
        emit(ndimage.binary_fill_holes(comp))

    bg, nb = ndimage.label(~m)
    outside = bg[0, 0]
    for i in range(1, nb + 1):
        if i == outside:
            continue
        hole = bg == i
        if hole.sum() < min_area:
            continue
        emit(hole)

    return " ".join(out)
