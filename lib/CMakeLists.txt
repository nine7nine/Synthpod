include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# libuv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

# elm
pkg_search_module(ELM REQUIRED elementary>=1.8)
include_directories(${ELM_INCLUDE_DIRS})

# lilv
pkg_search_module(LILV REQUIRED lilv-0>=0.20)
include_directories(${LILV_INCLUDE_DIRS})

# lv2
pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})

# uuid
pkg_search_module(UUID REQUIRED uuid>=2.25)
include_directories(${UUID_INCLUDE_DIRS})

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c
	${PROJECT_SOURCE_DIR}/ext_urid.c
	${PROJECT_SOURCE_DIR}/varchunk.c)
target_link_libraries(synthpod.lib
	${UV_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${UUID_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lib DESTINATION lib)

# synthpod
add_library(synthpod_ui.lib SHARED
	synthpod_ui.c
	${PROJECT_SOURCE_DIR}/varchunk.c)
target_link_libraries(synthpod_ui.lib
	${ELM_LDFLAGS}
	${UV_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${UUID_LDFLAGS})
set_target_properties(synthpod_ui.lib PROPERTIES OUTPUT_NAME "synthpod_ui")
install(TARGETS synthpod_ui.lib DESTINATION lib)
