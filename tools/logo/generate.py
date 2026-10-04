#!/usr/bin/env python3
"""Build Lockstep's SVG logo set from the raster original."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace as T
from PIL import Image
import numpy as np
from scipy import ndimage

SRC = '/home/mounty/Documents/lockstep_logo_3.png'
OUT = '/home/programming/seq_play/website/static/img'

CYAN, BLUE, CREAM, INK = '#2AF4F1', '#3C8FD1', '#EAE6DD', '#161B21'

a = np.array(Image.open(SRC).convert('RGBA'))[:, :, 3]
mask = a > 160
mark = mask.copy(); mark[1326:, :] = False
text = mask.copy(); text[:1326, :] = False

def crop(m):
    ys, xs = np.nonzero(m)
    return m[ys.min():ys.max()+1, xs.min():xs.max()+1], xs.min(), ys.min()

mk, mx0, my0 = crop(mark)
tk, tx0, ty0 = crop(text)
MH, MW = mk.shape
TH, TW = tk.shape
GAP = ty0 - (my0 + MH)

# The mark is two interlocking ribbons. Keep them as separate paths: they are
# separate shapes in the design, and emitting them as one lets a renderer merge
# them where they pass close.
lab, n = ndimage.label(mk)
ribbons = [T.region_paths(lab == i, 1.0, 0, 0, eps=1.1)
           for i in range(1, n + 1) if (lab == i).sum() > 500]

def grad(x2, y2):
    return ('  <linearGradient id="g" x1="0" y1="0" x2="%s" y2="%s">\n'
            '    <stop offset="0" stop-color="%s"/>\n'
            '    <stop offset="1" stop-color="%s"/>\n'
            '  </linearGradient>\n' % (x2, y2, CYAN, BLUE))

def shift(d, dx, dy):
    """Translate a path's absolute coordinates."""
    out = []
    for sub in d.split('M'):
        if not sub.strip():
            continue
        pts = sub.strip().rstrip('Z').split()
        moved = []
        for p in pts:
            x, y = p.split(',')
            moved.append('%.2f,%.2f' % (float(x) + dx, float(y) + dy))
        out.append('M' + ' '.join(moved) + 'Z')
    return ' '.join(out)

HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" '
        'role="img" aria-label="Lockstep">\n')

# ---------------------------------------------------------------- mark only
# Square and padded: a mark gets asked for a square far more often than for its
# own aspect ratio -- avatar, navbar, app icon, favicon -- and padding it here
# saves every consumer doing it.
S, PAD = 512, 18
sc = min((S - 2*PAD) / MW, (S - 2*PAD) / MH)
ox, oy = (S - MW*sc)/2, (S - MH*sc)/2
with open(OUT + '/logo-mark.svg', 'w') as f:
    f.write(HEAD % (S, S))
    f.write('<defs>\n' + grad('0.25', '1') + '</defs>\n')
    for r in ribbons:
        scaled = T.region_paths(lab == (ribbons.index(r) + 1), sc, ox, oy, eps=1.6)
        f.write('  <path fill="url(#g)" fill-rule="evenodd" d="%s"/>\n' % scaled)
    f.write('</svg>\n')

# ---------------------------------------------------------------- full lockup
W = max(MW, TW)
H = MH + GAP + TH
mdx, tdx = (W - MW)/2, (W - TW)/2
d_text = shift(T.region_paths(tk, 1.0, 0, 0, eps=0.9), tdx, MH + GAP)

def write_full(path, textfill):
    with open(path, 'w') as f:
        f.write(HEAD % (W, H))
        f.write('<defs>\n' + grad('0.25', '1') + '</defs>\n')
        for r in ribbons:
            f.write('  <path fill="url(#g)" fill-rule="evenodd" d="%s"/>\n' % shift(r, mdx, 0))
        f.write('  <path fill="%s" fill-rule="evenodd" d="%s"/>\n' % (textfill, d_text))
        f.write('</svg>\n')

write_full(OUT + '/logo-full.svg', CREAM)       # for dark backgrounds
write_full(OUT + '/logo-full-light.svg', INK)   # for light backgrounds

for n_ in ('logo-mark', 'logo-full', 'logo-full-light'):
    print('%-18s %6d bytes' % (n_, os.path.getsize('%s/%s.svg' % (OUT, n_))))
print('mark %dx%d  text %dx%d  gap %d  ribbons %d' % (MW, MH, TW, TH, GAP, len(ribbons)))
