# Local development package notices

Qt 6.8.3 Core, Gui, Widgets, Concurrent and the deployed platform/image plugins are dynamically linked, unmodified development binaries acquired with aqtinstall 3.3.0 from the Qt distribution. Library sources for the exact version are available from [Qt's qtbase v6.8.3](https://github.com/qt/qtbase/tree/v6.8.3). Qt's LGPL and GPL license texts are included beside this notice. Qt's bundled dependency metadata is retained in the local package's `qt-sbom` directory.

Clipper2 1.5.4 (Boost Software License 1.0) and nanosvg (zlib licence, commit `239e102`) are vendored as unmodified source in `third_party/` and compiled into the application. Miniz 3.1.0 is used in the IO library under its MIT license. Their full notices accompany the local package; versions, commits and checksums are recorded in `dependencies.json`.

Microsoft Fluent System Icons are used under the MIT license (see the Fluent notices).

This package is for local development and testing. The application's distribution license has not been selected. Before public distribution, finish the release checks in `docs/IMPLEMENTATION_STATUS.md` and the Qt source/relink/compliance packaging appropriate to that license.
