include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
		
add_definitions("-D_GNU_SOURCE=1") # asprintf

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c
	synthpod_app_state.c
	synthpod_app_worker.c
	synthpod_app_ui.c
	synthpod_app_mod.c
	synthpod_app_port.c)
target_link_libraries(synthpod.lib
	m
	pthread
	${ECORE_LDFLAGS}
	${ECORE_FILE_LDFLAGS}
	${EFREET_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${SRATOM_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES
	OUTPUT_NAME "synthpod"
	#VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod.lib DESTINATION lib)
