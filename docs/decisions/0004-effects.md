# D-L4: effects

Adopted 27 October 2026.

Effects are raster operations on each object's layer alpha at the output resolution, in the Qt-free core
(`core/effects`). Sizes are artboard units multiplied by the output scale, so 64 px and 512 px exports match
proportionally (verified by a downsampled comparison). Fixed order: drop shadow, outer glow, fill + outlines, inner
shadow, inner glow, bevel & emboss. Blur is a three-pass box blur approximating a Gaussian with clamp-to-edge.
Bevel uses an exact Euclidean distance transform (Felzenszwalb–Huttenlocher) of the coverage, a profile curve
(linear/round/cove) for height, a minimum 1 px height smoothing to keep normals free of stair-step stipple, Lambert
lighting from angle/altitude, highlight in Screen and shadow in Multiply. Inner/Outer/Emboss/Pillow are height-field
variants. Effects below 0.75 output px are skipped instead of producing a muddy fringe.

Validation: `render_contracts` (distance field, blur mass, zero-size no-op, bevel symmetry, scale invariance, bounded
memory on a 4096 px export with effects).
