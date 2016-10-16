
set(LIBUTIL_SRCS
    Sqlite.h
    Util.cpp
    Util.h
    ZmqMsg.h
)

set(LIBSETAB_CORE_SRCS
    Registry.h
    Row.h
    RowBuffer.h
    Setab.cpp
    Setab.h
)

set(SETAB_SRCS
    engine.cpp
)

set(STREAM_MAKER_SRCS
    stream_maker.cpp
)

add_library(setab_util STATIC ${LIBUTIL_SRCS})
add_library(setab_core ${LIBSETAB_CORE_SRCS})

add_executable(setab ${SETAB_SRCS})
target_link_libraries(setab setab_core setab_util)
target_link_libraries(setab
    ${FOLLY_LIBRARIES}
    ${LIBSQLITE3_LIBRARY}
    ${LIBGLOG_LIBRARY}
    ${GFLAGS_LIBRARIES}
    ${ZMQ_LIBRARY}
)

add_executable(stream_maker ${STREAM_MAKER_SRCS})
target_link_libraries(stream_maker setab_core setab_util)
target_link_libraries(stream_maker
    ${FOLLY_LIBRARIES}
    ${LIBGLOG_LIBRARY}
    ${GFLAGS_LIBRARIES}
    ${ZMQ_LIBRARY}
    Boost::program_options
)

## Install Defs
install(
    TARGETS setab
    RUNTIME DESTINATION bin
    COMPONENT runtime
)