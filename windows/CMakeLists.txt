add_executable(flux_app WIN32
    main.cpp
    ${CMAKE_SOURCE_DIR}/lib/main.cpp
)

target_link_libraries(flux_app PRIVATE flux::flux dbghelp)
target_include_directories(flux_app PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_options(flux_app PRIVATE /SUBSYSTEM:WINDOWS)

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    add_custom_command(TARGET flux_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:flux_app>"
        COMMENT "Copying assets"
    )
endif()