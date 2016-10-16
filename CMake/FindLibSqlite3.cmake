# Finds libsqlite3.
#
# This module defines:
# LIBSQLITE3_INCLUDE_DIR
# LIBSQLITE3_LIBRARY
#

find_package(PkgConfig)
pkg_check_modules(PC_SQLITE3 QUIET sqlite3)

find_path(
    LIBSQLITE3_INCLUDE_DIR
    NAMES sqlite3.h
    HINTS ${PC_SQLITE3_INCLUDE_DIRS}
)

find_library(LIBSQLITE3_LIBRARY NAMES sqlite3)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    LIBSQLITE3 DEFAULT_MSG LIBSQLITE3_INCLUDE_DIR LIBSQLITE3_LIBRARY)

mark_as_advanced(LIBSQLITE3_INCLUDE_DIR LIBSQLITE3_LIBRARY LIBSQLITE3_FOUND)

if(LIBSQLITE3_FOUND AND NOT LIBSQLITE3_FIND_QUIETLY)
    message(STATUS "LIBSQLITE3: ${LIBSQLITE3_INCLUDE_DIR}")
endif()