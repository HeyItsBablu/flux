add_executable(flux_cli
    flux_cli.cpp
)

if(WIN32)
    target_sources(flux_cli PRIVATE platform/build_windows.cpp)
elseif(APPLE)
    target_sources(flux_cli PRIVATE platform/build_macos.cpp)
elseif(UNIX)
    target_sources(flux_cli PRIVATE platform/build_linux.cpp)
endif()

set_target_properties(flux_cli PROPERTIES OUTPUT_NAME "flux")
target_include_directories(flux_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})