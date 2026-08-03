// cli/installer/default_apprun_template.hpp
#pragma once

inline const char *kAppRunTemplate = R"SH(#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/flux_app" "$@"
)SH";