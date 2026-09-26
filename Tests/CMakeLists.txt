#
# YOI DSP tests
#
# Renders audio through the shared kernel with no plug-in format involved, the same way the
# VST3 build will compile it (YOI_PORTABLE). Run from the workspace root:
#
#   cmake -S Tests -B Tests/build && cmake --build Tests/build && ctest --test-dir Tests/build
#

cmake_minimum_required(VERSION 3.25)

project(YoiDSPTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(YOI_EXTENSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Yoi/YoiExtension")

add_executable(yoi_dsp_tests yoi_dsp_tests.cpp)
target_include_directories(yoi_dsp_tests PRIVATE
    "${YOI_EXTENSION_DIR}/DSP"
    "${YOI_EXTENSION_DIR}/Parameters"
)
# Keeps AudioToolbox out of the kernel, as it will be for VST3.
target_compile_definitions(yoi_dsp_tests PRIVATE YOI_PORTABLE=1)

if(MSVC)
    target_compile_options(yoi_dsp_tests PRIVATE /W4)
else()
    target_compile_options(yoi_dsp_tests PRIVATE -Wall -Wextra -Wshadow)
endif()

enable_testing()
add_test(NAME yoi_dsp_tests COMMAND yoi_dsp_tests)
