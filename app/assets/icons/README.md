# Tool and interface icons

Official Microsoft Fluent System Icons, MIT licensed, pinned at commit
`a347af95478f989038cccbf085978865264c048a` of
https://github.com/microsoft/fluentui-system-icons.

Original regular 24px SVG files are retained without modification. The PNG variants use #e1e6ee for the dark
interface and are rendered at 24, 48 and 72px. They are embedded locally; builds and the app need no network or SVG
plugin. Source checksums are in source-hashes.json. License and upstream NOTICE are in third_party and included in
packaged notices.

| Icon | Upstream asset |
| --- | --- |
| select | Cursor |
| direct_select | Select Object |
| pen | Pen |
| shapes | Shapes |
| ellipse | Circle |
| text | Text T |
| image | Image |
| picker | Eyedropper |
| hand | Hand Left |
| zoom | Zoom In |
| shape_union / subtract / intersect / exclude | Shape Union / Subtract / Intersect / Exclude |
| align_left / right / top / bottom | Align Left / Right / Top / Bottom |
| align_center_horizontal / vertical | Align Center Horizontal / Vertical |
| distribute_horizontal / vertical | Align Space Evenly Horizontal / Vertical |
| flip_horizontal / vertical | Flip Horizontal / Vertical |
| font | Text Font |
| export | Arrow Export LTR |
| group | Group |
| rotate | Arrow Rotate Clockwise |
| repeat | Arrow Repeat All |
| sparkle | Sparkle |
| star | Star |

Also reused from Kantan NineSlice (same commit): add, arrow_fit, arrow_reset, checkmark, chevron_down, chevron_right,
circle_half_fill, color, copy, delete, dismiss, eye, folder_add, grid, link, lock_closed, lock_open, move, picker,
rectangle, resize, settings.

To regenerate PNGs, run scripts/render-icons.cjs (see docs/BUILD.md). This is an optional asset maintenance step,
not a build dependency.
