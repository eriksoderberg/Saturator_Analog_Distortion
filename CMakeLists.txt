cmake_minimum_required(VERSION 3.24)

set(RE_PROJECT_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# RE SDK version (required by online build server since March 2026).
# 5.0.0 (JukeboxSDK_500_012) for the C++-bound touch-screen display
# (display_id, periodic notify, images, paths). Runs in Reason 14+ only.
# Build setup mirrors PCFX, the first in-house SDK 5 device.
set(RE_SDK_VERSION 5.0.0)

include(cmake/options.cmake)
include(cmake/re-cmake.cmake)

# ----------------------------------------------------------------------
# Project name **IMPORTANT** (must match info.lua medium_name conventions)
# ----------------------------------------------------------------------
project(Saturator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

re_cmake_init(INCLUDES re-logging re-mock)

# ----------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------
set(RE_CPP_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src/cpp")

set(re_sources_cpp
    "${RE_CPP_SRC_DIR}/Saturator.h"
    "${RE_CPP_SRC_DIR}/Saturator.cpp"
    "${RE_CPP_SRC_DIR}/JukeboxExports.cpp"
    "${RE_CPP_SRC_DIR}/SaturatorShared.h"
    "${RE_CPP_SRC_DIR}/ui/SaturatorDisplay.h"
    "${RE_CPP_SRC_DIR}/ui/SaturatorDisplay.cpp"
    "${RE_CPP_SRC_DIR}/ui/CtlOverlay.h"
    "${RE_CPP_SRC_DIR}/ui/CtlOverlay.cpp"
    "${RE_CPP_SRC_DIR}/ui/TinyFmt.h"
)

# ----------------------------------------------------------------------
# GUI2D
# ----------------------------------------------------------------------
set(RE_2D_SRC_DIR "${RE_PROJECT_ROOT_DIR}/GUI2D")
include("${RE_2D_SRC_DIR}/gui_2D.cmake")

# ----------------------------------------------------------------------
# Plugin definition
# ----------------------------------------------------------------------
add_re_plugin(
    RE_SDK_VERSION         "${RE_SDK_VERSION}"
    RE_SDK_ROOT            "${RE_SDK_ROOT}"
    RE_2D_RENDER_ROOT      "${RE_2D_RENDER_ROOT}"
    DISPLAY_LUA

    BUILD_SOURCES          "${re_sources_cpp}"
    NATIVE_BUILD_SOURCES   "${re-logging_SOURCES}"

    RENDER_2D_SOURCES      "${re_sources_2d}"
    INCLUDE_DIRECTORIES    "${re-logging_INCLUDE_DIRS}" "${RE_CPP_SRC_DIR}"

    JBOX_COMPILE_OPTIONS   -Wall
    ENABLE_DEBUG_LOGGING
)

# ----------------------------------------------------------------------
# SDK 5 display exports (copied from PCFX). re-cmake v1.8.5 does not export
# the four display callbacks, and the host asserts the moment a display_id
# widget exists without them (ES20 / Cloud9 history).
# ----------------------------------------------------------------------
if(APPLE AND TARGET z-re-sdk-lib)
    target_link_options(z-re-sdk-lib PUBLIC
        "LINKER:-exported_symbol,_JukeboxExport_Draw"
        "LINKER:-exported_symbol,_JukeboxExport_Gesture"
        "LINKER:-exported_symbol,_JukeboxExport_DisplaySetup"
        "LINKER:-exported_symbol,_JukeboxExport_Notify"
    )
endif()
