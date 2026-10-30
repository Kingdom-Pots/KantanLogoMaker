# Layer drag, cursor, and default-colour fixes

30 October 2026.

## Why layers appeared to vanish

The custom ObjectTree drop handler reordered the document and synchronously rebuilt the tree. It accepted a
native MoveAction without calling the QTreeWidget drop handler. Qt's source-drag completion then had no record
that the internal move was handled and could remove the selected rows from the rebuilt view. The unchanged-
document refresh optimization also meant those missing rows could remain missing from the panel.

This cleanup behavior is present in the pinned Qt 6.8.3 sources:
[QAbstractItemView::startDrag / clearOrRemove](https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/widgets/itemviews/qabstractitemview.cpp)
and [QTreeWidget::dropEvent move bookkeeping](https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/widgets/itemviews/qtreewidget.cpp).
The application also allowed dropping into boolean/clip containers, which can change an object's visible result
through compositing when a user only intended to change stacking order.

## Layer changes

- ObjectTree owns QDrag directly; it never delegates source-row deletion to QAbstractItemView.
- A drag captures object IDs before entering the native drag loop. Drop handling never passes tree-item pointers
  through a document/tree rebuild or reads a potentially changed selection to identify the dragged objects.
- External drags are rejected. Stale IDs, locked destinations, hierarchy cycles, and filtered-tree reordering are
  rejected without changing the document.
- Lines show insertion above/below a layer; a group highlight shows nesting. Only ordinary groups accept a centre
  drop. Boolean/clip containers use ordinary insertion lines rather than implicitly adding operands.
- Dropping into blank space moves layers to the bottom of the root stack. Multiple layers retain their relative
  order. Reparenting uses the editor's existing world-transform preservation and undo support.
- The panel is called Layers, shows total/selected counts, and includes move-up/down buttons and a drag preview.
  Dragging near the panel edge scrolls the list.
- Visibility and lock clicks do not replace the current selection or start a drag. Invalid renames restore the
  document's canonical name. Missing view rows force a full rebuild even if the document snapshot is unchanged.

## Canvas and colour changes

- Resize handles use horizontal, vertical, or diagonal resize arrows. Their directions follow the selected
  object's frame, including rotated frames. Rotation uses a curved-arrow cursor. Drag completion/cancellation
  resets the cursor.
- New shapes use dark desaturated blue **#405164** (RGB 64, 81, 100). The core default paint and application shape
  defaults share the same constant. Saved artwork and explicitly chosen styles retain their own colours.

## Regression checks

UI regressions exercise single/multiple layer insertion, root drops, undo/redo, stable drag identity, restoration
after view-only row removal, transformed-group nesting, cycle/lock rejection, visibility clicks preserving
selection, handle cursor orientation, and actual shape-tool output using the muted blue.

These exercise the controller/view contracts and reproduce the former view/document mismatch. They do not
simulate an operating-system mouse drag from end to end; the native cleanup integration was checked against
the pinned Qt implementation. Full contract results are in out/layers-tests.log and the UI JUnit report.
