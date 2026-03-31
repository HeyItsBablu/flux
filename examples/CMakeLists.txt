include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# ── Helper function ───────────────────────────────────────────────────────────
function(add_flux_example NAME)
    add_executable(${NAME} WIN32
        ${NAME}.cpp
        ${CMAKE_SOURCE_DIR}/src/main_win32.cpp
    )
    target_link_libraries(${NAME} PRIVATE flux::flux)
    target_link_options(${NAME} PRIVATE /SUBSYSTEM:WINDOWS)

    # So main_win32.cpp can find main_app.hpp
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/screenshots"
            "$<TARGET_FILE_DIR:${NAME}>/screenshots"
        COMMENT "Copying screenshots -> output dir for ${NAME}"
    )
endfunction()
# ── Examples ──────────────────────────────────────────────────────────────────
add_flux_example(paintfull)


# ── image_editor needs stb ────────────────────────────────────────────────────
# add_executable(image_editor WIN32 image_editor.cpp)
# target_link_libraries(image_editor PRIVATE flux::flux)
# target_link_options(image_editor PRIVATE /SUBSYSTEM:WINDOWS)
# target_include_directories(image_editor PRIVATE ${stb_SOURCE_DIR})
# target_compile_definitions(image_editor PRIVATE
#     STB_IMAGE_IMPLEMENTATION
#     STB_IMAGE_WRITE_IMPLEMENTATION
# )