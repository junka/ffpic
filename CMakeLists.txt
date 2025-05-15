cmake_minimum_required(VERSION 3.11)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

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

if (NOT MSVC)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -g -O0")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra")

  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -rdynamic")

endif()

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra")
endif(CMAKE_COMPILER_IS_GNUCXX)

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
  "${FFPIC_ROOT}/utils/queue.c"
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
find_package(Vulkan)

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
          COMMAND ${CLSOURCE_COMPILER} -i ${INPUT_FILE} | sed 's/[a-z_A-Z0-9]*opcl_cl/srcstr/g' > ${OUTPUT_FILE}
          COMMENT "Compiling ${INPUT_FILE} to binary")
      LIST(APPEND FFPIC_ACCL ${OUTPUT_FILE})
  ENDFOREACH()

  list(APPEND FFPIC_ACCL
    "${FFPIC_ROOT}/arch/opencl/opcl.c")
endif()

if (Vulkan_FOUND)
  set(SPIRV_DIR ${CMAKE_CURRENT_BINARY_DIR})
  FILE(GLOB_RECURSE SHADER_FILES "${FFPIC_ROOT}/arch/vulkan/*.comp")
  FOREACH(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SPIRV_FILE ${SPIRV_DIR}/${SHADER_NAME}.spv)
    set(HEADER_FILE ${SPIRV_DIR}/${SHADER_NAME}.h)

    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND glslc -c ${SHADER} -fshader-stage=compute -g -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
    )

    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND xxd -i ${SPIRV_FILE} | sed "s/[_a-zA-Z0-9]*${SHADER_NAME}/${SHADER_NAME}/g" > ${HEADER_FILE}
        DEPENDS ${SPIRV_FILE}
        COMMENT "Converting ${SPIRV} to header file"
    )

    list(APPEND SPIRV_HEADERS ${HEADER_FILE})
  ENDFOREACH()
  message("${SPIRV_HEADERS}")
  list(APPEND FFPIC_ACCL
    "${FFPIC_ROOT}/arch/vulkan/vk_idct.c")
endif()

add_library(ffpic ${FFPIC_DISPLAY} ${FFPIC_FORMART} ${FFPIC_ACCL} ${FFPIC_CODING})
target_include_directories(ffpic PRIVATE ${FFPIC_DIRS})


if (Vulkan_FOUND)
  add_custom_target(compile_shaders DEPENDS ${SPIRV_HEADERS})
  add_dependencies(ffpic compile_shaders)
  target_include_directories(ffpic PRIVATE ${SPIRV_DIR})
endif ()

if(OpenCL_FOUND)
  target_compile_options(ffpic PRIVATE -DENABLE_OPENCL)
endif()

add_executable(picinfo "${FFPIC_ROOT}/app/picinfo.c")
add_executable(transbmp "${FFPIC_ROOT}/app/transbmp.c")
add_executable(transcode "${FFPIC_ROOT}/app/transcode.c")

if(Vulkan_FOUND)
  target_include_directories(ffpic PRIVATE $(Vulkan_INCLUDE_DIRS))
  include_directories(ffpic "${FFPIC_ROOT}/arch/vulkan/")
  target_compile_options(ffpic PRIVATE -DENABLE_VULKAN)
endif()

if(SDL2_FOUND)
  add_executable(sdlshow "${FFPIC_ROOT}/app/sdlshow.c" ${FFPIC_SDL})
  target_link_libraries(sdlshow ffpic m)
  target_include_directories(sdlshow PRIVATE ${FFPIC_DIRS})
endif(SDL2_FOUND)

target_link_libraries(picinfo ffpic m ${CMAKE_DL_LIBS})
target_link_libraries(transbmp ffpic m ${CMAKE_DL_LIBS})
target_link_libraries(transcode ffpic m ${CMAKE_DL_LIBS})
target_include_directories(picinfo PRIVATE ${FFPIC_DIRS})
target_include_directories(transbmp PRIVATE ${FFPIC_DIRS})
target_include_directories(transcode PRIVATE ${FFPIC_DIRS})

if(SDL2_FOUND)
  target_include_directories(sdlshow PRIVATE ${SDL2_INCLUDE_DIRS})
  message("SDL2 library found ${SDL2_INCLUDE_DIRS}")
  target_link_libraries(sdlshow ${SDL2_LIBRARIES})
else(SDL2_FOUND)
  message("SDL2 library not found")
endif(SDL2_FOUND)

if(OpenCL_FOUND)
  target_include_directories(ffpic PRIVATE ${OpenCL_INCLUDE_DIRS})
  target_link_libraries(ffpic OpenCL::OpenCL)
  target_link_libraries(picinfo OpenCL::OpenCL)
  target_link_libraries(transbmp OpenCL::OpenCL)
  target_link_libraries(transcode OpenCL::OpenCL)
  if(SDL2_FOUND)
    target_link_libraries(sdlshow OpenCL::OpenCL)
  endif(SDL2_FOUND)
endif()

if (Vulkan_FOUND)
  target_link_libraries(ffpic ${CMAKE_DL_LIBS})
  target_link_libraries(picinfo ${CMAKE_DL_LIBS})
  target_link_libraries(transbmp ${CMAKE_DL_LIBS})
  target_link_libraries(transcode ${CMAKE_DL_LIBS})
  if(SDL2_FOUND)
    target_link_libraries(sdlshow ${CMAKE_DL_LIBS})
  endif(SDL2_FOUND)
endif()

include(CTest)
option(BUILD_TESTING "Build the test cases" OFF)
if (BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
