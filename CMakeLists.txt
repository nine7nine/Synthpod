cmake_minimum_required(VERSION 2.8)

project(synthpod)

# pkg-config
find_package(PkgConfig REQUIRED)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckSymbolExists)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

set(APP_DIR share/synthpod)
set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")

# options
option(BUILD_UI "Build user interface libraries" OFF)
option(BUILD_JACK "Build JACK standalone host" OFF)
option(BUILD_PORTAUDIO "Build PortAudio standalone host" OFF)

# eina
pkg_search_module(EINA REQUIRED eina>=1.8)
include_directories(${EINA_INCLUDE_DIRS})

# ecore-con
pkg_search_module(ECORE_CON REQUIRED ecore-con>=1.8)
include_directories(${ECORE_CON_INCLUDE_DIRS})
	
# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

if(BUILD_UI)
	add_definitions("-DBUILD_UI")

# elm
	pkg_search_module(ELM REQUIRED elementary>=1.8)
	include_directories(${ELM_INCLUDE_DIRS})

	pkg_search_module(ECORE_X OPTIONAL ecore-x)
endif()

add_subdirectory(lib)
add_subdirectory(bin)
add_subdirectory(plugins)
add_subdirectory(data)
