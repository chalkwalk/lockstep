# Logo derivation

`website/static/img/logo-*.svg` are **generated from the raster original**, not
drawn by hand, so they can be regenerated if the source artwork changes.

```bash
python3 tools/logo/generate.py            # needs numpy, scipy, Pillow
```

- `trace.py` — marching-squares contour extraction plus Ramer-Douglas-Peucker
  simplification. Tracing is done one connected region at a time: the generic
  contour follower merges loops wherever two boundaries share a point, which
  real artwork does constantly, and a region traced alone has exactly one
  boundary and nothing to merge with.
- `generate.py` — masks the source, traces the two ribbons and the wordmark,
  and writes the SVG set with a shared gradient.

**The source raster is not in the repository.** `generate.py` points at
`~/Documents/lockstep_logo_3.png`; keep the original somewhere safe and update
the path if it moves. The SVGs are the artefact that ships.

## What is generated, and what is not

| File | Generated | Notes |
|---|---|---|
| `logo-mark.svg` | yes | The two interlocking ribbons, square and padded. |
| `logo-full.svg` | yes | Mark plus wordmark, cream text, for dark grounds. |
| `logo-full-light.svg` | yes | Same with ink text, for light grounds. |
| `favicon.svg` | **no** | Hand-drawn simplification; see below. |
| `favicon-alt.svg` | **no** | Alternative simplification, unused. |
| `social-card.png` | no | `rsvg-convert` of `logo-full.svg` onto `#14181d`. |

## Why the favicon is not the logo

The mark is two gear-toothed ribbons locked together. Rendered at 16, 24, 32,
48 and 64 px, the teeth alias into a blur and the interlock -- the whole idea
of the mark -- is the first thing to go. It only begins to read at about 48 px.

`favicon.svg` therefore keeps the idea and drops the detail that cannot survive
small sizes: two links, the same rotational symmetry, the same gradient,
stroked heavily enough to hold at 16 px. It was checked at that size rather
than assumed, and the same check is why the navbar uses it too -- Docusaurus
renders the navbar logo at about 32 px.
