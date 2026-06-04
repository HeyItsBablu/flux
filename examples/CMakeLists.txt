if(ANDROID)
    return()
endif()


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
    endif()

    # ── Common ────────────────────────────────────────────────────────────
    target_link_libraries(${NAME} PRIVATE flux::flux)

    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/screenshots")
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/screenshots"
                "$<TARGET_FILE_DIR:${NAME}>/screenshots"
            COMMENT "Copying screenshots -> output dir for ${NAME}"
        )
    endif()

endfunction()

add_flux_example(paintapp)