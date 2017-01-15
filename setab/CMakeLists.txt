add_library(
  setab_util STATIC

  Sqlite.h
  Util.cpp
  Util.h
  ZmqMsg.h
)

add_library(
  setab_core

  Registry.h
  Row.h
  RowBuffer.h
  Setab.cpp
  Setab.h
)

add_executable(
  setab

  engine.cpp
)
target_link_libraries(
    setab

    setab_core
    setab_util
    sqlite3
    ${FOLLY_LIBRARIES}
    ${LIBGLOG_LIBRARY}
    ${GFLAGS_LIBRARIES}
    ${ZEROMQ_LIBRARIES}
    Threads::Threads
)

add_executable(
  stream_maker

  stream_maker.cpp
)
target_link_libraries(
  stream_maker

  setab_core
  setab_util
  ${FOLLY_LIBRARIES}
  ${LIBGLOG_LIBRARY}
  ${GFLAGS_LIBRARIES}
  ${ZEROMQ_LIBRARIES}
  Boost::program_options
)

## Install Defs
install(
    TARGETS setab
    RUNTIME DESTINATION bin
    COMPONENT runtime
)
