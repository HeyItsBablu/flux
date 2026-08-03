#pragma once

// Default DEBIAN/control file. Overridden if the project has its own file
// at installer/linux/control — same "convention with override" pattern as
// app.iss and app.desktop.
//
// This is the DYNAMIC-LINKED variant: Depends: lists the system packages
// providing the libs flux links against on native Linux (see the
// pkg_check_modules calls in the top-level CMakeLists.txt). The .deb does
// NOT bundle these .so files itself — apt resolves and installs them.
// If a target machine's package versions are too far from what this build
// linked against, that's a real compatibility gap AppImage doesn't have
// (see the note at the end of this change).
inline const char *kDefaultControlTemplate = R"CONTROL(Package: @@APP_PACKAGE_NAME@@
Version: @@APP_VERSION@@
Section: utils
Priority: optional
Architecture: amd64
Depends: libsdl2-2.0-0, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, libasound2t64 | libasound2, libavformat60 | libavformat59 | libavformat58, libavcodec60 | libavcodec59 | libavcodec58, libavutil58 | libavutil57 | libavutil56, libswscale7 | libswscale6 | libswscale5, libjpeg62-turbo | libjpeg-turbo8
Maintainer: @@APP_PUBLISHER@@
Description: @@APP_NAME@@
)CONTROL";