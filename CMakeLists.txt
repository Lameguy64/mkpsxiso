# mkpsxiso build script (rewritten)
# (C) 2021 spicyjpeg

cmake_minimum_required(VERSION 3.20)

project(
	mkpsxiso
	LANGUAGES    C CXX
	VERSION      1.27
	DESCRIPTION  "PlayStation ISO image maker"
	HOMEPAGE_URL "https://github.com/Lameguy64/mkpsxiso"
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_STANDARD           17)
set(CMAKE_CXX_STANDARD         17)

# Useful paths
set(mkpsxiso_dir "src/mkpsxiso")
set(dumpsxiso_dir "src/dumpsxiso")
set(shared_dir "src/shared")

## External dependencies

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/tinyxml2/tinyxml2.cpp)
	message(FATAL_ERROR "The tinyxml2 directory is empty. Run 'git submodule update --init --recursive' to populate it.")
endif()

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/miniaudio/miniaudio.h)
	message(FATAL_ERROR "The miniaudio directory is empty. Run 'git submodule update --init --recursive' to populate it.")
endif()

# Build tinyxml2. I didn't bother with tinyxml2's actual CMake integration
# because it's far easier do do this. It is a single-file library after all.
add_library               (tinyxml2 STATIC tinyxml2/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC tinyxml2)

## Internal dependencies

# Populate shared files
add_library(iso_shared OBJECT
	${shared_dir}/common.cpp
	${shared_dir}/platform.cpp
)
target_include_directories(iso_shared PUBLIC ${shared_dir})
target_compile_definitions(iso_shared PUBLIC VERSION="${PROJECT_VERSION}")

## Executables

add_executable(mkpsxiso
	${mkpsxiso_dir}/cdwriter.cpp
	${mkpsxiso_dir}/edcecc.cpp
	${mkpsxiso_dir}/iso.cpp
	${mkpsxiso_dir}/main.cpp
)
target_include_directories(mkpsxiso PUBLIC "miniaudio")
target_link_libraries(mkpsxiso tinyxml2 iso_shared)
if(MINGW)
	target_link_libraries(mkpsxiso "-municode")
endif()

add_executable(dumpsxiso
	${dumpsxiso_dir}/cdreader.cpp
	${dumpsxiso_dir}/main.cpp
)
target_link_libraries(dumpsxiso tinyxml2 iso_shared)
if(MINGW)
	target_link_libraries(dumpsxiso "-municode")
endif()

## Installation

install(TARGETS mkpsxiso dumpsxiso)

include(InstallRequiredSystemLibraries)

## CPack (used by GitHub Actions to package releases)

if(NOT DEFINED CPACK_GENERATOR)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set(CPACK_GENERATOR ZIP DEB RPM)
	else()
		set(CPACK_GENERATOR ZIP)
	endif()
endif()

set(CPACK_VERBATIM_VARIABLES        ON)
set(CPACK_ARCHIVE_THREADS           0)
set(CPACK_PACKAGE_DIRECTORY         ${PROJECT_BINARY_DIR}/packages)
set(CPACK_PACKAGE_NAME              mkpsxiso)
set(CPACK_PACKAGE_VENDOR            Lameguy64)
set(CPACK_PACKAGE_CONTACT           Lameguy64)
set(CPACK_RESOURCE_FILE_README      ${PROJECT_SOURCE_DIR}/README.md)
set(CPACK_RESOURCE_FILE_LICENSE     ${PROJECT_SOURCE_DIR}/LICENSE.md)
#set(CPACK_PACKAGE_ICON              ${PROJECT_SOURCE_DIR}/icon.ico)
set(CPACK_PACKAGE_INSTALL_DIRECTORY mkpsxiso)

set(CPACK_DEBIAN_PACKAGE_DEPENDS  "libc6 (>= 2.28)")
set(CPACK_DEBIAN_PACKAGE_SECTION  devel)
#set(CPACK_RPM_PACKAGE_RELOCATABLE ON)

include(CPack)
