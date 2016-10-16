cmake_minimum_required(VERSION 2.8.7 FATAL_ERROR)

# This is necessary so that find_package can find the "Find$x.cmake" files.
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# package information
set(PACKAGE_NAME      "setab")
set(PACKAGE_VERSION   "1")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/linuxfood/sqlite-setab/issues")

project(${PACKAGE_NAME} C CXX)

# Check architecture OS
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Setab requires a 64bit OS")
endif()

find_package(
    Boost 1.58.0 REQUIRED
    COMPONENTS
        program_options
)

find_package(Folly REQUIRED)
find_package(GFlags REQUIRED)
find_package(Glog REQUIRED)
find_package(LibSqlite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZMQ REQUIRED)

include(CheckFunctionExists)
include(SetabCompilerOptions)

set(SETAB_HOME ${CMAKE_CURRENT_SOURCE_DIR})
set(TP_PROJECTS_DIR "third-party")
# So that qualified includes work. E.g. #include "setab/$x.h"
include_directories(${SETAB_HOME})
# So that all subsequent directories have access to folly.
include_directories(${FOLLY_INCLUDE_DIR})

add_subdirectory(${TP_PROJECTS_DIR}/gtest)
include_directories(${GTEST_INCLUDE_DIRS})
enable_testing()
add_subdirectory(setab)
add_subdirectory(tests)

## Install Defs
# use GNU install dirs (e.g. lib64 instead of lib)
include(GNUInstallDirs)

# modules / depends
file(GLOB SETAB_CMAKE_FILES "CMake/*.cmake")
install(
    FILES ${SETAB_CMAKE_FILES}
    DESTINATION CMake
    COMPONENT dev
)