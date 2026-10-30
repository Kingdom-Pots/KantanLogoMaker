# Building Kantan LogoMaker

Windows x64, C++20, Visual Studio 2022, MSVC 19.38.33141 (toolset 14.38.33130), CMake 3.25 or newer, the same
toolchain as Kantan NineSlice. Install the VS Desktop development with C++ workload and the pinned v143 toolset.
Put CMake on PATH. Python 3.12 is used only to acquire dependencies.

**Quickest way:** double-click `run.bat` in the project folder. It builds only what changed (the first build takes a
few minutes), packages the runtime dependencies and starts the app. CMake uses Qt from `.deps`, or from
`..\KantanNineSlice\.deps` when this project has none, and the launcher finds the CMake that ships with Visual
Studio. Explicit `Qt6_DIR` or `CMAKE_PREFIX_PATH` settings take precedence. Packaging uses the Qt installation
recorded in the desktop build cache. If the build fails, the details are in `out\run-build.log`.

The full sequence, from the checkout in PowerShell:

```powershell
.\scripts\setup.ps1 -Python python
.\scripts\build.ps1
.\scripts\format.ps1 -Check
.\scripts\package.ps1
.\scripts\run.ps1
```

Run `scripts/validate-package.ps1 -Screenshots` after packaging to check native startup at 100%, 150%,
and 200% scaling with Qt removed from PATH. The script writes readiness measurements and screenshots to
`out/package-validation`. This is a local smoke check; a clean-machine and physical mixed-monitor pass remain
separate release checks. Packaging includes the MSVC runtime libraries selected by CMake.

Windows IO/UI tests use a temporary directory under their build folder. UI tests use the checked-in test font,
fail on unexpected message dialogs, and have a 90-second timeout. Detailed UI results are written to
`out/desktop-msvc1938/ui-contracts.xml`.

`setup.ps1` acquires Qt 6.8.3 qtbase with the locked aqt installer and Miniz 3.1.0 with its SHA-256 check (the
same as NineSlice; you can copy NineSlice's `.deps` folder instead to skip the download). Clipper2 and nanosvg are
vendored, unmodified source under `third_party/` with per-file checksums in `dependencies.json`, so they need no
download. `build.ps1` configures, builds and runs every contract check (`core_contracts`, `render_contracts`,
`architecture`, `io_contracts`, `ui_contracts`). Owned code builds with `/W4 /WX`; vendored headers are included
with angle brackets and treated as external.

The core builds without Qt or downloaded dependencies:

```powershell
.\scripts\build.ps1 -CoreOnly
.\scripts\build.ps1 -Sanitize
```

The desktop preset produces `out/desktop-msvc1938/Release/KantanLogoMaker.exe`. Packaging installs it in
`dist/KantanLogoMaker`, deploys Qt, and copies notices (Qt, Miniz, Clipper2, nanosvg, Fluent icons) and the user
guide. `scripts/benchmark.ps1` records render timings, launch time, memory and idle CPU in `out/measurements`.

## Linux validation build

The Qt-free core and the full app also build on Linux for headless checks (this is how the code was validated
before the first Windows build). With GCC 13 or Clang 18, Ninja and a distro Qt 6.4+:

```sh
cmake --preset linux-core && cmake --build --preset linux-core && ctest --preset linux-core
cmake --preset linux-desktop && cmake --build --preset linux-desktop && ctest --preset linux-desktop
```

The Linux desktop preset relaxes the exact Qt 6.8.3 requirement; Windows presets never do. GCC/Clang builds use
`-Wconversion` so narrowing is caught as MSVC `/W4` would.

## Icons

Interface icons are official Fluent System Icons at the pinned commit; PNGs at 1x/2x/3x are checked in. To
regenerate them, run `node scripts/render-icons.cjs` with Playwright available (`KANTAN_NODE_MODULES`) and
optionally `KANTAN_CHROME` pointing at Chrome. The script also refreshes `app/assets/icons/source-hashes.json`.
