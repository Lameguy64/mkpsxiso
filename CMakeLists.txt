cmake_minimum_required(VERSION 2.8.4)
include(FindPkgConfig)

#set global exe name
set(MAIN "mkpsxiso")
set (CMAKE_CXX_STANDARD 11)

project(${MAIN})
#include directory, same for all build types
include_directories(src)

file(GLOB SOURCES "src/*.cpp")

find_package(PkgConfig REQUIRED)

pkg_search_module(TINYXML REQUIRED tinyxml2)

#include sources that are platform specific
if(MINGW OR CYGWIN)
  message(STATUS "WINDOWS BUILD")
  
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin_win)
  
elseif(MSYS OR MSVC)
  message(FATAL_ERROR "CAN NOT BUILD ON MICROSOFT COMPILER")

elseif(WATCOM OR BORLAND)
  message(FATAL_ERROR "CAN NOT BUILD FOR APPLE, WATCOM, BORLAND")
  
elseif(UNIX OR APPLE)
  message(STATUS "UNIX BUILD")

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin_nix)

else()
  message(FATAL_ERROR "NO BUILD TARGET MATCH")
  
endif()

add_executable(${MAIN} ${SOURCES})

target_link_libraries(${MAIN} ${TINYXML_LIBRARIES})
