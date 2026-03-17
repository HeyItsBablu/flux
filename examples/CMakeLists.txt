# ── FetchContent for stb ──────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# ── Helper function ───────────────────────────────────────────────────────────
function(add_flux_example NAME)
    add_executable(${NAME} WIN32 ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE flux::flux)
    target_link_options(${NAME} PRIVATE /SUBSYSTEM:WINDOWS)
endfunction()

# ── Examples ──────────────────────────────────────────────────────────────────
# add_flux_example(main)
# add_flux_example(counter)
# add_flux_example(component)
# add_flux_example(draggable)
# add_flux_example(layout)
# add_flux_example(listview)
# add_flux_example(graphtest)
# add_flux_example(illustrator)
# add_flux_example(overlaytest)
add_flux_example(paintfull)
#add_flux_example(logic_sim)

# ── image_editor needs stb ────────────────────────────────────────────────────
# add_executable(image_editor WIN32 image_editor.cpp)
# target_link_libraries(image_editor PRIVATE flux::flux)
# target_link_options(image_editor PRIVATE /SUBSYSTEM:WINDOWS)
# target_include_directories(image_editor PRIVATE ${stb_SOURCE_DIR})
# target_compile_definitions(image_editor PRIVATE
#     STB_IMAGE_IMPLEMENTATION
#     STB_IMAGE_WRITE_IMPLEMENTATION
# )