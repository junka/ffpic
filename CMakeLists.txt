cmake_minimum_required(VERSION 3.6)
set(CMAKE_C_STANDARD 11)

set(FFPIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FFPIC_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")

if("${FFPIC_ROOT}" STREQUAL "${FFPIC_CONFIG_DIR}")
  message(
    FATAL_ERROR "Building from within the source tree is not supported.\n"
    "Hint: Run these commands\n"
    "$ rm -rf CMakeCache.txt CMakeFiles\n"
    "$ mkdir -p ffpic_build\n" "$ cd ffpic_build\n"
    "And re-run CMake from the ffpic_build directory.")
endif()

project(FFPIC C CXX)

# GENERATED source property global visibility.
if(POLICY CMP0118)
  cmake_policy(SET CMP0118 NEW)
endif()

# Enable generators like Xcode and Visual Studio to place projects in folders.
set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2)

list(APPEND FFPIC_DIRS
  "${FFPIC_ROOT}/display"
  "${FFPIC_ROOT}/format"
  "${FFPIC_ROOT}/coding"
  "${FFPIC_ROOT}/utils"
  "${FFPIC_ROOT}/arch"
  "${FFPIC_ROOT}/arch/x86"
  "${FFPIC_ROOT}/arch/opencl")

list(APPEND FFPIC_DISPLAY
  "${FFPIC_ROOT}/display/bmpwriter.c"
  "${FFPIC_ROOT}/display/display.c")
list(APPEND FFPIC_SDL
  "${FFPIC_ROOT}/display/sdl_screen.c")

list(APPEND FFPIC_CODING
  "${FFPIC_ROOT}/coding/lzw.c"
  "${FFPIC_ROOT}/coding/lz77.c"
  "${FFPIC_ROOT}/coding/huffman.c"
  "${FFPIC_ROOT}/coding/deflate.c"
  "${FFPIC_ROOT}/coding/booldec.c"
  "${FFPIC_ROOT}/coding/golomb.c"
  "${FFPIC_ROOT}/coding/cabac.c"
  "${FFPIC_ROOT}/utils/crc.c"
  "${FFPIC_ROOT}/utils/alder.c"
  "${FFPIC_ROOT}/utils/hexdump.c"
  "${FFPIC_ROOT}/utils/bitstream.c"
  "${FFPIC_ROOT}/utils/utils.c"
  "${FFPIC_ROOT}/utils/vlog.c"
  "${FFPIC_ROOT}/utils/byteorder.c"
  "${FFPIC_ROOT}/utils/idct.c"
  "${FFPIC_ROOT}/utils/colorspace.c"
  "${FFPIC_ROOT}/coding/hevc.c")

list(APPEND FFPIC_FORMART
  "${FFPIC_ROOT}/format/png.c"
  "${FFPIC_ROOT}/format/gif.c"
  "${FFPIC_ROOT}/format/bmp.c"
  "${FFPIC_ROOT}/format/tiff.c"
  "${FFPIC_ROOT}/format/pnm.c"
  "${FFPIC_ROOT}/format/jpg.c"
  "${FFPIC_ROOT}/format/tga.c"
  "${FFPIC_ROOT}/format/ico.c"
  "${FFPIC_ROOT}/format/basemedia.c"
  "${FFPIC_ROOT}/format/exr.c"
  "${FFPIC_ROOT}/format/psd.c"
  "${FFPIC_ROOT}/format/svg.c"
  "${FFPIC_ROOT}/format/file.c"
  "${FFPIC_ROOT}/format/webp.c"
  "${FFPIC_ROOT}/format/jp2.c"
  "${FFPIC_ROOT}/format/heif.c"
  "${FFPIC_ROOT}/format/avif.c"
  "${FFPIC_ROOT}/format/bpg.c"
  "${FFPIC_ROOT}/format/predict.c")

find_package(SDL2)
find_package(OpenCL)
find_library(MATH_LIBRARY m)

list(APPEND FFPIC_ACCL
  "${FFPIC_ROOT}/arch/accl.c"
  "${FFPIC_ROOT}/arch/x86/sse2.c"
  "${FFPIC_ROOT}/arch/x86/avx.c")
if(OpenCL_FOUND)
  SET(CLSOURCE_COMPILER xxd)
  FILE(GLOB_RECURSE OPENCL_SOURCES "${FFPIC_ROOT}/arch/opencl/*.cl")
  FOREACH(INPUT_FILE ${OPENCL_SOURCES})
      SET(OUTPUT_FILE ${INPUT_FILE}_xx.h)
      ADD_CUSTOM_COMMAND(
          OUTPUT ${OUTPUT_FILE}
          COMMAND ${CLSOURCE_COMPILER} -n srcstr -i ${INPUT_FILE} ${OUTPUT_FILE}
          COMMENT "Compiling ${INPUT_FILE} to binary")
      LIST(APPEND FFPIC_ACCL ${OUTPUT_FILE})
  ENDFOREACH()

  list(APPEND FFPIC_ACCL
    "${FFPIC_ROOT}/arch/opencl/opcl.c")
endif()


add_library(ffpic ${FFPIC_DISPLAY} ${FFPIC_FORMART} ${FFPIC_SDL} ${FFPIC_CODING} ${FFPIC_ACCL})
target_include_directories(ffpic PRIVATE ${FFPIC_DIRS})
target_compile_options(ffpic PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)

if(OpenCL_FOUND)
  target_compile_options(ffpic PRIVATE -DENABLE_OPENCL)
endif()

add_executable(sdlshow "${FFPIC_ROOT}/app/sdlshow.c")
add_executable(picinfo "${FFPIC_ROOT}/app/picinfo.c" ${FFPIC_FORMART} ${FFPIC_CODING} ${FFPIC_ACCL})

add_executable(transbmp "${FFPIC_ROOT}/app/transbmp.c" ${FFPIC_DISPLAY} ${FFPIC_FORMART} ${FFPIC_CODING} ${FFPIC_ACCL})

target_link_libraries(sdlshow ffpic m)
target_link_libraries(picinfo m)
target_link_libraries(transbmp m)
target_include_directories(sdlshow PRIVATE ${FFPIC_DIRS})
target_include_directories(picinfo PRIVATE ${FFPIC_DIRS})
target_include_directories(transbmp PRIVATE ${FFPIC_DIRS})

if(SDL2_FOUND)
  target_include_directories(ffpic PRIVATE ${SDL2_INCLUDE_DIRS})
  target_include_directories(sdlshow PRIVATE ${SDL2_INCLUDE_DIRS})
  message("SDL2 library found ${SDL2_INCLUDE_DIRS}")
  target_link_libraries(sdlshow ${SDL2_LIBRARIES})
else(SDL2_FOUND)
  message(FATAL_ERROR "SDL2 library not found")
endif(SDL2_FOUND)

if(OpenCL_FOUND)
  target_include_directories(ffpic PRIVATE ${OpenCL_INCLUDE_DIRS})
  target_link_libraries(ffpic OpenCL::OpenCL)
  target_link_libraries(sdlshow OpenCL::OpenCL)
  target_link_libraries(picinfo OpenCL::OpenCL)
  target_link_libraries(transbmp OpenCL::OpenCL)
endif()

include(CTest)
option(BUILD_TESTING "Build the test cases" OFF)
if (BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()