# mkpsxiso build script (rewritten)
# (C) 2021 spicyjpeg

cmake_minimum_required(VERSION 3.20)

project(
	mkpsxiso
	LANGUAGES    C CXX
	VERSION      2.10
	DESCRIPTION  "PlayStation ISO image maker & dumping tool"
	HOMEPAGE_URL "https://github.com/Lameguy64/mkpsxiso"
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_C_STANDARD           11)
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

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/ghc/include/ghc/filesystem.hpp)
	message(FATAL_ERROR "The ghc directory is empty. Run 'git submodule update --init --recursive' to populate it.")
endif()

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/ThreadPool/ThreadPool.h)
	message(FATAL_ERROR "The ThreadPool directory is empty. Run 'git submodule update --init --recursive' to populate it.")
endif()

# Build tinyxml2. I didn't bother with tinyxml2's actual CMake integration
# because it's far easier do do this. It is a single-file library after all.
add_library               (tinyxml2 STATIC tinyxml2/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC tinyxml2)

# Build libFLAC, unless explicitly requested not too
if(NOT MKPSXISO_NO_LIBFLAC)
	if(NOT EXISTS ${PROJECT_SOURCE_DIR}/flac/include/FLAC/stream_encoder.h)
		message(FATAL_ERROR "The flac directory is empty. Run 'git submodule update --init --recursive' to populate it.")
	endif()

	set(BUILD_CXXLIBS OFF CACHE INTERNAL "")
	set(BUILD_PROGRAMS OFF CACHE INTERNAL "")
	set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
	set(BUILD_DOCS OFF CACHE INTERNAL "")
	set(BUILD_TESTING OFF CACHE INTERNAL "")
	set(INSTALL_MANPAGES OFF CACHE INTERNAL "")
	set(INSTALL_PKGCONFIG_MODULES OFF CACHE INTERNAL "")
	set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE INTERNAL "")
	set(WITH_OGG OFF CACHE INTERNAL "")
	if(MINGW)
		set(WITH_STACK_PROTECTOR, OFF CACHE INTERNAL "")
	endif()
	add_subdirectory(flac EXCLUDE_FROM_ALL)
endif()

# Add ghc to support filesystem on MacOS
add_subdirectory(ghc EXCLUDE_FROM_ALL)

## Internal dependencies

# Populate shared files
add_library(iso_shared OBJECT
	${shared_dir}/common.cpp
	${shared_dir}/fs.cpp
	${shared_dir}/mmappedfile.cpp
	${shared_dir}/platform.cpp
)
target_include_directories(iso_shared PUBLIC ${shared_dir})
target_compile_definitions(iso_shared PUBLIC VERSION="${PROJECT_VERSION}")
target_link_libraries(iso_shared ghc_filesystem)

find_package(Threads REQUIRED)

## Executables

add_executable(mkpsxiso
	${mkpsxiso_dir}/cdwriter.cpp
	${mkpsxiso_dir}/edcecc.cpp
	${mkpsxiso_dir}/iso.cpp
	${mkpsxiso_dir}/main.cpp
)
target_include_directories(mkpsxiso PUBLIC "miniaudio" "ThreadPool")
target_link_libraries(mkpsxiso tinyxml2 iso_shared Threads::Threads)
if(MINGW)
	target_link_libraries(mkpsxiso "-municode")
endif()

add_executable(dumpsxiso
	${dumpsxiso_dir}/cdreader.cpp
	${dumpsxiso_dir}/cue.cpp
	${dumpsxiso_dir}/main.cpp
)
target_link_libraries(dumpsxiso tinyxml2 iso_shared)
if(NOT MKPSXISO_NO_LIBFLAC)
	target_link_libraries(dumpsxiso FLAC)
else()
	target_compile_definitions(dumpsxiso PUBLIC MKPSXISO_NO_LIBFLAC)
endif()
if(MINGW)
	target_link_libraries(dumpsxiso "-municode")
endif()

## Installation

include(GNUInstallDirs)
if (WIN32)
	set(CMAKE_INSTALL_BINDIR $<IF:$<CONFIG:Release>,.,${CMAKE_INSTALL_BINDIR}>)
	set(CMAKE_INSTALL_DATADIR $<IF:$<CONFIG:Release>,doc,${CMAKE_INSTALL_DOCDIR}>)
else()
	set(CMAKE_INSTALL_DATADIR ${CMAKE_INSTALL_DOCDIR})
endif()
install(TARGETS dumpsxiso mkpsxiso RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/examples/example.xml ${PROJECT_SOURCE_DIR}/LICENSE.md ${PROJECT_SOURCE_DIR}/README.md DESTINATION ${CMAKE_INSTALL_DATADIR})

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
