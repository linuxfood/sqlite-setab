
set(ROW_BUFFER_TEST_SRCS RowBufferTests.cpp)
set(STREAM_TIME_TEST_SRCS StreamTimeTest.cpp)

add_executable(row_buffer_harness ${ROW_BUFFER_TEST_SRCS})
target_link_libraries(
    row_buffer_harness
    setab_util
    ${FOLLY_LIBRARIES}
    ${GTEST_HARNESS_LIBS}
    ${LIBGLOG_LIBRARY}
    ${GFLAGS_LIBRARIES}
)

add_executable(stream_time_harness ${STREAM_TIME_TEST_SRCS})
target_link_libraries(
    stream_time_harness
    setab_util
    ${FOLLY_LIBRARIES}
    ${GTEST_HARNESS_LIBS}
    ${LIBGLOG_LIBRARY}
    ${GFLAGS_LIBRARIES}
)

add_test(row_buffer_test row_buffer_harness)
add_test(stream_time_test stream_time_harness)