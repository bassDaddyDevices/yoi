#
# YOI VST3
#
# Builds the VST3 version of YOI from the same DSP kernel, editor page and factory presets as the
# Audio Unit. Follows Graphite's VST3 build. See README.md.
#

cmake_minimum_required(VERSION 3.25)

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    # The same as the Audio Unit (owner, 2026-10-06).
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "macOS deployment target")
endif()

project(YOI
    VERSION 1.0.0
    DESCRIPTION "YOI mono bass synth, VST3 edition"
    LANGUAGES C CXX
)

if(APPLE)
    # For the one Objective-C++ file that puts the web view into the host's window.
    enable_language(OBJC OBJCXX)
endif()

include(FetchContent)

# ---- Options ---------------------------------------------------------------------------------

if(APPLE OR WIN32)
    set(YOI_WEBVIEW_DEFAULT ON)
else()
    # Linux builds are for development and CI: the plug-in works with the host's generic editor.
    set(YOI_WEBVIEW_DEFAULT OFF)
endif()
option(YOI_WITH_WEBVIEW "Build the HTML/CSS/JS editor (macOS and Windows)" ${YOI_WEBVIEW_DEFAULT})
option(YOI_BUILD_TESTS "Build the VST3 tests" ON)
option(YOI_UI_DEBUG "Allow the web inspector in the editor" OFF)

set(VST3_SDK_DIR "" CACHE PATH "Use an existing VST3 SDK checkout instead of downloading one")
set(CHOC_DIR "" CACHE PATH "Use an existing choc checkout instead of downloading one")

# Only the plug-in and the validator are wanted from the SDK.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)

# Linking into the system VST3 folder needs admin rights (or Developer Mode) on Windows.
if(WIN32)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Link the built plug-in into the VST3 folder")
endif()

# ---- Dependencies ----------------------------------------------------------------------------
# Both are free and permissively licensed: the VST3 SDK (MIT since 3.8) and choc (ISC). The same
# versions Graphite ships with.

if(VST3_SDK_DIR)
    set(vst3sdk_SOURCE_DIR "${VST3_SDK_DIR}")
    add_subdirectory("${VST3_SDK_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk")
else()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.1_build_84
        GIT_SHALLOW TRUE
        GIT_SUBMODULES base pluginterfaces public.sdk cmake
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)
endif()

smtg_enable_vst3_sdk()

if(SMTG_RUN_VST_VALIDATOR AND NOT TARGET validator)
    add_subdirectory("${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-hosting/validator"
                     "${CMAKE_BINARY_DIR}/vst3sdk-validator")
endif()

# choc is needed even without the editor: state and presets are JSON.
if(CHOC_DIR)
    set(choc_SOURCE_DIR "${CHOC_DIR}")
else()
    FetchContent_Declare(choc
        GIT_REPOSITORY https://github.com/Tracktion/choc.git
        GIT_TAG 9606a6615b592605993d92733cdfd6ca6ecbd825
        SOURCE_SUBDIR do-not-add
    )
    FetchContent_MakeAvailable(choc)
endif()

# ---- Shared DSP ------------------------------------------------------------------------------

set(YOI_EXTENSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Yoi/YoiExtension")

# The kernel is header-only; this target carries its include paths and settings.
add_library(yoi_dsp INTERFACE)
target_include_directories(yoi_dsp INTERFACE
    "${YOI_EXTENSION_DIR}/DSP"
    "${YOI_EXTENSION_DIR}/Parameters"
    "${CMAKE_CURRENT_SOURCE_DIR}/source"
    "${choc_SOURCE_DIR}"
)
# Keeps AudioToolbox and CoreMIDI out of the kernel on every platform, macOS included.
target_compile_definitions(yoi_dsp INTERFACE YOI_PORTABLE=1)
# The SDK switches on -ffast-math for Xcode builds, which lets the compiler assume no NaN or
# infinity (turning the kernel's finite-value guards into no-ops) and computes differently from the
# AU. YOI's code is built to strict IEEE rules; these come after the SDK's options, so they win.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(yoi_dsp INTERFACE -fno-fast-math -ffp-contract=on)
endif()
target_compile_features(yoi_dsp INTERFACE cxx_std_20)

# ---- Embedded files: the editor page and the factory presets ---------------------------------
# Straight from the Audio Unit's folders, so both formats show the same page and the same presets.

set(YOI_WEBUI_DIR "${YOI_EXTENSION_DIR}/WebUI")
set(YOI_WEBUI_FILES
    index.html
    bdd-modern.css
    yoi-modern.css
    bdd-bridge.js
    bdd-pixel.js
    bdd-controls.js
    bdd-curve.js
    yoi-standin.js
    yoi-panel.js
    Manrope.ttf
    Unbounded.ttf
)
set(YOI_EMBED_SOURCES "")
set(YOI_EMBED_PATHS "")
foreach(file IN LISTS YOI_WEBUI_FILES)
    list(APPEND YOI_EMBED_SOURCES "${YOI_WEBUI_DIR}/${file}")
    list(APPEND YOI_EMBED_PATHS "/${file}")
endforeach()

file(GLOB YOI_FACTORY_PRESETS CONFIGURE_DEPENDS "${YOI_EXTENSION_DIR}/Presets/Factory/*.json")
list(SORT YOI_FACTORY_PRESETS)
foreach(file IN LISTS YOI_FACTORY_PRESETS)
    get_filename_component(name "${file}" NAME)
    list(APPEND YOI_EMBED_SOURCES "${file}")
    list(APPEND YOI_EMBED_PATHS "/factory/${name}")
endforeach()

set(YOI_EMBED_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/yoi_resources.cpp")
string(REPLACE ";" "|" YOI_EMBED_SOURCES_ARG "${YOI_EMBED_SOURCES}")
string(REPLACE ";" "|" YOI_EMBED_PATHS_ARG "${YOI_EMBED_PATHS}")
add_custom_command(
    OUTPUT "${YOI_EMBED_GENERATED}"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=${YOI_EMBED_GENERATED}"
        "-DSOURCES_ARG=${YOI_EMBED_SOURCES_ARG}"
        "-DPATHS_ARG=${YOI_EMBED_PATHS_ARG}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake"
    DEPENDS ${YOI_EMBED_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resources.cmake"
    COMMENT "Embedding the editor page and the factory presets"
    VERBATIM
)

# Everything but the VST3 classes, so the tests link the same code the plug-in runs.
add_library(yoi_common STATIC
    source/shared/bdd_drawing_library.cpp
    source/shared/bdd_preset_file.cpp
    source/shared/bdd_preset_library.cpp
    source/yoi_state.cpp
    "${YOI_EMBED_GENERATED}"
)
target_include_directories(yoi_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/source/shared")
target_link_libraries(yoi_common PUBLIC yoi_dsp)
set_target_properties(yoi_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Plug-in ---------------------------------------------------------------------------------

set(yoi_sources
    source/yoi_controller.cpp
    source/yoi_controller.h
    source/yoi_entry.cpp
    source/yoi_ids.h
    source/yoi_params.h
    source/yoi_processor.cpp
    source/yoi_processor.h
    source/yoi_state.h
    source/shared/bdd_param_spec.h
    source/shared/bdd_spec_parameter.h
    source/shared/bdd_platform.h
)
if(APPLE)
    list(APPEND yoi_sources source/shared/bdd_platform_mac.mm)
elseif(WIN32)
    list(APPEND yoi_sources source/shared/bdd_platform_win.cpp)
endif()
if(YOI_WITH_WEBVIEW)
    list(APPEND yoi_sources
        source/yoi_editor.cpp
        source/yoi_editor.h
        source/shared/bdd_web_editor.cpp
        source/shared/bdd_web_editor.h
    )
endif()

smtg_add_vst3plugin(YOI ${yoi_sources})

target_link_libraries(YOI PRIVATE sdk yoi_common)
target_compile_features(YOI PRIVATE cxx_std_20)
target_compile_definitions(YOI PRIVATE
    YOI_WITH_WEBVIEW=$<BOOL:${YOI_WITH_WEBVIEW}>
    YOI_UI_DEBUG=$<BOOL:${YOI_UI_DEBUG}>
)

if(APPLE)
    set_source_files_properties(source/shared/bdd_platform_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(YOI PRIVATE "-framework Cocoa")
    if(YOI_WITH_WEBVIEW)
        target_link_libraries(YOI PRIVATE "-framework WebKit")
    endif()
elseif(WIN32)
    target_link_libraries(YOI PRIVATE ole32 shell32 user32 version)
    target_compile_definitions(YOI PRIVATE NOMINMAX)
endif()

if(SMTG_MAC)
    smtg_target_set_bundle(YOI
        BUNDLE_IDENTIFIER "com.bassdaddydevices.yoi.vst3"
        COMPANY_NAME "Bass Daddy Devices"
    )
endif()

# ---- Tests -----------------------------------------------------------------------------------

if(YOI_BUILD_TESTS)
    enable_testing()
    add_executable(yoi_vst3_tests tests/yoi_vst3_tests.cpp)
    target_link_libraries(yoi_vst3_tests PRIVATE yoi_common)
    target_compile_definitions(yoi_vst3_tests PRIVATE
        YOI_STANDIN_JS="${YOI_WEBUI_DIR}/yoi-standin.js"
        YOI_FACTORY_DIR="${YOI_EXTENSION_DIR}/Presets/Factory"
    )
    add_test(NAME yoi_vst3_tests COMMAND yoi_vst3_tests)
endif()
