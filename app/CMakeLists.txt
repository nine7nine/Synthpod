include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
		
add_definitions("-D_GNU_SOURCE=1") # asprintf

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c)
target_link_libraries(synthpod.lib
	${EINA_LDFLAGS}
	${ECORE_LDFLAGS}
	${ECORE_CON_LDFLAGS}
	${ECORE_FILE_LDFLAGS}
	${EFREET_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${SRATOM_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES
	OUTPUT_NAME "synthpod"
	VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod.lib DESTINATION lib)
