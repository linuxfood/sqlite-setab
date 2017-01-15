cmake_minimum_required(VERSION 3.2.0)
project(zeromq_builder C CXX)
include(ExternalProject)

find_package(Threads REQUIRED)

externalproject_add(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.2.1
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/zeromq-v4.2.1"
    BUILD_IN_SOURCE 0
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/zeromq-v4.2.1
      -DENABLE_DRAFTS=ON
      -DZMQ_BUILD_TESTS=OFF
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
)

externalproject_get_property(libzmq DOWNLOAD_DIR)
set(ZEROMQ_INCLUDE_DIRS ${DOWNLOAD_DIR}/libzmq/include)

externalproject_get_property(libzmq BINARY_DIR)
set(ZEROMQ_LIBRARIES_DIR ${BINARY_DIR})

if(WIN32)
  set(ZEROMQ_LIBNAME ${ZEROMQ_LIBRARIES_DIR}/lib/libzmq-${CMAKE_PLATFORM_TOOLSET}-mt-gd-4_2_1.lib)
else()
  set(ZEROMQ_LIBNAME ${ZEROMQ_LIBRARIES_DIR}/lib/libzmq.so)
endif()

add_library(zeromq SHARED IMPORTED GLOBAL)
set_target_properties(
    zeromq
    PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX
        IMPORTED_LOCATION ${ZEROMQ_LIBNAME}
        COMPILE_DEFINITIONS "ZMQ_BUILD_DRAFT_API=1"
)

set_property(
  DIRECTORY ${CMAKE_SOURCE_DIR}

  APPEND_STRING
  PROPERTY COMPILE_DEFINITIONS "ZMQ_BUILD_DRAFT_API=1"
)
set_property(
  DIRECTORY ${CMAKE_SOURCE_DIR}

  APPEND
  PROPERTY INCLUDE_DIRECTORIES ${ZEROMQ_INCLUDE_DIRS}
)
set(ZEROMQ_LIBRARIES zeromq PARENT_SCOPE)
