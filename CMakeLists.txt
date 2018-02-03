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
include_directories(${PROJECT_SOURCE_DIR}/app)
include_directories(${PROJECT_SOURCE_DIR}/ardour.lv2)
include_directories(${PROJECT_SOURCE_DIR}/bin)
include_directories(${PROJECT_SOURCE_DIR}/bundle)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/jackey)
include_directories(${PROJECT_SOURCE_DIR}/libosc_stream)
include_directories(${PROJECT_SOURCE_DIR}/mapper.lv2)
include_directories(${PROJECT_SOURCE_DIR}/netatom.lv2)
include_directories(${PROJECT_SOURCE_DIR}/nk_pugl)
include_directories(${PROJECT_SOURCE_DIR}/nuklear)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/plugins)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/pugl)
include_directories(${PROJECT_SOURCE_DIR}/sandbox_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/ui)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/lfrtm)
include_directories(${PROJECT_SOURCE_DIR}/cross_clock)

if(${CMAKE_VERSION} VERSION_LESS "3.9.0")
	message(WARNING "Please consider to switch to CMake 3.9.0")
	set(IPO_SUPPORTED NO)
else()
	cmake_minimum_required(VERSION 3.9)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT IPO_SUPPORTED)
	message(STATUS "Link time optimizations enabled: ${IPO_SUPPORTED}")
endif()

set(SYNTHPOD_BIN_DIR "bin")
add_definitions("-DSYNTHPOD_BIN_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_BIN_DIR}/\"")

set(SYNTHPOD_DATA_DIR "share/synthpod")
add_definitions("-DSYNTHPOD_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_DATA_DIR}/\"")

set(SYNTHPOD_BUNDLE_DIR "lib/synthpod/lv2")
add_definitions("-DSYNTHPOD_BUNDLE_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_BUNDLE_DIR}/\"")

set(SYNTHPOD_PLUGIN_DIR "lib/lv2/synthpod.lv2")
add_definitions("-DSYNTHPOD_PLUGIN_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_PLUGIN_DIR}/\"")

set(SYNTHPOD_ICON_DIR "share/icons/hicolor/256x256/apps")
add_definitions("-DSYNTHPOD_ICON_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_ICON_DIR}/\"")

set(SYNTHPOD_DESKTOP_DIR "share/applications")
add_definitions("-DSYNTHPOD_DESKTOP_DIR=\"${CMAKE_INSTALL_PREFIX}/${SYNTHPOD_DESKTOP_DIR}/\"")

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

add_definitions("-D_GNU_SOURCE=1") # asprintf

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
option(BUILD_ALSA "Build ALSA standalone host" ON)
option(BUILD_DUMMY "Build DUMMY standalone host" OFF)

option(BUILD_UI "Build graphical user interface(s)" ON)

option(USE_DYNAMIC_PARALLELIZER "Use dynamic scheduler for parallelization" ON)
if(USE_DYNAMIC_PARALLELIZER)
	add_definitions("-DUSE_DYNAMIC_PARALLELIZER")
endif()

include(CMakeDependentOption)

pkg_search_module(XCB OPTIONAL xcb>=1.10) # XCB
pkg_search_module(XCB_ICCCM OPTIONAL xcb-icccm>=0.4) # XCB-ICCCM
if(XCB_FOUND AND XCB_ICCCM_FOUND)
	cmake_dependent_option(BUILD_SANDBOX_X11 "Build X11 sandbox" ON "BUILD_UI" OFF)
else()
	cmake_dependent_option(BUILD_SANDBOX_X11 "Build X11 sandbox" OFF "BUILD_UI" OFF)
endif()

pkg_search_module(GTK2 OPTIONAL gtk+-2.0) # gtk+-2.0
if(GTK2_FOUND)
	cmake_dependent_option(BUILD_SANDBOX_GTK2 "Build GTK-2 sandbox" ON "BUILD_UI" OFF)
else()
	cmake_dependent_option(BUILD_SANDBOX_GTK2 "Build GTK-2 sandbox" OFF "BUILD_UI" OFF)
endif()

pkg_search_module(GTK3 OPTIONAL gtk+-3.0) # gtk+-3.0
if(GTK3_FOUND)
	cmake_dependent_option(BUILD_SANDBOX_GTK3 "Build GTK-3 sandbox" ON "BUILD_UI" OFF)
else()
	cmake_dependent_option(BUILD_SANDBOX_GTK3 "Build GTK-3 sandbox" OFF "BUILD_UI" OFF)
endif()

pkg_search_module(QT4CORE OPTIONAL QtCore) # Qt4Core
pkg_search_module(QT4GUI OPTIONAL QtGui) # Qt4Gui
if(QT4CORE_FOUND AND QT4GUI_FOUND)
	cmake_dependent_option(BUILD_SANDBOX_QT4 "Build Qt-4 sandbox" ON "BUILD_UI" OFF)
else()
	cmake_dependent_option(BUILD_SANDBOX_QT4 "Build Qt-4 sandbox" OFF "BUILD_UI" OFF)
endif()

pkg_search_module(QT5CORE OPTIONAL Qt5Core) # Qt5Core
pkg_search_module(QT5WIDGETS OPTIONAL Qt5Widgets) # Qt5Widgets
pkg_search_module(QT5GUI OPTIONAL Qt5Gui) # Qt5Gui
if(QT5CORE_FOUND AND QT5WIDGETS_FOUND AND QT5GUI_FOUND)
	cmake_dependent_option(BUILD_SANDBOX_QT5 "Build Qt-5 sandbox" ON "BUILD_UI" OFF)
else()
	cmake_dependent_option(BUILD_SANDBOX_QT5 "Build Qt-5 sandbox" OFF "BUILD_UI" OFF)
endif()

cmake_dependent_option(BUILD_SANDBOX_SHOW "Build show-iface sandbox" ON "BUILD_UI" OFF)
cmake_dependent_option(BUILD_SANDBOX_KX "Build external-ui sandbox" ON "BUILD_UI" OFF)

if(BUILD_SANDBOX_X11)
	add_definitions("-DSANDBOX_X11")
endif()
if(BUILD_SANDBOX_GTK2)
	add_definitions("-DSANDBOX_GTK2")
endif()
if(BUILD_SANDBOX_GTK3)
	add_definitions("-DSANDBOX_GTK3")
endif()
if(BUILD_SANDBOX_QT4)
	add_definitions("-DSANDBOX_QT4")
endif()
if(BUILD_SANDBOX_QT5)
	add_definitions("-DSANDBOX_QT5")
endif()
if(BUILD_SANDBOX_SHOW)
	add_definitions("-DSANDBOX_SHOW")
endif()
if(BUILD_SANDBOX_KX)
	add_definitions("-DSANDBOX_KX")
endif()

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

if(DEFINED STATIC_LILV)
	set(SRATOM_LDFLAGS ${LIBS} ${STATIC_SRATOM} ${STATIC_SORD} ${STATIC_SERD})
	set(LILV_LDFLAGS ${LIBS} ${STATIC_LILV} ${SRATOM_LDFLAGS})
endif()

set(BIN_LIBS
	m rt pthread
	${UV_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${SRATOM_LDFLAGS})

set(SANDBOX_LIBS
	rt pthread
	${LILV_LDFLAGS})

add_subdirectory(app)
add_subdirectory(bin)
add_subdirectory(bundle)
add_subdirectory(data)
add_subdirectory(plugins)
add_subdirectory(sandbox_ui.lv2)
