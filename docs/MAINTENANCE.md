# Kantan maintenance and change policy

Status: adopted maintenance policy, inherited from Kantan NineSlice (13 October 2026) and applied to Kantan LogoMaker from 27 October 2026. Remaining release checks are recorded in [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

The purpose of this policy is to make shortcuts visible and keep each completed slice maintainable. It does not promise that all future design choices will be perfect. Refactoring a clear boundary when evidence changes is normal maintenance; hiding duplicated behavior or an unresolved failure path is avoidable debt.

## Working rules

1. Maintain one source of truth for each contract: [FOUNDATION_PLAN.md](FOUNDATION_PLAN.md) for product behavior, [ARCHITECTURE.md](ARCHITECTURE.md) for engineering, and this file for change checks. Update affected documents with the implementation; do not copy their full contents into comments or additional guides.
2. Implement the smallest coherent vertical slice through the chosen architecture. Prototype experiments live outside production targets, have a recorded decision question, and are deleted or replaced before the next milestone. Do not ship a prototype and promise to extract the framework later.
3. Prefer an existing shared operation over another path. UI event handlers resolve user input and call Editor commands; they do not rasterize, decode files, or modify committed Document fields. Shape, text, image, boolean, transform and style edits and Undo must not grow separate history/save-invalidation implementations.
4. Keep functions focused on one named responsibility. Split a controller that starts owning region detection or raster storage because its responsibilities crossed the boundary, not merely because it passed an arbitrary line-count limit.
5. Add abstractions in response to actual variation. Two nearly identical algorithms should share their invariants; two different algorithms need not be forced into a generic engine with flags for every distinction. Public extension mechanisms wait for a real second consumer.
6. Do not replace correct error reporting with silent fallback. Unsupported input, a locked destination, an invalid file, and a memory budget failure must reach an actionable user result. Successful no-ops do not create undo entries or empty fill layers.
7. Copy behavior and code from Kantan NineSlice selectively (D-L5). Do not import its tile storage, brush, region or slice features. Keep provenance for copied code in PROVENANCE.md.

## Toolchain and dependency discipline

Before the first code milestone, record and pin the C++ toolchain, CMake version, Qt distribution, codec/archive packages, and test dependencies. Use one dependency acquisition route with pinned versions/checksums or locked manifests. Record license/distribution requirements for the components actually used; do not mix package sources casually.

Check in build presets and clean setup instructions. Required paths are explicit and portable; do not rely on the developer's PATH, user profile, hard-coded absolute checkout path, or undocumented globally installed packages. A clean clone must configure and build using those instructions. Build output, temporary fixtures, caches, generated files, and recovery files stay outside source-controlled inputs.

The initial verification pipeline has these stages:

- Core-only configure/build/test with Qt disabled, including the include-policy check.
- IO contract tests and invalid/recovery fixture tests when IO exists.
- Windows app compilation/package smoke check when the app exists.
- Formatting and agreed compiler warnings treated as errors for owned production code; third-party warnings remain isolated.
- Sanitizer or equivalent memory diagnostics on supported headless configurations, scoped to owned code. Do not pretend a toolchain supports a diagnostic mode it lacks.
- Repeatable release benchmark runner with machine/fixture metadata and a checked-in baseline report. Timing enforcement uses a stable machine or manual same-machine comparison, rather than failing arbitrary hosted CI on noisy wall-clock thresholds.

CI setup and local commands grow with the actual implementation. Do not create empty tests or dummy passing jobs just to tick these boxes. Coverage percentage is a diagnostic, not the definition of quality.

## Definition of done for an implementation change

| Check | Required evidence |
| --- | --- |
| Behavior | The relevant acceptance case works; the UI and command have the same target/selection semantics |
| Boundary | No new forbidden include/link or mutable-state ownership violation; core-only build stays independent |
| Artwork safety | Apply, cancel, undo/redo, and failure paths preserve coherent pixels/structure; relevant regressions covered |
| Async state | Inputs are frozen, stale results cannot publish, jobs/resources are cleaned up |
| Persistence | Affected saved data round-trips; schema changes include explicit migration/unsupported-version behavior and old fixtures |
| Memory/performance | No unbounded allocations/queues/caches; relevant release benchmark compared when the drawing path or storage changed |
| Reproducibility | Dependencies and build instructions remain sufficient for a clean setup |
| Handoff | Brief outcome, validation, and remaining limitations; affected contracts/decision records updated |

Run checks proportionate to the change. A documentation-only or cosmetic adjustment needs document/layout verification, not a new pixel test suite. Data mutations, file IO, coordinate math, and concurrency require targeted meaningful tests. Broaden checks after a failure or a new concern; do not repeat an unchanged full suite without reason.

For any new editing feature, walk through: no selection, empty selection, active mask, locked/hidden/empty target, multiple document instances, cancellation, undo/redo, and save/reopen where data changes. These are case categories, not a requirement to duplicate the same contract test for every widget.

## Technical-debt exceptions

Default: resolve a discovered shortcut within its current feature slice. A missing dependency boundary, second mutation path, shared global document state, unsafe save, or partial multi-layer commit cannot be waved through with a TODO. These are release-blocking contract violations.

If a temporary compromise is necessary, record it in `docs/DEBT_REGISTER.md` before leaving it in a production target. Each entry must contain:

- ID and concrete behavior/contract being compromised.
- Evidence that prompted the compromise and why the normal path cannot be completed in the slice.
- User impact and containment; affected source locations and coverage.
- Responsible feature/milestone and removal trigger.
- Acceptance checks proving the fix, including a measurable budget where relevant.
- Current disposition: open, resolved, or superseded by a dated architecture decision.

A bare TODO, “fix later,” arbitrary deadline, or disabled test does not satisfy this policy. Temporary entries block the next milestone unless their containment and removal are explicitly resolved. Before first release, close them or promote an intentional permanent tradeoff through a recorded decision with honest documented behavior. Do not relabel a data-loss or ownership failure as a permanent tradeoff.

No production shortcuts are waived by a passing test suite. Incomplete recovery, disk backing and tablet/performance acceptance remain explicit release gates in the implementation status; they must not be advertised as completed features. Intentional history representation is recorded in decision D8. Create the debt register when an actual temporary exception is introduced; do not hide it in an unrelated future feature list.

## Evolving the framework

When a second tool reuses the core, first integrate through the existing Editor, snapshot, and raster APIs. Add only the boundary required by that integration. Animation owns timing/exposures outside the drawing model. A native file importer owns its schema mapping outside the core. A presenter owns hardware handles outside the compositor.

Preserve old format and brush fixtures when updating schemas. Remove obsolete migration paths only with an explicit compatibility policy; do not make old artwork unreadable as incidental cleanup. Remove replaced implementations, feature flags, dead widgets, and unused dependencies in the same change that completes the replacement, unless a tracked compatibility reason requires them.

The framework is ready for reuse when a small headless consumer can create a document, paint a stroke, run Enclose to obtain coverage, move selected pixels across surfaces, undo, and export through IO without booting the Kantan UI or relying on its global state. This is the final handoff demonstration, while core independence remains checked from the beginning.
