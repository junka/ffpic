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
            "${FFPIC_ROOT}/coding")

list(APPEND FFPIC_SOURCES
            "${FFPIC_ROOT}/display/screen.c"
            "${FFPIC_ROOT}/format/gif.c")

list(APPEND FFPIC_HEADERS
            "${FFPIC_ROOT}/coding/lzw.h"
            "${FFPIC_ROOT}/coding/coding.h"
            "${FFPIC_ROOT}/display/screen.h"
            "${FFPIC_ROOT}/format/gif.h")

find_package(SDL2)
find_package(SDL2_IMAGE)

add_library(ffpic ${FFPIC_SOURCES} ${FFPIC_HEADERS})


add_executable(picinfo "${FFPIC_ROOT}/app/picinfo.c" $<TARGET_OBJECTS:ffpic>)
target_link_libraries(picinfo ffpic)
target_include_directories(picinfo PRIVATE ${FFPIC_DIRS})

if(SDL2_FOUND)
  target_include_directories(ffpic PRIVATE ${SDL2_INCLUDE_DIRS})
  target_include_directories(picinfo PRIVATE ${SDL2_INCLUDE_DIRS})
  message("SDL2 library found ${SDL2_INCLUDE_DIRS}")
  target_link_libraries(picinfo ${SDL2_LIBRARIES})
else(SDL2_FOUND)
  message(FATAL_ERROR "SDL2 library not found")
endif(SDL2_FOUND)

# if(SDL2_IMAGE_FOUND)
#   target_include_directories(ffpic PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
#   target_include_directories(picinfo PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
#   message("SDL2_IMAGE library found ${SDL2_IMAGE_INCLUDE_DIRS}")
#   target_link_libraries(picinfo ${SDL2_IMAGE_LIBRARIES})
# else(SDL2_IMAGE_FOUND)
#   message(FATAL_ERROR "SDL2_image library not found")
# endif(SDL2_IMAGE_FOUND)