# D-L3: fonts and text shaping

Adopted 27 October 2026.

The core defines `TextShaper`, a narrow interface that returns plain `Path` geometry for a `TextData`. The Qt
implementation in `kantan_io` lays text out with `QTextLayout` (HarfBuzz kerning and ligatures) at a fixed 256 px
reference size without hinting, extracts glyph outlines with `QRawFont::pathForGlyph`, then scales, aligns, applies
tracking, line height and arc bend. Results are cached by every input that affects geometry and shaping is serialized,
so worker-thread exports are safe.

Imported fonts are copied into the app library (AppData/fonts), registered with `QFontDatabase`, and embedded in the
project by default. A per-font "do not embed" option stores the name only; on open a missing font is reported and
rendered with the system fallback while the project keeps the original name and re-links when available.

Validation: `io_contracts` (embedded font round trip, linked fonts re-link from the library, tracking, arc bend,
missing-font fallback, Convert to path equals the live render).
