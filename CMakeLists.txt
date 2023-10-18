cmake_minimum_required(VERSION 3.6)
set(CMAKE_C_STANDARD 11)

set(FFPIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FFPIC_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")

if("${FFPIC_ROOT}" STREQUAL "${FFPIC_CONFIG_DIR}")
  message(
    FATAL_ERROR "Building from within the source tree is not supported.\n"
    "Hint: Run these commands\n"
    "$ rm -rf CMakeCache.txt CMakeFiles\n"
    "$ mkdir -p ../ffpic_build\n" "$ cd ../ffpic_build\n"
    "And re-run CMake from the ffpic_build directory.")
endif()

project(FFPIC C CXX)

# GENERATED source property global visibility.
if(POLICY CMP0118)
  cmake_policy(SET CMP0118 NEW)
endif()

# Enable generators like Xcode and Visual Studio to place projects in folders.
set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2)

list(APPEND FFPIC_DIRS
  "${FFPIC_ROOT}/display"
  "${FFPIC_ROOT}/format"
  "${FFPIC_ROOT}/coding"
  "${FFPIC_ROOT}/utils")

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
  "${FFPIC_ROOT}/format/predict.c")

find_package(SDL2)

add_library(ffpic ${FFPIC_DISPLAY} ${FFPIC_FORMART} ${FFPIC_SDL} ${FFPIC_CODING})
target_include_directories(ffpic PRIVATE ${FFPIC_DIRS})
# target_compile_options(ffpic PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(sdlshow "${FFPIC_ROOT}/app/sdlshow.c" $<TARGET_OBJECTS:ffpic>)
add_executable(picinfo "${FFPIC_ROOT}/app/picinfo.c" ${FFPIC_FORMART} ${FFPIC_CODING})

add_executable(transbmp "${FFPIC_ROOT}/app/transbmp.c" ${FFPIC_DISPLAY} ${FFPIC_FORMART} ${FFPIC_CODING})

target_link_libraries(sdlshow ffpic)
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
