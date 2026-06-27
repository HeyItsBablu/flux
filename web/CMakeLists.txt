add_executable(flux_app
    main.cpp
    ${CMAKE_SOURCE_DIR}/lib/main.cpp
)

set_target_properties(flux_app PROPERTIES SUFFIX ".html")

target_link_libraries(flux_app PRIVATE flux::flux)
target_include_directories(flux_app PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(flux_app PRIVATE
    FLUX_APP_NAME="${FLUX_APP_NAME}"
    FLUX_APP_VERSION="${FLUX_APP_VERSION}"
    FLUX_APP_BUNDLE_ID="${FLUX_APP_BUNDLE_ID}"
)

# Generate shell.html from template
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.html.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shell.html"
)

target_link_options(flux_app PRIVATE
    "--shell-file=${CMAKE_CURRENT_BINARY_DIR}/shell.html"
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

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    target_link_options(flux_app PRIVATE
        "--preload-file=${CMAKE_SOURCE_DIR}/assets@/assets"
    )
endif()