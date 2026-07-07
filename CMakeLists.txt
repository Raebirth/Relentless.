cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Template VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

include(FetchContent)
FetchContent_Declare(
  mimalloc
  GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
  GIT_TAG v2.1.2 
)

set(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "Build object library" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(MI_OVERRIDE ON CACHE BOOL "Override standard malloc" FORCE)

FetchContent_MakeAvailable(mimalloc)

target_link_libraries(${PROJECT_NAME} mimalloc-static)
target_include_directories(${PROJECT_NAME} PRIVATE ${mimalloc_SOURCE_DIR}/include)

setup_geode_mod(${PROJECT_NAME})
