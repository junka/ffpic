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
