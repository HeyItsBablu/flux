# config/AppConfig.cmake
file(READ "${CMAKE_CURRENT_LIST_DIR}/AppConfig.json" FLUX_CONFIG_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/AppConfig.json"
)

string(JSON FLUX_APP_NAME      GET ${FLUX_CONFIG_JSON} name)
string(JSON FLUX_APP_BUNDLE_ID GET ${FLUX_CONFIG_JSON} bundleId)
string(JSON FLUX_APP_VERSION   GET ${FLUX_CONFIG_JSON} version)
string(JSON FLUX_APP_BUILD     GET ${FLUX_CONFIG_JSON} build)


string(JSON FLUX_APP_WINDOW_WIDTH  GET ${FLUX_CONFIG_JSON} window width)
string(JSON FLUX_APP_WINDOW_HEIGHT GET ${FLUX_CONFIG_JSON} window height)
string(JSON FLUX_APP_FULLSCREEN    GET ${FLUX_CONFIG_JSON} window fullscreen)
string(JSON FLUX_APP_MAXIMIZE      GET ${FLUX_CONFIG_JSON} window maximize)

if(FLUX_APP_FULLSCREEN)
    set(FLUX_APP_FULLSCREEN_BOOL 1)
else()
    set(FLUX_APP_FULLSCREEN_BOOL 0)
endif()

if(FLUX_APP_MAXIMIZE)
    set(FLUX_APP_MAXIMIZE_BOOL 1)
else()
    set(FLUX_APP_MAXIMIZE_BOOL 0)
endif()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/AppConfig.h.in"
    "${CMAKE_BINARY_DIR}/generated/AppConfig.generated.h"
    @ONLY
)