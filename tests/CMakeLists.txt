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