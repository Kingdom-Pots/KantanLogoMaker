# D-L5: code reuse from Kantan NineSlice

Adopted 27 October 2026 (owner's decision: copy and adapt).

LogoMaker is a standalone repository. Build files, presets, scripts, `.clang-format`, the dependency record format,
compact controls, tool-icon plumbing, the icon pipeline, `WriteCoordinator`, the blend equations, the ZIP allocator
and validation approach, and the dark theme were copied and adapted from NineSlice (see PROVENANCE.md). Raster
painting features (tiles, brushes, regions, masks, nine-slice) were not carried over.

Consequence: fixes to shared pieces must be applied to each tool by hand. Revisit (extract a shared Kantan library)
when a third tool needs the same changes in lockstep.
