# mkpsxiso build script (rewritten)
# (C) 2021 spicyjpeg

cmake_minimum_required(VERSION 3.21)
#include(FindPkgConfig)

project(
	mkpsxiso
	LANGUAGES    C CXX
	VERSION      1.26
	DESCRIPTION  "PlayStation ISO image maker"
	HOMEPAGE_URL "https://github.com/Lameguy64/mkpsxiso"
)

## External dependencies

# Let CMake attempt to find tinyxml2 on its own first. This avoids invoking
# pkg-config where it might not be installed, and allows usage of package
# managers like vcpkg. The path to tinyxml2 can also be specified manually by
# passing -Dtinyxml2_ROOT.
find_package(tinyxml2 CONFIG)

if(NOT tinyxml2_FOUND)
	find_package(PkgConfig REQUIRED)
	pkg_search_module(_tinyxml2 REQUIRED IMPORTED_TARGET tinyxml2)

	add_library(tinyxml2::tinyxml2 ALIAS PkgConfig::_tinyxml2)
endif()

## Executables

# This is required in order to properly link against tinyxml2 under MSVC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD         11)

function(_add_target name source_dir)
	file(
		GLOB _sources
		${source_dir}/*.c
		${source_dir}/*.cpp
	)

	add_executable            (${name} ${_sources})
	target_compile_definitions(${name} PUBLIC VERSION="${PROJECT_VERSION}")
	target_link_libraries     (${name} tinyxml2::tinyxml2)
endfunction()

_add_target(mkpsxiso src)
#_add_target(mkisoxml mkisoxml/src)

# Ensure the tinyxml2 DLL (if any) is installed alongside mkpsxiso.
install(
	TARGETS mkpsxiso
	RUNTIME_DEPENDENCIES
	PRE_EXCLUDE_REGEXES ".*"
	PRE_INCLUDE_REGEXES "tinyxml2"
)
