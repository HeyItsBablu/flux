if(ANDROID)
    return()
endif()


include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# ============================================================================
# add_flux_example(NAME)
#
# Win32 : WIN32 executable, links against flux::flux, /SUBSYSTEM:WINDOWS
# Linux : console executable, links against flux::flux + SDL2 + Cairo + Pango
# ============================================================================

function(add_flux_example NAME)

    if(WIN32)

        add_executable(${NAME} WIN32
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_win32.cpp
        )
        target_link_options(${NAME} PRIVATE /SUBSYSTEM:WINDOWS)

    elseif(UNIX)

        add_executable(${NAME}
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_linux.cpp
        )

        # SDL2
        find_package(SDL2 REQUIRED)

        # Cairo + Pango via pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(CAIRO      REQUIRED cairo)
        pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)

        target_include_directories(${NAME} PRIVATE
            ${SDL2_INCLUDE_DIRS}
            ${CAIRO_INCLUDE_DIRS}
            ${PANGOCAIRO_INCLUDE_DIRS}
            ${stb_SOURCE_DIR}
        )

        target_link_libraries(${NAME} PRIVATE
            ${SDL2_LIBRARIES}
            ${CAIRO_LIBRARIES}
            ${PANGOCAIRO_LIBRARIES}
        )

        # Link flags from pkg-config (e.g. -pthread, rpath entries)
        target_compile_options(${NAME} PRIVATE
            ${CAIRO_CFLAGS_OTHER}
            ${PANGOCAIRO_CFLAGS_OTHER}
        )

    endif()

    # ── Common ────────────────────────────────────────────────────────────
    target_link_libraries(${NAME} PRIVATE flux::flux)

    # So main.cpp can find app.hpp
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    # Copy screenshots folder next to the built binary
    if(EXISTS "${CMAKE_SOURCE_DIR}/screenshots")
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/screenshots"
                "$<TARGET_FILE_DIR:${NAME}>/screenshots"
            COMMENT "Copying screenshots -> output dir for ${NAME}"
        )
    endif()

endfunction()

# ============================================================================
# Examples
# ============================================================================

add_flux_example(helloworld)

# ── image_editor (needs stb) — uncomment to enable ───────────────────────────
# add_flux_example(image_editor)
# target_include_directories(image_editor PRIVATE ${stb_SOURCE_DIR})
# target_compile_definitions(image_editor PRIVATE
#     STB_IMAGE_IMPLEMENTATION
#     STB_IMAGE_WRITE_IMPLEMENTATION
# )