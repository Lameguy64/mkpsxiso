# mkpsxiso build script (rewritten)
# (C) 2021 spicyjpeg

cmake_minimum_required(VERSION 3.21)
#include(FindPkgConfig)

project(
	mkpsxiso
	LANGUAGES    C CXX
	VERSION      1.27
	DESCRIPTION  "PlayStation ISO image maker"
	HOMEPAGE_URL "https://github.com/Lameguy64/mkpsxiso"
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_STANDARD           11)
set(CMAKE_CXX_STANDARD         11)

## External dependencies

if(NOT EXISTS tinyxml2/tinyxml2.cpp)
	message(FATAL_ERROR "The tinyxml2 directory is empty. Run 'git submodule update --init' to populate it.")
endif()

# Build tinyxml2. I didn't bother with tinyxml2's actual CMake integration
# because it's far easier do do this. It is a single-file library after all.
add_library               (tinyxml2 STATIC tinyxml2/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC tinyxml2)

#add_subdirectory(tinyxml2)

## Executables

function(_add_target name source_dir)
	file(
		GLOB _sources
		${source_dir}/*.c
		${source_dir}/*.cpp
	)

	add_executable            (${name} ${_sources})
	target_compile_definitions(${name} PUBLIC VERSION="${PROJECT_VERSION}")
	target_link_libraries     (${name} tinyxml2)
endfunction()

_add_target(mkpsxiso src)
#_add_target(mkisoxml mkisoxml/src)

install(
	TARGETS mkpsxiso
	#RUNTIME_DEPENDENCIES
	#PRE_EXCLUDE_REGEXES ".*"
	#PRE_INCLUDE_REGEXES "tinyxml2"
)
