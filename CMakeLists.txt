cmake_minimum_required(VERSION 2.8)

project(synthpod)

# pkg-config
find_package(PkgConfig REQUIRED)
include(CheckIncludeFiles)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/cjson)
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

# uuid
pkg_search_module(UUID REQUIRED uuid>=2.25)
include_directories(${UUID_INCLUDE_DIRS})
set(LIBS ${LIBS} ${UUID_LDFLAGS})

# synthpod
add_executable(synthpod
	synthpod.c
	app.c
	varchunk.c
	patcher.c
	ext_urid.c
	cjson/cJSON.c)
target_link_libraries(synthpod ${LIBS})
target_compile_definitions(synthpod PUBLIC -DAPP_DIR=${APP_DIR})
install(TARGETS synthpod DESTINATION bin)

add_executable(test_varchunk
	test_varchunk.c
	varchunk.c)
target_link_libraries(test_varchunk ${UV_LDFLAGS})
	
find_program(EDJE_CC NAMES edje_cc)
if(EDJE_CC_NOTFOUND)
	message(SEND_ERROR "edje_cc not found")
else(EDJE_CC_NOTFOUND)
	message(STATUS "edje_cc found: " ${EDJE_CC})
endif(EDJE_CC_NOTFOUND)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/synthpod.edj
	COMMAND ${EDJE_CC} ARGS
		"-fd" ${PROJECT_SOURCE_DIR}/font
		"-id" ${PROJECT_SOURCE_DIR}/pix
		${PROJECT_SOURCE_DIR}/synthpod.edc
		${PROJECT_BINARY_DIR}/synthpod.edj
	DEPENDS
		${PROJECT_SOURCE_DIR}/synthpod.edc
		${PROJECT_SOURCE_DIR}/patcher.edc)
add_custom_target(THEME ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod.edj)
install(FILES ${PROJECT_BINARY_DIR}/synthpod.edj DESTINATION ${APP_DIR})

add_subdirectory(plugins)
add_subdirectory(ext)
