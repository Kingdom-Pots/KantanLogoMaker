# Source provenance

Kantan LogoMaker was started on 27 October 2026 as a new repository following D-L5 (copy and adapt from Kantan
NineSlice at `../KantanNineSlice`). The NineSlice checkout remains independent; there are no runtime or build-path
dependencies on it.

Copied or adapted from NineSlice: CMake structure and presets, `cmake/CheckArchitecture.cmake` (rules rewritten for
vector modules), `scripts/` (setup, build, format, package, run, benchmark, icon rendering), `.clang-format`,
`.gitignore`, `dependencies.json` structure, `CompactControls.h`, `ToolIcons`, the dark style sheet and `main.cpp`
launch options, `WriteCoordinator` (unchanged), the blend equations in `core/composite` (unchanged), checked
coordinate helpers and `Pixel` operations in `core/base/Types.h`, the memory budget/reservation classes, the
budget-aware Miniz allocator and archive validation approach in `io/ProjectIO.cpp`, the maintenance policy, and the
Fluent icons already used by NineSlice.

Written for LogoMaker: geometry, shapes, Clipper2 adapter, coverage rasterizer, effects, renderer, image sampling,
document model and editor, transforms/snapping/alignment for vector objects, serialization, project format, ICO
writer, export job, font library and text shaper, SVG import, canvas tools, object panel, inspector, presets panel,
size strip, export and artboard dialogs, presets and templates, the eight SVG symbols in `app/assets/presets`, and all
tests.

Third-party additions: Clipper2 1.5.4 and nanosvg (vendored, unmodified), additional Fluent System Icons from the
same pinned commit, and `tests/fixtures/KantanTestSans.ttf`, a renamed glyph subset of DejaVu Sans Bold used only by
tests (Bitstream Vera licence alongside it).
