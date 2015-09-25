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

set(SYNTHPOD_DATA_DIR "share/synthpod")
add_definitions("-DSYNTHPOD_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_DATA_DIR}\"")

set(SYNTHPOD_BUNDLE_DIR "lib/synthpod")
add_definitions("-DSYNTHPOD_BUNDLE_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_BUNDLE_DIR}\"")

set(SYNTHPOD_PLUGIN_DIR "lib/lv2/synthpod.lv2")
add_definitions("-DSYNTHPOD_PLUGIN_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_PLUGIN_DIR}\"")

set(SYNTHPOD_MAJOR_VERSION 0)
set(SYNTHPOD_MINOR_VERSION 1)
set(SYNTHPOD_MICRO_VERSION 1)
set(SYNTHPOD_VERSION "${SYNTHPOD_MAJOR_VERSION}.${SYNTHPOD_MINOR_VERSION}.${SYNTHPOD_MICRO_VERSION}")
add_definitions("-DSYNTHPOD_VERSION=\"${SYNTHPOD_VERSION}\"")

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")

set(CMAKE_CXX_FLAGS "-std=gnu++11 -Wextra -ffast-math -fvisibility=hidden ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "-Wshadow -Wredundant-decls -Weffc++ ${CMAKE_CXX_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-z,relro ${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,relro -Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,relro ${CMAKE_SHARED_LINKER_FLAGS}")

# options
option(BUILD_JACK "Build JACK standalone host" ON)
option(BUILD_ALSA "Build ALSA standalone host (experimental)" OFF)

# vectorization
include(CheckCCompilerFlag)
CHECK_C_COMPILER_FLAG("-ftree-vectorize" CAN_VECTORIZE)
if(${CAN_VECTORIZE})
	set(CMAKE_C_FLAGS "-ftree-vectorize ${CMAKE_C_FLAGS}")
	#set(CMAKE_C_FLAGS "-ftree-vectorizer-verbose=1 ${CMAKE_C_FLAGS}") # GCC
	#set(CMAKE_C_FLAGS "-Rpass=loop-vectorize ${CMAKE_C_FLAGS}") # CLANG
endif()

# eina
pkg_search_module(EINA REQUIRED eina>=1.8)
include_directories(${EINA_INCLUDE_DIRS})

# efreet
pkg_search_module(EFREET REQUIRED efreet>=1.8)
include_directories(${EFREET_INCLUDE_DIRS})

# ecore
pkg_search_module(ECORE REQUIRED ecore>=1.8)
include_directories(${ECORE_INCLUDE_DIRS})

# ecore-con
pkg_search_module(ECORE_CON REQUIRED ecore-con>=1.8)
include_directories(${ECORE_CON_INCLUDE_DIRS})

# ecore-file
pkg_search_module(ECORE_FILE REQUIRED ecore-file>=1.8)
include_directories(${ECORE_FILE_INCLUDE_DIRS})
	
# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

# elm
pkg_search_module(ELM REQUIRED elementary>=1.8)
include_directories(${ELM_INCLUDE_DIRS})

if((${ELM_VERSION} VERSION_EQUAL "1.10.0") OR (${ELM_VERSION} VERSION_GREATER "1.10.0"))
	add_definitions("-DELM_1_10")
endif()

pkg_search_module(ECORE_X OPTIONAL ecore-x)

add_subdirectory(lib)
add_subdirectory(bin)
add_subdirectory(plugins)
add_subdirectory(data)
add_subdirectory(state)
add_subdirectory(bundle)

# CPack
set(CPACK_GENERATOR "TBZ2;DEB")

set(CPACK_PACKAGE_NAME "synthpod")
set(CPACK_PACKAGE_VERSION "${SYNTHPOD_VERSION}")
SET(CPACK_PACKAGE_VERSION_MAJOR ${SYNTHPOD_MAJOR_VERSION})
SET(CPACK_PACKAGE_VERSION_MINOR ${SYNTHPOD_MINOR_VERSION})
SET(CPACK_PACKAGE_VERSION_PATCH ${SYNTHPOD_PATCH_VERSION})
set(CPACK_PACKAGE_CONTACT "Hanspeter Portner <dev@open-music-kontrollers.ch>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Non-linear plugin container")
set(CPACK_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY})
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
SET(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
SET(CPACK_STRIP_FILES ON)

# Debian
set(CPACK_DEBIAN_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
set(CPACK_DEBIAN_PACKAGE_VERSION ${CPACK_PACKAGE_VERSION})
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

set(CPACK_DEBIAN_PACKAGE_BUILD_DEPENDS "cmake, libasound2-dev, libelementary-dev, liblilv-dev, libjack-dev, libzita-alsa-pcmi-dev, lv2-dev")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libasound2, libelementary2, libevas1-engines-x, liblilv-0-0, jackd, libzita-alsa-pcmi0")
set(CPACK_DEBIAN_PACKAGE_PROVIDES "lv2-plugin, lv2-host")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER ${CPACK_PACKAGE_CONTACT})
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://open-music-kontrollers.ch/lv2/moony/")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
SET(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/copyright.in ${PROJECT_BINARY_DIR}/copyright)
install(FILES "${PROJECT_BINARY_DIR}/copyright" DESTINATION "share/doc/${CPACK_DEBIAN_PACKAGE_NAME}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/changelog.in ${PROJECT_BINARY_DIR}/changelog)
add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/changelog.gz
	COMMAND ${GZIP} ARGS
		"-c9" ${PROJECT_BINARY_DIR}/changelog
		">" ${PROJECT_BINARY_DIR}/changelog.gz
	DEPENDS
		${PROJECT_BINARY_DIR}/changelog)
add_custom_target(CHANGELOG ALL DEPENDS ${PROJECT_BINARY_DIR}/changelog.gz)
install(FILES "${PROJECT_BINARY_DIR}/changelog.gz" DESTINATION "share/doc/${CPACK_DEBIAN_PACKAGE_NAME}")

include(CPack)
