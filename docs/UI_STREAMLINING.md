# UI streamlining

The Select options bar now exposes three action/help buttons instead of twelve action buttons plus a long
instruction label. Arrange contains alignment, distribution, and stacking order; Combine reuses the existing
Union/Subtract/Intersect/Exclude actions and their shortcuts. The alignment reference selector remains visible.
The six frequent canvas-alignment buttons remain directly beside Transform.

Tool instructions have moved from permanent toolbar labels into a small help button with a tooltip, accessible
description, and clickable help popup. Instructions remain available without forcing wide toolbar layouts.

Layers keeps direct up/down ordering controls. Group, Duplicate, and Delete are consolidated into one labelled
Actions menu. Context menus and keyboard commands remain available. Empty, single, multiple, locked, and busy
selections control action availability. Layer selection updates the action state and count without scheduling
an artwork or size-strip render.

Regression coverage checks the compact control count, shared boolean actions, layer-menu contents, and selection
availability. Existing editing/transform/layer/IO tests continue to exercise the preserved commands. Verification
artifacts are out/streamline-build.log, out/streamline-tests.log, and out/streamline-package-validation/.

This increment focuses on UI density. The render scheduling and broader reliability items in the professional
quality roadmap remain tracked separately.
