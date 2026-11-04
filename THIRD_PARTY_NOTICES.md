# Third-party components

Desktop Tasks code: MIT, see LICENSE. No affiliation with Google, KDE or Microsoft.

- **Qt 6 Core, Gui, Widgets, Network** and deployment plugins: dynamically linked, LGPL-3.0 (or GPL/commercial alternatives). Windows build uses 6.8.3, Linux uses distribution packages. Copyright The Qt Company and contributors. Licenses: https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSES?h=v6.8.3 ; source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz . Other deployed Qt plugin sources are in the same version’s submodules directory (notably qtimageformats, qtsvg, qttranslations if deployed).
- **QtKeychain 0.15.0** (BSD-3-Clause): https://github.com/frankosterfeld/qtkeychain/tree/ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7 ; Windows source archive is attached to the release. Linux uses its packaged version.
- **layer-shell-qt** (Linux Wayland only): LGPL-3.0 / LGPL-2.1 alternatives as specified in upstream files. https://invent.kde.org/plasma/layer-shell-qt . Provided by distribution, not bundled.
- **libxcb** (Linux X11 only): MIT-style license, https://xcb.freedesktop.org/ . Provided by distribution.
- **Microsoft Visual C++ runtime** (Windows): Microsoft redistributable license. The deployment may include its official redistributable installer. See https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist . Not part of the MIT license.

## Your rights with the binary bundles

Qt and QtKeychain are shared libraries, not statically embedded in the application. You may replace them with compatible modified builds. Reverse engineering for debugging your modifications to LGPL libraries is not prohibited by this project. No DRM or signature check prevents replacement. Installation needs no signing key. Keep the Qt plugins directory and a compatible Qt build together.

Rebuild Qt with its upstream configure/CMake instructions, then build this project using `-DCMAKE_PREFIX_PATH=path/to/your/Qt`. Rebuild QtKeychain with `-DBUILD_WITH_QT6=ON -DBUILD_TRANSLATIONS=OFF`. Replace the corresponding DLLs/plugins or point your local build to the replacements. Distribution Linux libraries can be rebuilt with `apt source qt6-base qtkeychain layer-shell-qt` after enabling deb-src. Corresponding Windows Qt/QtKeychain source archives and license texts accompany the release, alongside application source and checksums. The source packages include upstream licensing notices for bundled third-party code. Those individual licenses continue to apply.
