cmake_minimum_required(VERSION 3.6)

set(FFPIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FFPIC_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if("${FFPIC_ROOT}" STREQUAL "${FFPIC_CONFIG_DIR}")
  message(
    FATAL_ERROR "Building from within the aom source tree is not supported.\n"
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

list(APPEND FFPIC_SOURCES
            "${FFPIC_ROOT}/display/screen.c")

list(APPEND FFPIC_CODING
            "${FFPIC_ROOT}/coding/lzw.c"
            "${FFPIC_ROOT}/coding/lz77.c"
            "${FFPIC_ROOT}/coding/huffman.c"
            "${FFPIC_ROOT}/coding/deflate.c"
            "${FFPIC_ROOT}/utils/crc.c"
            "${FFPIC_ROOT}/utils/alder.c"
            "${FFPIC_ROOT}/utils/hexdump.c")

list(APPEND FFPIC_FORMART
            "${FFPIC_ROOT}/format/png.c"
            "${FFPIC_ROOT}/format/gif.c"
            "${FFPIC_ROOT}/format/bmp.c"
            "${FFPIC_ROOT}/format/tiff.c"
            "${FFPIC_ROOT}/format/pnm.c"
            "${FFPIC_ROOT}/format/file.c")

find_package(SDL2)
find_package(SDL2_IMAGE)

add_library(ffpic ${FFPIC_SOURCES} ${FFPIC_CODING} ${FFPIC_FORMART})
target_include_directories(ffpic PRIVATE ${FFPIC_DIRS})


add_executable(picshow "${FFPIC_ROOT}/app/picshow.c" $<TARGET_OBJECTS:ffpic>)
add_executable(picinfo "${FFPIC_ROOT}/app/picinfo.c" ${FFPIC_FORMART} ${FFPIC_CODING})
target_link_libraries(picinfo z)
target_link_libraries(picshow z)

target_link_libraries(picshow ffpic)
target_include_directories(picshow PRIVATE ${FFPIC_DIRS})
target_include_directories(picinfo PRIVATE ${FFPIC_DIRS})

if(SDL2_FOUND)
  target_include_directories(ffpic PRIVATE ${SDL2_INCLUDE_DIRS})
  target_include_directories(picshow PRIVATE ${SDL2_INCLUDE_DIRS})
  message("SDL2 library found ${SDL2_INCLUDE_DIRS}")
  target_link_libraries(picshow ${SDL2_LIBRARIES})
else(SDL2_FOUND)
  message(FATAL_ERROR "SDL2 library not found")
endif(SDL2_FOUND)
