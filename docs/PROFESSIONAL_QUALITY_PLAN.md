# Kantan LogoMaker: professional quality plan

30 October 2026. Proposed implementation plan based on the current source, the reported interaction problems, and the Windows build/test work in this chat. No UI changes are implemented by this document. Findings below distinguish code observations from improvements that still require hands-on validation.

Execution has started. See [QUALITY_IMPLEMENTATION.md](QUALITY_IMPLEMENTATION.md) for delivered changes, verification evidence, and remaining roadmap/release gates. The findings below describe the baseline before implementation.

## Goal

Make routine editing predictable, precise, reversible, and responsive. A user should be able to build a logo without accidentally changing properties, fighting transforms, losing work, or guessing what a command will do. Complete this foundation before expanding the feature list.

## Current findings

| Area | Evidence | Implication |
| --- | --- | --- |
| Accidental wheel edits | Inspector.cpp creates ordinary spin boxes, sliders, and combo boxes without a wheel guard. Transform controls use a separate construction path. | Fix every property-control family, including toolbars and dialogs; fixing only Inspector::number leaves gaps. |
| Incomplete scrubbing | Inspector.cpp's ScrubLabel supports horizontal drag and modifiers for fields built by number(). Transform fields use ordinary labels/prefixes. ScrubLabel has no explicit Escape cancellation or drag threshold. | Extend and standardize the existing behavior rather than introducing a competing implementation. |
| Alignment exists but is easy to miss | MainWindow.cpp has Align to Selection/Artboard and six alignment commands; a single selected object automatically aligns to the artboard. | Improve visibility and make the effective target explicit. Validate the existing commands rather than assuming alignment is absent. |
| Resize snapping is missing | CanvasView.cpp uses snapTranslation during Move, but Scale computes factors directly from the pointer without a snap operation. | Dragging a transform edge onto the canvas edge cannot use the same assistance as moving an object. |
| Potential responsiveness risk | CanvasView::refresh calls render synchronously. SizeStrip::renderAll renders five outputs. | Profile real documents and separate overlay updates from document rendering before adding caches or workers. |
| Validation gap | Windows build, packaging, startup, and four contract suites pass. The offscreen UI suite stalls in save/open/export; earlier cases pass and the run reports missing fonts. | Diagnose the exact modal/error path and restore a bounded, dependable UI test run. Missing fonts are a lead, not a proven root cause. |

Source anchors: app/Inspector.cpp (ScrubLabel, number, slider, combo, buildTransform); app/MainWindow.cpp (alignment toolbar and align); app/CanvasView.cpp (Move, Scale, wheelEvent, refresh); app/SizeStrip.cpp (renderAll). These are navigation references, not proof of untested runtime behavior.

## Phase 1 — Safe property editing

Highest priority. Deliver this before appearance work.

### Wheel behavior

- Interpret “selected property” as a field explicitly activated by clicking or keyboard navigation. Selecting an object, hovering a field, or scrolling over it must not activate that property.
- Unfocused numeric fields, sliders, and closed dropdowns pass wheel input through to the containing scroll area. Use a focus policy that prevents the wheel itself from acquiring focus.
- A focused property may accept wheel changes, with a clear focus indicator. Clicking elsewhere ends that state. Dropdown popups retain normal list scrolling.
- Cover transform fields, shape/text/image properties, effects, tool options, artboard settings, and export dialogs with the same rule.
- Handle mouse wheels and high-resolution trackpads. Scrolling through the Inspector must never dirty the document or add undo entries unless an active property actually changes.

### Numeric field and scrubbing contract

- Extract a reusable numeric property row/control with units, bounds, precision, wheel behavior, reset behavior, and accessible naming. Keep document mutations in the Inspector/controller layer.
- Give X, Y, W, H, angle, and all suitable numeric properties individual scrub targets. A shared Position label must not ambiguously change both axes.
- Drag the property name horizontally to adjust its value; show a horizontal drag cursor, hover treatment, and tooltip. Retain Ctrl for fine adjustment and Shift for coarse adjustment, with sensitivity appropriate to each property.
- Start a drag only after a small pointer threshold. Preserve the initial value and document snapshot; preview during movement; commit once on release; Escape restores the initial state. Define cleanup for interrupted drags and window deactivation.
- Changing precision modifiers mid-drag must not jump the value. Saturating at a limit and dragging back should remain controllable.
- One drag equals one undo operation. A click without movement creates none. Coalesce an intentional wheel gesture rather than creating dozens of undo entries.
- Show mixed values explicitly. Relative scrubbing should preserve differences between selected objects; entering an absolute number should apply that number to all eligible targets. Define equivalent selection-level semantics for transforms separately.
- Add a discoverable Reset action with a named default; preserve direct typing and keyboard access.

Acceptance: scroll repeatedly through a populated Inspector with the pointer crossing every control type; no values or history change. Activate a field and verify intentional wheel changes. Scrub every transform axis, cancel it, undo it, and repeat with mixed selections and locked objects.

## Phase 2 — Precise transforms and canvas alignment

### Make alignment obvious

- Put the six align commands and an explicit target selector beside Transform. Offer Canvas/Artboard and Selection with consistent naming throughout the app.
- Display the effective target, including the single-object case; remove the current mismatch where Selection can be displayed while the command actually uses the artboard.
- Provide clear tooltips such as “Align selection left edge to canvas.” Disable unavailable operations with a reason.
- For several selected objects, offer a clear distinction between aligning each object independently and positioning the combined selection as one arrangement. Preserve relative positions when moving the arrangement to a canvas edge.
- Start with geometric bounds matching the transform box. Explain that strokes and shadows can extend beyond it; consider an explicit visible-bounds mode later, after its rules are specified and tested.

### Snap while resizing

- Add a resize snap solver to the Qt-free editing layer and use it from CanvasView's Scale interaction. Support canvas left/right/top/bottom edges and centers first; then guides and other objects.
- Solve for the dragged edge or handle while preserving the opposite anchor. Respect aspect lock, center resizing, minimum size, and flipped geometry.
- Use a screen-space capture tolerance converted through zoom, with a slightly larger release tolerance to prevent jitter. Show the target line and a short “Canvas right edge” hint.
- For the first increment, guarantee unrotated objects and axis-aligned multi-selection. For rotated objects, define whether snapping targets transformed handles or world-space bounds; never silently rotate, skew, or move the fixed anchor to force a match.
- Publish one temporary snap-bypass modifier after checking existing shortcut conflicts. Make snapping state visible.
- Keep X/Y/W/H values, transform outlines, snap hints, and actual geometry consistent, including nested groups and text.

Acceptance: resize each edge exactly onto every canvas edge at several zoom levels; undo restores the original. Verify corner handles, Shift/Alt combinations, nested objects, multi-selection, negative scales, and locked content. Alignment lands exactly on the chosen reference within numeric tolerance, not merely within a visual pixel.

## Phase 3 — Editing reliability and file confidence

- Audit commit/cancel behavior across numeric edits, sliders, colors, text, node edits, drags, and selection changes. No unrelated edit may inherit another control's unfinished transaction.
- Disable inapplicable controls; explain locked content and mixed selections. Commands must not appear successful when they did nothing.
- Diagnose the UI save/open/export stall. Report unexpected modal dialogs as test failures, provide known test fonts, isolate test data/settings, and impose timeouts. Do not dismiss all dialogs indiscriminately or suppress product errors to obtain a pass.
- Exercise save-over-existing-file, failed saves, close during IO, autosave recovery, missing fonts, unavailable linked assets, and malformed projects on Windows. Failed writes must leave the last good file intact.
- Make save/export progress and completion visible. Errors should name the operation and destination and provide a useful recovery action.
- Make multi-file export outcomes explicit, including partial failure, filename collisions, and overwrite decisions; preserve existing files according to the chosen policy.
- Separate export preview state from saved document state where appropriate; changing preview background or zoom must not mark artwork dirty.

Acceptance: a scripted edit → save → reopen → export round trip preserves the document and output; deliberate filesystem failures produce actionable errors and no data loss. All five contract suites complete reliably on Windows.

## Phase 4 — Responsiveness and everyday workflow

- Establish representative documents: simple icon, text-heavy badge, nested boolean composition, image-heavy design, and dense effects scene. Record hardware, dimensions, zoom, and object counts with measurements.
- Measure drag/scrub latency, render time, memory, cold start, and idle CPU before optimization. Proposed targets on a named reference machine: typical interaction updates under 33 ms at the 95th percentile; large-document input feedback under 100 ms; no persistent idle redraw. Adjust targets from measured evidence.
- Repaint selection/hover/snap overlays without rerendering unchanged artwork. Coalesce rapid preview updates and postpone expensive size-strip refreshes until a short idle interval or gesture completion.
- Add caching only where profiling demonstrates a benefit. If background rendering is needed, use immutable snapshots, request generations, cancellation, and discard stale results; keep Qt font/shaping ownership constraints explicit.
- Improve selection predictability: visible hover targets, clear group entry/exit, reliable marquee selection, consistent locked/hidden-object handling, and clear active text/node modes.
- Add a zoom percentage control, Fit Canvas, Zoom to Selection, and resettable workspace layout. Validate mouse and trackpad navigation before changing existing wheel-to-zoom behavior; make navigation conventions explicit.
- Improve object organization with meaningful names, search, consistent rename shortcuts, and clear drop/reparent previews.

Acceptance: repeated scrubbing and dragging remain responsive on the benchmark scenes, rendered results settle to the latest state, and memory does not grow through repeated edit/undo cycles.

## Phase 5 — Visual consistency, accessibility, release readiness

- Standardize property row heights, spacing, units, precision, focus rings, disabled states, disclosure behavior, and icon tooltips. Prioritize readability over fitting more controls into the panel.
- Preserve Inspector scroll position and disclosure state when selection changes where practical. Avoid moving the user's active control during an edit.
- Review keyboard traversal, accessible names, contrast, text scaling, and usability at 100%, 150%, and 200% Windows scaling, including moving between monitors.
- Add a concise shortcut reference and contextual hints for scrubbing, snapping, transform modifiers, and undo. Avoid startup tutorials that block work.
- Validate the packaged app on a clean Windows account/machine without Qt or developer tools on PATH. Cover Unicode paths, ordinary-user permissions, missing dependencies, and recovery after a forced termination.
- Complete distribution notices, version information, installer/uninstall behavior, and release notes. Update implementation status to separate automated checks from hands-on verification.

Acceptance: a new user can create a badge, add text, align it, save/reopen it, and export PNG/ICO without assistance or accidental edits. No unresolved data-loss defects, critical interaction defects, or unexplained test hangs remain.

## Delivery order and checkpoints

| Increment | Concrete deliverable | Exit gate |
| --- | --- | --- |
| 1 | Shared wheel/focus policy across property controls | Scroll safety and intentional editing regression checks pass |
| 2 | Complete transform/property scrubbing | Preview, Escape, undo, mixed values, and interruption behavior verified |
| 3 | Discoverable canvas alignment and edge resize snapping | Exact bounds and modifier combinations verified |
| 4 | Reliable save/open/export and UI automation | All contract suites finish; failure/recovery scenarios pass |
| 5 | Measured responsiveness and workflow improvements | Published baseline and measured improvement on representative documents |
| 6 | Visual/accessibility pass and clean-machine release candidate | Hands-on workflow, DPI, packaging, and release checks pass |

Keep changes reviewable: shared controls first, then their integration, transform math separately from canvas UI, and performance changes backed by measurements. Add focused regression coverage for interaction defects rather than relying on launch smoke tests.

Do not expand into multiple artboards, arbitrary text-on-path, extra effects, collaboration, or new export formats during these increments. Reassess those features after the core workflow meets the acceptance gates.

Recommended first implementation: increments 1–3. They directly address accidental editing, keyboard dependence, and exact canvas-edge placement while establishing reusable interaction behavior for the rest of the app.
