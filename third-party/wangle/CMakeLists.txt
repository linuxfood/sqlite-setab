cmake_minimum_required(VERSION 2.8.8)
project(wangle_builder C CXX)
include(ExternalProject)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

externalproject_add(
    wangle_tp
    GIT_REPOSITORY https://github.com/facebook/wangle.git
    GIT_TAG v2016.10.24.00
    CMAKE_ARGS
      -DFOLLY_LIBRARYDIR=/usr/local/folly/lib
      -DFOLLY_INCLUDEDIR=/usr/local/folly/include
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
    SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/src/wangle_tp/wangle"
    # Disable install step
    INSTALL_COMMAND ""
    )

# Specify include dir
externalproject_get_property(wangle_tp DOWNLOAD_DIR)
set(
    WANGLE_INCLUDE_DIRS
    ${DOWNLOAD_DIR}/wangle_tp
    PARENT_SCOPE
)

# Specify MainTest's link libraries
externalproject_get_property(wangle_tp BINARY_DIR)
set(WANGLE_LIBS_DIR ${BINARY_DIR}/lib)

add_library(wangle STATIC IMPORTED GLOBAL)
set_target_properties(
    wangle
    PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES CXX
        IMPORTED_LOCATION ${WANGLE_LIBS_DIR}/libwangle.a
)

set(WANGLE_LIBRARIES wangle Threads::Threads ${OPENSSL_LIBRARIES} PARENT_SCOPE)
