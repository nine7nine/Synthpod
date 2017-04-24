cmake_minimum_required(VERSION 2.8)

project(synthpod)

# pkg-config
find_package(PkgConfig REQUIRED)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(FindUnixCommands)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

include_directories(${PROJECT_SOURCE_DIR}/netatom.lv2)

set(SYNTHPOD_BIN_DIR "bin")
add_definitions("-DSYNTHPOD_BIN_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_BIN_DIR}/\"")

set(SYNTHPOD_DATA_DIR "share/synthpod")
add_definitions("-DSYNTHPOD_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_DATA_DIR}/\"")

set(SYNTHPOD_BUNDLE_DIR "lib/synthpod/lv2")
add_definitions("-DSYNTHPOD_BUNDLE_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_BUNDLE_DIR}/\"")

set(SYNTHPOD_PLUGIN_DIR "lib/lv2/synthpod.lv2")
add_definitions("-DSYNTHPOD_PLUGIN_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_PLUGIN_DIR}/\"")

file(STRINGS "VERSION" SYNTHPOD_VERSION)
string(REPLACE "." ";" VERSION_LIST ${SYNTHPOD_VERSION})
list(GET VERSION_LIST 0 SYNTHPOD_MAJOR_VERSION)
list(GET VERSION_LIST 1 SYNTHPOD_MINOR_VERSION)
list(GET VERSION_LIST 2 SYNTHPOD_MICRO_VERSION)
add_definitions("-DSYNTHPOD_MINOR_VERSION=${SYNTHPOD_MINOR_VERSION}")
add_definitions("-DSYNTHPOD_MICRO_VERSION=${SYNTHPOD_MICRO_VERSION}")
add_definitions("-DSYNTHPOD_VERSION=\"${SYNTHPOD_VERSION}\"")

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -Wno-attributes -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")

set(CMAKE_CXX_FLAGS "-std=gnu++11 -Wextra -ffast-math -fvisibility=hidden ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "-Wshadow -Wredundant-decls -Wno-effc++ ${CMAKE_CXX_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-z,relro -Wl,-z,defs ${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,relro -Wl,-z,nodelete -Wl,-z,defs ${CMAKE_MODULE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,relro -Wl,-z,defs ${CMAKE_SHARED_LINKER_FLAGS}")

include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("
	int main(int argc, char **argv)
	{
		void *src;
		void *dst = __builtin_assume_aligned(src, 16);
		return 0;
	}" HAS_BUILTIN_ASSUME_ALIGNED)
if(${HAS_BUILTIN_ASSUME_ALIGNED})
	add_definitions("-DHAS_BUILTIN_ASSUME_ALIGNED")
endif()

# options
option(BUILD_JACK "Build JACK standalone host" ON)
option(BUILD_ALSA "Build ALSA standalone host" OFF)
option(BUILD_DUMMY "Build DUMMY standalone host" OFF)
option(BUILD_DOT "Build Dot exporter" ON)

include(CMakeDependentOption)
option(BUILD_SANDBOX_LIB "Build sandbox library" ON)
cmake_dependent_option(BUILD_SANDBOX_X11 "Build X11 sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_GTK2 "Build GTK-2 sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_GTK3 "Build GTK-3 sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_QT4 "Build Qt-4 sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_QT5 "Build Qt-5 sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_EFL "Build EFL sandbox" ON "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_SHOW "Build show-iface sandbox" OFF "BUILD_SANDBOX_LIB" OFF)
cmake_dependent_option(BUILD_SANDBOX_KX "Build external-ui sandbox" OFF "BUILD_SANDBOX_LIB" OFF)

# vectorization
include(CheckCCompilerFlag)
CHECK_C_COMPILER_FLAG("-ftree-vectorize" CAN_VECTORIZE)
if(${CAN_VECTORIZE})
	set(CMAKE_C_FLAGS "-ftree-vectorize ${CMAKE_C_FLAGS}")
	#set(CMAKE_C_FLAGS "-ftree-vectorizer-verbose=1 ${CMAKE_C_FLAGS}") # GCC
	#set(CMAKE_C_FLAGS "-Rpass=loop-vectorize ${CMAKE_C_FLAGS}") # CLANG
endif()

# atomic operations
include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("
	#include <stdlib.h>
	#include <stdatomic.h>
	int main(int argc, char **argv)
	{
		_Atomic size_t head;
		atomic_init(&head, 0);
		size_t chead = atomic_load_explicit(&head, memory_order_acquire);
		chead += 1;
		atomic_store_explicit(&head, chead, memory_order_release);
		return 0;
	}" HAS_ATOMIC)
if(NOT ${HAS_ATOMIC})
	message(FATAL_ERROR "compiler has no support for C11 atomic operations")
endif()

# uv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

# uuid
pkg_search_module(UUID REQUIRED uuid>=2.0)
include_directories(${UUID_INCLUDE_DIRS})

# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})
if((${LILV_VERSION} VERSION_EQUAL "0.22.0") OR (${LILV_VERSION} VERSION_GREATER "0.22.0"))
	add_definitions("-DLILV_0_22")
endif()

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

# sratom
pkg_search_module(SRATOM REQUIRED sratom-0>=0.4)
include_directories(${SRATOM_INCLUDE_DIRS})

# elm
pkg_search_module(ELM REQUIRED elementary>=1.8)
include_directories(${ELM_INCLUDE_DIRS})

# evas-x11
pkg_search_module(EVAS_OPENGL_X11 OPTIONAL evas-opengl-x11>=1.8)
pkg_search_module(EVAS_SOFTWARE_X11 OPTIONAL evas-software-x11>=1.8)
if( (NOT ${EVAS_OPENGL_X11_FOUND}) AND (NOT ${EVAS_SOFTWARE_X11_FOUND}) )
	message(WARNING "No Evas engine for X11 has been found: UI may not be functional on X11!")
endif()

if((${ELM_VERSION} VERSION_EQUAL "1.10.0") OR (${ELM_VERSION} VERSION_GREATER "1.10.0"))
	add_definitions("-DELM_1_10")
endif()

if((${ELM_VERSION} VERSION_EQUAL "1.13.0") OR (${ELM_VERSION} VERSION_GREATER "1.13.0"))
	add_definitions("-DELM_1_13")
endif()

add_subdirectory(app)
add_subdirectory(ui)
add_subdirectory(bin)
add_subdirectory(plugins)
add_subdirectory(data)
add_subdirectory(bundle)

# CPack
set(CPACK_GENERATOR "TBZ2")

set(CPACK_PACKAGE_NAME "synthpod")
set(CPACK_PACKAGE_VERSION "${SYNTHPOD_VERSION}")
SET(CPACK_PACKAGE_VERSION_MAJOR ${SYNTHPOD_MAJOR_VERSION})
SET(CPACK_PACKAGE_VERSION_MINOR ${SYNTHPOD_MINOR_VERSION})
SET(CPACK_PACKAGE_VERSION_PATCH ${SYNTHPOD_MICRO_VERSION})
set(CPACK_PACKAGE_CONTACT "Hanspeter Portner <dev@open-music-kontrollers.ch>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Non-linear plugin container")
set(CPACK_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
SET(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
SET(CPACK_STRIP_FILES ON)

include(CPack)
