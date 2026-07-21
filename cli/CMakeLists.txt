add_executable(flux_template_embed tools/embed_template.cpp)

set(TEMPLATE_BIN ${CMAKE_CURRENT_BINARY_DIR}/generated/flux_template.bin)

add_custom_command(
    OUTPUT  ${TEMPLATE_BIN}
    COMMAND flux_template_embed ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${TEMPLATE_BIN}
    DEPENDS flux_template_embed
    COMMENT "Building embedded flux template archive"
)

add_custom_target(flux_template_archive DEPENDS ${TEMPLATE_BIN})

if(WIN32)
    # MSVC has no inline-asm path for embedding binary data on x64, so we
    # embed via a Win32 resource (RCDATA) instead of incbin's .incbin
    # mechanism. The .rc just needs the output *path*; rc.exe reads the
    # file's actual bytes at compile time, after flux_template_embed has
    # produced it (enforced by OBJECT_DEPENDS below).
    set(TEMPLATE_RC ${CMAKE_CURRENT_BINARY_DIR}/generated/flux_template.rc)
    file(WRITE ${TEMPLATE_RC} "101 RCDATA \"${TEMPLATE_BIN}\"\n")
    set_source_files_properties(${TEMPLATE_RC} PROPERTIES OBJECT_DEPENDS ${TEMPLATE_BIN})
endif()


add_executable(flux_cli
    flux_cli.cpp
    package_manager.cpp
    create.cpp
    util.cpp
    
)

if(WIN32)
    target_sources(flux_cli PRIVATE template_archive_win.cpp ${TEMPLATE_RC})
else()
    target_sources(flux_cli PRIVATE template_archive.cpp)
endif()
add_dependencies(flux_cli flux_template_archive)

if(NOT WIN32)
    target_compile_definitions(flux_cli PRIVATE FLUX_TEMPLATE_BIN_PATH="${TEMPLATE_BIN}")
endif()

target_include_directories(flux_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(WIN32)
    target_sources(flux_cli PRIVATE platform/build_windows.cpp)
elseif(APPLE)
    target_sources(flux_cli PRIVATE platform/build_macos.cpp)
elseif(UNIX)
    target_sources(flux_cli PRIVATE platform/build_linux.cpp)
endif()

set_target_properties(flux_cli PROPERTIES OUTPUT_NAME "flux")