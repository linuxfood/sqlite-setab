find_library(DL_LIBRARY dl)
add_library(
  sqlite3

  sqlite3.c
  sqlite3.h
)
target_link_libraries(
  sqlite3

  ${DL_LIBRARY}
)
target_include_directories(
  sqlite3

  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)
