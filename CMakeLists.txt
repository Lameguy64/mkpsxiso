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

add_executable(dumpsxiso
	${dumpsxiso_dir}/cdreader.cpp
	${dumpsxiso_dir}/main.cpp
)
target_link_libraries(dumpsxiso tinyxml2 iso_shared)

install(TARGETS mkpsxiso dumpsxiso)

include(InstallRequiredSystemLibraries)
