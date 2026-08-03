// cli/installer/default_desktop_template.hpp
#pragma once

inline const char *kDefaultDesktopTemplate = R"DESKTOP(
[Desktop Entry]
Type=Application
Name=@@APP_NAME@@
Exec=flux_app
Icon=@@APP_ICON_NAME@@
Categories=Utility;
Terminal=false
)DESKTOP";