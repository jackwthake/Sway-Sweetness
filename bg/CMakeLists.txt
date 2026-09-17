cmake_minimum_required(VERSION 3.16)
project(bg C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Build SDL first ---
set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/third-party/SDL")
if(NOT EXISTS "${SDL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL source not found at ${SDL_DIR}. Check the vendored dependency path.")
endif()
add_subdirectory("${SDL_DIR}")

# --- Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL     REQUIRED egl)
pkg_check_modules(GLES    REQUIRED glesv2)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# --- Wayland protocol glue generation ---
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
set(GEN_DIR   "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

# xdg-shell is stable Wayland; use the vendored SDL copy
set(XDG_SHELL_XML
    "${CMAKE_CURRENT_SOURCE_DIR}/src/third-party/SDL/wayland-protocols/xdg-shell.xml")

function(gen_protocol name xml)
    add_custom_command(
        OUTPUT "${GEN_DIR}/${name}-client-protocol.h"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}"
                "${GEN_DIR}/${name}-client-protocol.h"
        DEPENDS "${xml}" VERBATIM)
    add_custom_command(
        OUTPUT "${GEN_DIR}/${name}-protocol.c"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}"
                "${GEN_DIR}/${name}-protocol.c"
        DEPENDS "${xml}" "${GEN_DIR}/${name}-client-protocol.h" VERBATIM)
endfunction()

gen_protocol(xdg-shell                   "${XDG_SHELL_XML}")
gen_protocol(wlr-layer-shell-unstable-v1 "${PROTO_DIR}/wlr-layer-shell-unstable-v1.xml")

add_custom_target(protocols ALL DEPENDS
    "${GEN_DIR}/xdg-shell-client-protocol.h"
    "${GEN_DIR}/xdg-shell-protocol.c"
    "${GEN_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
    "${GEN_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
)

# --- Main executable ---
file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(${PROJECT_NAME}
    ${SOURCES}
    "${GEN_DIR}/xdg-shell-protocol.c"
    "${GEN_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
)

add_dependencies(${PROJECT_NAME} protocols)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    "${GEN_DIR}"
    ${WAYLAND_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    SDL3::SDL3
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    m
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wshadow
    ${WAYLAND_CFLAGS_OTHER}
    -O3 -march=native -ffast-math
)

# --- Copy assets to build directory ---
file(GLOB_RECURSE ASSET_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")

add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMENT "Copying assets to build directory"
    SOURCES ${ASSET_FILES}
)

add_dependencies(${PROJECT_NAME} copy_assets)
