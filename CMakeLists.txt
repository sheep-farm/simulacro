cmake_minimum_required(VERSION 3.16)
project(simulacro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base aui propgrid)
include(${wxWidgets_USE_FILE})

# --------------------------------------------------------------------- SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

# ------------------------------------------------------------------ sources
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(simulacro ${CORE_SOURCES} ${UI_SOURCES})
target_include_directories(simulacro PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk")
target_link_libraries(simulacro PRIVATE
    ${wxWidgets_LIBRARIES}
    PkgConfig::SDL2
    PkgConfig::SDL2TTF)

if(MSVC)
    target_compile_options(simulacro PRIVATE /W4)
else()
    target_compile_options(simulacro PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------ plugins
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp")
set(PLUGIN_NAMES "")
foreach(src ${PLUGIN_SOURCES})
    get_filename_component(name "${src}" NAME_WE)
    list(APPEND PLUGIN_NAMES ${name})
    add_library(${name} MODULE "${src}")
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk")
endforeach()
if(PLUGIN_NAMES)
    add_custom_target(plugins DEPENDS ${PLUGIN_NAMES})
endif()

install(TARGETS simulacro RUNTIME DESTINATION bin)
install(DIRECTORY "${CMAKE_BINARY_DIR}/plugins/" DESTINATION share/simulacro/plugins
        OPTIONAL)
