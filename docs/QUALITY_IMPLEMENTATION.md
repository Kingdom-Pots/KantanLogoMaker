# Quality implementation record

30 October 2026. This records delivered work against PROFESSIONAL_QUALITY_PLAN.md. The build is a quality preview; the complete professional-release roadmap is not yet finished.

## Delivered

- Application-wide property wheel protection for spin boxes, sliders, and dropdowns, including controls in dialogs and toolbars. Fields require explicit focus. Wheel events over inactive fields are forwarded to enclosing scroll areas, preserving trackpad pixel/angle deltas and gesture phase.
- Inspector wheel edits are grouped into one undo operation per gesture. Focus changes or typing end the gesture; Escape cancels it.
- Shared scrub labels with a drag threshold, fine/coarse modifiers that do not jump when changed, Escape/deactivation cancellation, hover feedback, accessible names, and one undo step. X/Y/W/H/Angle and slider labels now scrub. Numeric multi-object scrubbing applies relative changes; typing remains absolute.
- Right-click numeric property names for a reset action that displays its target value. Resets change only that property and support undo. Single-value scrubbing stops accumulating excess movement at a bound, so reversing the pointer adjusts the value immediately.
- Transform is first in the Inspector. Locked selections show an explanation and disable property editing.
- Six canvas alignment buttons beside Transform. Explicit toolbar targets distinguish Selection, Canvas as an arrangement, and Canvas for each object. Arrangement alignment preserves spacing.
- Core resize snapping for axis-aligned frames, with canvas/object/guide targets, zoom-adjusted tolerance, capture/release hysteresis, fixed anchors, uniform corner scaling, centre scaling, and negative scales. Ctrl bypasses move/resize snapping; pink guides appear during both operations. Rotated frames retain free resizing.
- Artwork rendering reuses the existing image when the immutable document, view transform, viewport dimensions, and DPI are unchanged. Overlay-only updates no longer force a full render. Checkerboard painting is bounded to the visible viewport.
- The object tree avoids rebuilding unchanged committed content during previews. Search matches names/types and preserves matching ancestors; clearing search restores prior collapsed groups. Reordering is disabled while filtering. Tree scrolling is preserved when selection does not change.
- Zoom to Selection, workspace layout reset, focused-field borders, and updated shortcut/user documentation.
- Autosave marks a state as saved only after successful IO, so failures retry. Async IO exceptions become reported errors. Failed exports report their destination and any successfully written files.
- Font fallback checks that the chosen family actually exists. Font previews handle pixel-sized fonts without passing invalid point sizes to Qt.
- Windows UI tests register a known font, isolate settings and temporary files, fail unexpected message dialogs, and time out after 90 seconds. Detailed JUnit output is retained.
- Packaging installs the required MSVC redistributable libraries beside the executable. A package-validation script checks dependencies and native startup at 100/150/200% scaling without Qt on PATH, optionally capturing screenshots.
- The benchmark script tolerates unavailable system-description services and cleans up its own helper process on failure.

## Verification

- MSVC Release build passes with strict warning settings.
- All five contract suites pass: core, rendering, architecture, IO, and UI. The UI report contains 25 cases including setup/cleanup, with zero failures.
- Formatting check passes.
- Regressions cover unfocused wheel safety and actual parent-panel scrolling, intentional focused wheel edits, wheel undo grouping, transform scrub undo/cancel, canvas arrangement alignment, resize snapping/undo, locked properties, object search, and artwork cache invalidation.
- Existing UI save/open/export tests now complete; unexpected dialogs fail explicitly instead of stalling indefinitely.
- Packaged native startup succeeds at 100/150/200% scaling with developer Qt paths cleared. Selected-object layouts were visually inspected. These are local scale-factor checks, not a physical monitor-transition test.
- The core 24-star/effects benchmark recorded approximately 22 ms at 512 px and 68 ms at 1024 px on this machine. These are single render samples, not interaction percentiles or before/after speedup claims.
- A native badge baseline recorded about 306 ms to report readiness, 60 MiB working set, 33 MiB private memory, and zero measured normalized CPU use over a 30-second idle interval. System-description metadata used the environment fallback because CIM access was unavailable. Each measurement records its executable hash; it is a baseline, not certification of every document or the final build's performance targets.

Reproduce with `scripts/build.ps1`, `scripts/format.ps1 -Check`, `scripts/package.ps1`, and `scripts/validate-package.ps1 -Screenshots`.

Evidence lives in out/quality-tests-final.log, out/desktop-msvc1938/ui-contracts.xml, out/quality-format-final.log, out/quality-render-baseline.csv, out/package-validation/, and out/quality-measurements/. Generated outputs are not source-controlled.

## Remaining roadmap and release gates

The original plan remains useful. These items have not been completed or certified by this implementation:

1. Rotated-frame resize snapping and an optional visible-bounds alignment mode require separate geometry rules; current alignment uses geometric bounds. Axis-aligned resizing is the delivered scope.
2. Representative text-heavy, image-heavy, boolean, and dense-effects documents still need drag/scrub percentile measurements and a long edit/undo memory soak. Immutable render reuse is verified; the proposed latency targets have not been certified. Rendering changed documents is still synchronous.
3. Physical mixed-DPI monitor transitions, trackpad hardware, screen-reader behavior, keyboard-only end-to-end use, and a new-user workflow trial require hands-on validation beyond the automated checks.
4. Crash/recovery and IO fault injection need broader end-to-end coverage, including close-during-save and disconnected destinations. Existing atomic-write and failure-preservation contracts pass.
5. A genuinely clean Windows machine/account, native ICO/resource-compiler checks, installer/uninstaller, signing, and distribution-license review remain release gates. Local PATH-isolated startup does not establish clean-machine compatibility.

Do not describe this preview as a certified professional release until these gates are closed. The reported scroll, scrub, and canvas-alignment problems have concrete implementations and regression coverage now.
