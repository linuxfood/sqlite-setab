cmake_minimum_required(VERSION 2.8.8)
project(gtest_builder C CXX)
include(ExternalProject)

set(GTEST_FORCE_SHARED_CRT ON)
set(GTEST_DISABLE_PTHREADS OFF)

find_package(Threads REQUIRED)

externalproject_add(
    gtest_tp
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.8.0
    CMAKE_ARGS
        -Dgtest_force_shared_crt=${GTEST_FORCE_SHARED_CRT}
        -Dgtest_disable_pthreads=${GTEST_DISABLE_PTHREADS}
        -DBUILD_GTEST=ON
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    # Disable install step
    INSTALL_COMMAND ""
    )

# Specify include dir
externalproject_get_property(gtest_tp SOURCE_DIR)
set(GTEST_INCLUDE_DIRS ${SOURCE_DIR}/googletest/include)

# Specify MainTest's link libraries
externalproject_get_property(gtest_tp BINARY_DIR)
set(GTEST_LIBS_DIR ${BINARY_DIR}/googlemock/gtest)

add_library(gtest STATIC IMPORTED GLOBAL)
set_target_properties(
    gtest
    PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX
        IMPORTED_LOCATION ${GTEST_LIBS_DIR}/libgtest.a
)

add_library(gtest_main STATIC IMPORTED GLOBAL)
set_target_properties(
    gtest_main
    PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX
        IMPORTED_LOCATION ${GTEST_LIBS_DIR}/libgtest_main.a
)

set_property(
  DIRECTORY ${CMAKE_SOURCE_DIR}

  APPEND
  PROPERTY INCLUDE_DIRECTORIES ${GTEST_INCLUDE_DIRS}
)

set(GTEST_HARNESS_LIBS gtest gtest_main Threads::Threads PARENT_SCOPE)
