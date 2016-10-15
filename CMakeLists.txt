cmake_minimum_required(VERSION 2.8.4)
include(FindPkgConfig)

#set global exe name
set(MAIN "mkpsxiso")

project(${MAIN})
#include directory, same for all build types
include_directories(include)

file(GLOB SOURCES "src/*.cpp")

find_package(PkgConfig REQUIRED)

pkg_search_module(TINYXML REQUIRED tinyxml2)

#include sources that are platform specific
if(MINGW OR MSYS OR CYGWIN OR MSVC)
  message(STATUS "WINDOWS BUILD")
  
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin_win)
  
elseif(APPLE OR WATCOM OR BORLAND)
  message(FATAL_ERROR "CAN NOT BUILD FOR APPLE, WATCOM, BORLAND")
  
elseif(UNIX)
  message(STATUS "LINUX BUILD")

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin_nix)

else()
  message(FATAL_ERROR "NO BUILD TARGET MATCH")
  
endif()

  
  add_executable(${MAIN} ${SOURCES})

target_link_libraries(${MAIN} ${TINYXML_LIBRARIES})