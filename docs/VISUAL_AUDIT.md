# Visual consistency audit

Concrete issues addressed in this pass:

| Finding | Change |
| --- | --- |
| Native controls and scrollbars mixed poorly with stylesheet-only dark colours | Install a shared Fusion style and dark application palette; style slim scrollbars explicitly |
| Open Inspector sections looked like large selected buttons | Neutral, flat section headings with subtle dividers and hover/focus feedback |
| A standalone question mark looked like a temporary help control | Multiscale information icon, preserving tooltip, accessible description, and clickable help |
| Hidden layers used an X, which suggests delete/close | Slashed-eye icon derived from the existing eye icon |
| Preview background control was labelled “Bg” | Background swatch icon reflecting the selected checkerboard/white/grey/dark mode |
| Disabled controls, menu spacing, and tooltip appearance were inconsistent | Muted disabled states, consistent menu padding/separators, dark readable tooltips, button focus borders |
| Light tab borders and dotted dock separators distracted from the dark panels | Muted frame colours and simple dividers with hover feedback |
| Startup `--select` could show a selection while toolbar state remained stale | Refresh the complete selection UI through the normal canvas-change signal |

The existing Fluent icon family remains the primary icon source. The new information and hidden-eye icons are
drawn at 1x/2x/3x to match the application's existing multiscale assets; the implementation does not substitute
generic fallback icons when a resource is missing.

Resource paths in app/icons.qrc were checked against disk: no missing files. A UI regression loads every bundled
PNG icon plus the generated information/hidden-eye icons at 20/40/60 pixels and checks for nonempty visible output.
That check establishes resource validity, not a subjective design certification.

Verification outputs: out/polish-build.log, out/polish-tests.log, and out/polish-package-validation/. Interactive
editing, layers, undo, file round trips, and export remain covered by the existing contract suites.

The audit is a concrete desktop UI consistency pass. It does not certify every dialog, physical monitor
transition, screen-reader workflow, or distribution branding item in the broader roadmap.
