if(ANDROID)
    return()
endif()

function(add_flux_example NAME)

    if(EMSCRIPTEN)
        add_executable(${NAME}
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_web.cpp
        )

        set_target_properties(${NAME} PROPERTIES
            SUFFIX ".html"
        )

        target_link_options(${NAME} PRIVATE
            "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
            "--preload-file=${CMAKE_SOURCE_DIR}/web/fonts@/fonts"
            "-sEXIT_RUNTIME=0"
            "-sUSE_WEBGL2=1"
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sFULL_ES3=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=67108864"
            "-sFETCH=1"
            "-sENVIRONMENT=web"
            "-sGL_ENABLE_GET_PROC_ADDRESS=1"
            "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$Browser']"
            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8','lengthBytesUTF8','HEAPU8','HEAPF32']"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free',\
            '_fluxWsOnOpen','_fluxWsOnMessage','_fluxWsOnError','_fluxWsOnClose']"
            $<$<CONFIG:Debug>:-sASSERTIONS=2>
            $<$<CONFIG:Debug>:-sSAFE_HEAP=1>
        )


        # Asset images (screenshots/*.png, *.jpg) referenced via AssetImage()
        # use a plain fopen() on the Emscripten virtual filesystem. That VFS
        # is only populated by --preload-file. Previously only "web/fonts"
        # was preloaded, so every AssetImage() load failed with ENOENT and
        # fell into the widget's Error state (pink box).
        #
        # The native post-build step below copies "screenshots/" next to the
        # executable, but that step is skipped for Emscripten (there is no
        # "next to the executable" on web), so it must be preloaded into the
        # VFS instead, mounted at the same relative path the code uses
        # ("screenshots/...") so fopen() resolves it from cwd "/".
        if(EXISTS "${CMAKE_SOURCE_DIR}/screenshots")
            target_link_options(${NAME} PRIVATE
                "--preload-file=${CMAKE_SOURCE_DIR}/screenshots@/screenshots"
            )
        endif()
        # ───────────────────────────────────────────────────────────────────

    elseif(WIN32)
        add_executable(${NAME} WIN32
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_win32.cpp
        )
        target_link_options(${NAME} PRIVATE /SUBSYSTEM:WINDOWS)

    elseif(APPLE)
        add_executable(${NAME}
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_macos.mm
        )

    elseif(UNIX)
        add_executable(${NAME}
            ${NAME}.cpp
            ${CMAKE_SOURCE_DIR}/src/main_linux.cpp
        )
    endif()

    target_link_libraries(${NAME} PRIVATE flux::flux)

    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    # Screenshots only make sense on native builds
    if(NOT EMSCRIPTEN)
        if(EXISTS "${CMAKE_SOURCE_DIR}/screenshots")
            add_custom_command(TARGET ${NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/screenshots"
                    "$<TARGET_FILE_DIR:${NAME}>/screenshots"
                COMMENT "Copying screenshots -> output dir for ${NAME}"
            )
        endif()
    endif()

endfunction()

add_flux_example(overlaytest)