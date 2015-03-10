cmake_minimum_required(VERSION 2.8)

project(synthpod)

# pkg-config
find_package(PkgConfig REQUIRED)
include(CheckIncludeFiles)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})

set(APP_DIR share/synthpod)
set(LIBS "")
#add_definitions("-std=gnu99" "-fdump-rtl-expand")
	
# libuv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})
set(LIBS ${LIBS} ${UV_LDFLAGS})

# elm
pkg_search_module(ELM REQUIRED elementary>=1.8)
include_directories(${ELM_INCLUDE_DIRS})
set(LIBS ${LIBS} ${ELM_LDFLAGS})

# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LILV_LDFLAGS})

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LV2_LDFLAGS})

# synthpod
add_executable(synthpod
	synthpod.c
	app.c
	varchunk.c
	ext_urid.c)
target_link_libraries(synthpod ${LIBS})
target_compile_definitions(synthpod PUBLIC -DAPP_DIR=${APP_DIR})
install(TARGETS synthpod DESTINATION bin)

add_executable(test_varchunk
	test_varchunk.c
	varchunk.c)
target_link_libraries(test_varchunk ${UV_LDFLAGS})
