# Kantan LogoMaker

A small Windows tool for logos and app/game icons. Build a logo from shapes, text and images on one artboard, then
export PNG and ICO files at every size you need in one step.

## Start

Launch `KantanLogoMaker.exe`. The **start screen** offers blank artboards in common sizes (square logo, app icon,
wordmark, Steam capsule, itch.io cover, social banner, avatar, or any custom size), the starter templates and your
recent logos. Untick *Show this at startup* to go straight to a blank 512 × 512 artboard; File › Start screen… brings
it back. **File › Open recent** lists the last ten logos you opened or saved.

Templates (also in **File › New from template** and the **Presets › Starter** tab): round badge, shield with
initials, app icon squircle, wordmark, star emblem, burst label, esports crest, monogram, retro sunset and a Steam
capsule that opens with the Steam export preset chosen. The newer ones come with a matching colour palette.

The window has the tool rail on the left, tool options along the top, the artboard in the middle, **Layers**,
**Inspector** and **Presets** on the right and the **Size preview** strip at the bottom. Docks can be moved, closed and
restored from the View menu; the layout is remembered. The size strip shows 16–64 px icons at their real pixel size;
**1× / 2× / 3×** enlarges them with hard pixels so you can inspect single pixels, and **Bg** cycles the background.

Right-click an object on the canvas or in Layers for the commands that apply to it (edit text, duplicate, group,
combine, clip, arrange, flip, copy/paste style, lock, hide); right-click empty canvas to paste, select all, add guides
or change the artboard.

In **Layers**, the top row is frontmost. Drag between rows to reorder; an insertion line shows the destination.
Drop in the centre of a normal group to move layers into it (the group highlights). Boolean/clip layers do not
accept implicit nesting drops. Drop in blank space below the rows to move to the bottom of the root stack.
Multiple dragged layers keep their relative order; Undo reverses the operation. Move-up/down buttons provide an
alternative to dragging. Eye and lock icons work without changing the selection.

The Layers **Actions** menu contains Group, Duplicate, and Delete. These commands also remain in the main menus,
context menu, and keyboard shortcuts. The Select tool's top bar uses **Arrange** and **Combine** menus instead of
separate buttons for every operation. The six frequent Canvas alignment buttons remain beside Transform.
Use the small **information** button in a tool's options for its instructions, or hover it for a quick reminder.

New shapes start with a dark desaturated blue fill (#405164). Choose another fill or style when needed; existing
saved colours stay unchanged. Transform handles show directional resize arrows, and the rotation handle shows
a curved arrow.

## Tools

| Tool | Key | Use |
| --- | --- | --- |
| Select | V | Click to select, Shift-click to add, drag empty space to box-select. Drag to move, handles to resize, the round handle to rotate. Shift: uniform / 15° steps · Alt: from the centre · Ctrl-click: select inside a group. Double-click text to edit it, a path to edit its nodes. |
| Direct select | A | Edit path nodes: drag nodes and handles, Alt-click a segment to add a node, Delete removes the selected node, double-click toggles corner/smooth. Parametric shapes and text need Path › Convert to path first. |
| Pen | P | Click for corners, drag for curves, click the first point to close. Enter or double-click finishes an open line (drawn as a round-capped stroke). Backspace removes the last point, Esc cancels. |
| Shape | R | Drag to draw the chosen preset (pick it in the top bar or hold the rail button). Shift keeps equal sides, Alt draws from the centre; a click places the default size. |
| Ellipse | E | Ellipses and circles (Shift). |
| Text | T | Click and type straight on the canvas. Enter finishes (one undo step), Shift+Enter adds a line, Esc cancels. Double-click existing text with the Select tool to edit it the same way. |
| Place image | I | Click where an image should go. |
| Eyedropper | K, or hold Alt | Click the artwork to apply that colour to the selection's fill or first outline, or to new shapes (choose in the top bar). |
| Hand | H, or hold Space | Pan. The middle mouse button pans with any tool. |
| Zoom | Z | Click to zoom in, Alt-click to zoom out. The mouse wheel zooms at the pointer; Ctrl+0 fits, Ctrl+1 is 100 %. |

Alt-drag an object to drag off a copy. **Edit › Duplicate** (Ctrl+D) repeats the last spacing: duplicate, move the
copy, then keep pressing Ctrl+D to lay out evenly spaced copies.

Arrow keys nudge the selection by 1 unit (Shift: 10). Moving snaps to whole units, the artboard edges and centre,
other objects and guides; pink lines show the active snap. Turn targets on and off in View › Snapping.

## Shapes and presets

Parametric shapes stay editable: corner radius (linked or per corner, round/chamfer/inverted), polygon sides, star
points and inner radius, ring thickness and arc angles, gear teeth, and one to three settings for shield, badge,
ribbon, speech bubble, arrow, heart and burst. Resizing changes the shape's size, so corners keep their radius.

**Presets › Shapes** places parametric shapes, SVG symbols and your saved presets (Object › Save selection as
preset…). **File › Import SVG…** brings paths, fills and strokes in as editable shapes; text, embedded images,
filters, masks and clip paths in the SVG are skipped and listed in the status bar.

## Style

The Inspector applies style edits to every selected object. When the selected objects differ, the field shows the
difference (grey italic text, a part-ticked box, or a split swatch) and typing sets them all. Drag a field's **label**
left or right to scrub its value (Shift ×10, Ctrl fine steps). Position, size and angle sit in one compact
Transform section.

Scrolling over an inactive property scrolls the panel without changing the value. Click or Tab into a field
before using the wheel to adjust it; the focused field has a highlighted border. Inspector wheel adjustments
made together form one undo step. Clicking elsewhere ends property focus.

The Transform section comes first. Drag **X**, **Y**, **W**, **H**, or **Angle** to adjust that value directly.
Drag other numeric property names, including slider labels, the same way. **Escape** cancels a scrub;
releasing commits it as one undo step. Relative scrubbing preserves differences across a selection where the
property supports multiple objects; typing applies an absolute value.

Right-click a numeric property name for its reset action; the menu shows the value it will apply. A reset is
undoable and changes only that property. Scrubbing a single value against a limit no longer accumulates excess
pointer movement: dragging back starts adjusting immediately.

The **Canvas** buttons under Transform align the combined selection to the canvas while preserving object
spacing. The top bar also offers **Selection**, **Canvas (as arrangement)**, and **Canvas (each object)** targets.
Alignment uses geometric bounds, so outlines and shadows may extend beyond the aligned edge.

Dragging an unrotated transform edge snaps it to canvas edges/centres, guides, and other objects. Pink lines
show the target. Hold **Ctrl** during a move or resize to bypass snapping; Shift retains aspect ratio for corner
resizing, and Alt resizes around the centre. Rotated frames currently resize freely without edge snapping.

**View › Zoom to selection** (Ctrl+2) frames selected objects; **View › Reset workspace layout** restores the
default docks. The Objects panel search filters names and object types, keeping matching ancestors visible.
Clear the search before reordering objects; filtering never edits the document.

**Colours.** Every colour field has a swatch (opens the colour picker, with a live preview on the canvas; Cancel puts
the old colour back), a hex box (`RRGGBB`, `RRGGBBAA` or `RGB`) and a row of quick swatches: the document palette,
colours already used in the logo and your recent colours. Alt-click the artwork with the Select tool to pick a colour.

**Presets › Palette** is the document's named colour palette, saved with the project. Click a colour to apply it to
the fill, Shift-click for the first outline. Double-click (or *Change colour everywhere*) replaces that colour in
every unlocked object in one undo step, keeping each use's transparency. Add the selection's colour, every colour in
the document, rename or remove entries from the buttons and right-click menu.

- **Fill**: none, solid, linear or radial gradient with an optional middle stop. With a gradient selected, drag the
  yellow handles on the canvas to change its angle, or a radial gradient's centre and radius.
- **Outlines**: up to three. *Outside* outlines stack outward behind the fill (a white outline then a dark one gives
  the classic sticker look), *Inside* outlines stack inward on top, *Centre* outlines straddle the edge.
- **Effects**: drop shadow, outer glow, inner shadow, inner glow and bevel & emboss (inner, outer, emboss, pillow;
  linear, round or cove profile; light angle and altitude). Sizes are in artboard units, so every export size looks
  the same.
- **Appearance**: opacity, blend mode, *Hide below / Hide above* (hide fine detail such as a tagline at 16 or 32 px),
  and radial copies around a pivot (Object › Radial repeat… centres the pivot on the artboard).

The search box at the top of **Presets** filters shapes, styles and templates by name.

**Presets › Styles** and the Effects menu apply Gold bevel, Chrome, Sticker, Neon glow, Flat shadow or Embossed stone
in one click; Effects › Copy style / Paste style transfers a look between objects.

## Text and fonts

Type in the font box to search (it matches anywhere in the name, and each name is drawn in its own font). Pick
installed fonts, fonts you imported (File › Import font… or drop a .ttf/.otf/.ttc on the window) or fonts
already in the project. Imported fonts are embedded in the project so it opens on other computers; Text › Project
fonts… lets you link a font by name instead when its licence does not allow sharing. If a linked font is missing on
open, a warning lists it and a fallback font is used until it is installed.

Text supports weight, italic, all caps, tracking, line height, alignment, several lines and **Arc bend** for badge
text. Path › Convert to path turns text into editable outlines.

## Combining and editing paths

Select two or more objects and use **Path › Union / Subtract / Intersect / Exclude** (or the top bar). The result is a
live boolean group: move or edit the parts and it updates. *Subtract* removes everything above the bottom object.
**Flatten boolean** bakes it into one path. Object › **Clip to bottom shape** masks the other selected objects (for
example an image) by the bottom shape.

Path › Offset path grows or shrinks a shape, Outline stroke turns an outline into its own filled shape, Simplify
reduces points, and Reverse direction, Break at selected node and Close open paths edit explicit paths.

## Images

Place PNG, JPEG, BMP, GIF (first frame) or ICO files, or paste an image. The original is stored once and never
resampled destructively: crop, brightness, contrast, saturation, hue, tint and **Remove colour** (for logos
delivered on white) are adjustable at any time. Outlines, shadows, glows and bevels follow the image's transparency.

## Checking how it will look

**View › Safe zone** dims what a platform crops away and outlines the visible area: a circle (avatars, round
icons), the rounded-square app icon mask (iOS/macOS), or Android adaptive icons (the 72 dp visible square and the 66 dp
keyline circle on a 108 dp canvas). It is only a guide and never exported.

**View › Preview on…** (Ctrl+Alt+P) shows the logo at real pixel sizes in a taskbar, on a desktop, in a browser tab,
as a round avatar, on a phone home screen and in a store listing, on a dark or light theme. It uses the export
padding and background.

## Export

**File › Export…** (Ctrl+Shift+E): tick PNG and/or ICO, the sizes (custom W × H sizes too), padding, how non-square
outputs fit, background, and whether to export the whole artboard or only the selection. File names follow the
pattern (`{name}_{size}` by default); the ICO holds all its ticked sizes (Windows uses 16, 32, 48 and 256). ICO
entries are limited to 256 × 256 by the format. The preview row shows every output at 1:1 before anything is written.
Each size is rendered separately from the vectors, so small icons stay crisp. Settings are saved with the project.

**Preset** at the top of the dialog sets everything for a target in one click:

| Preset | Writes |
| --- | --- |
| Windows app / GameMaker icon | One .ico with 16, 24, 32, 48, 64 and 256 px, plus a 256 px PNG |
| Website favicon | favicon.ico (16/32/48), favicon-16x16, favicon-32x32, apple-touch-icon (180), android-chrome 192 and 512 |
| Android launcher | `mipmap-mdpi` … `mipmap-xxxhdpi/ic_launcher.png` (48–192) and a 512 px Play Store icon |
| iOS app icon | AppIcon-1024.png, opaque |
| Steam store and library | The Steamworks capsules, library assets and community icon, in a `steam` folder |
| itch.io | Cover image 630 × 500 and 315 × 250 in an `itch` folder |
| Social avatars | 400 and 800 px squares |

Presets keep your padding, fit and folder. Wide outputs such as Steam capsules follow *Non-square fit*; use *Cover*
with a wide artboard (the Steam capsule template does). Pick *Custom* to go back to your own sizes.

**File › Export again** (Ctrl+E) repeats the last export with the same settings, folder and name, without the dialog.
If there is no saved folder yet it opens the dialog instead.

## Saving

Projects are `.kantanlogo` files and include embedded images and fonts. Unsaved work is autosaved every minute; if
the app does not close normally, the next start offers to restore it.
