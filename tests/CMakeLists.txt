cmake_minimum_required(VERSION 3.6)

set(HUFFMAN_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_huffman.c)
add_executable(test_huffman ${HUFFMAN_TEST})
target_include_directories(test_huffman PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_huffman ffpic m)
add_test(NAME test_huffman COMMAND test_huffman)


set(BITSTREAM_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_bitstream.c)
add_executable(test_bitstream ${BITSTREAM_TEST})
target_include_directories(test_bitstream PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_bitstream ffpic m)
add_test(NAME test_bitstream COMMAND test_bitstream)


set(BITSTREAM_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_queue.c)
add_executable(test_queue ${BITSTREAM_TEST})
target_include_directories(test_queue PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_queue ffpic m pthread)
add_test(NAME test_queue COMMAND test_queue)


set(DCT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_dct.c)
add_executable(test_dct ${DCT_TEST})
target_include_directories(test_dct PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_dct ffpic m)
add_test(NAME test_dct COMMAND test_dct)


set(CABAC_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_cabac.c)
add_executable(test_cabac ${CABAC_TEST})
target_include_directories(test_cabac PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_cabac ffpic m)
add_test(NAME test_cabac COMMAND test_cabac)


set(BOOLDEC_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_booldec.c)
add_executable(test_booldec ${BOOLDEC_TEST})
target_include_directories(test_booldec PRIVATE ${FFPIC_DIRS})
target_link_libraries(test_booldec ffpic m)
add_test(NAME test_booldec COMMAND test_booldec)


set(JPG_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_jpg.c)
add_executable(test_jpg ${JPG_TEST})
target_include_directories(test_jpg PRIVATE ${FFPIC_DIRS})
target_compile_definitions(test_jpg PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_jpg ffpic m)
add_test(NAME test_jpg COMMAND test_jpg)


set(DECODE_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_decode.c)
add_executable(test_decode ${DECODE_TEST})
target_include_directories(test_decode PRIVATE ${FFPIC_DIRS})
target_compile_definitions(test_decode PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_decode ffpic m)
add_test(NAME test_decode COMMAND test_decode)


set(DECODE_MORE_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_decode_more.c)
add_executable(test_decode_more ${DECODE_MORE_TEST})
target_include_directories(test_decode_more PRIVATE ${FFPIC_DIRS})
target_compile_definitions(test_decode_more PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_decode_more ffpic m)
add_test(NAME test_decode_more COMMAND test_decode_more)


set(HEVC_TEST ${CMAKE_CURRENT_SOURCE_DIR}/test_hevc.c)
add_executable(test_hevc ${HEVC_TEST})
target_include_directories(test_hevc PRIVATE ${FFPIC_DIRS})
target_compile_definitions(test_hevc PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_hevc ffpic m)
add_test(NAME test_hevc COMMAND test_hevc)
