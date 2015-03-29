cmake_minimum_required(VERSION 2.8)

project(synthpod)

# pkg-config
find_package(PkgConfig REQUIRED)
include(CheckIncludeFiles)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/cjson)
include_directories(${PROJECT_BINARY_DIR})

set(APP_DIR share/synthpod)
#set(LIBS "")
#add_definitions("-std=gnu99" "-fdump-rtl-expand")

# eina
pkg_search_module(EINA REQUIRED eina>=1.8)
include_directories(${EINA_INCLUDE_DIRS})
	
# libuv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

option(BUILD_UI OFF)
if(BUILD_UI)
# elm
	pkg_search_module(ELM REQUIRED elementary>=1.8)
	include_directories(${ELM_INCLUDE_DIRS})
endif()

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

add_subdirectory(lib)
add_subdirectory(bin)
add_subdirectory(plugins)
add_subdirectory(ext)
