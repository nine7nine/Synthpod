include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/cjson)
		
add_definitions("-D_GNU_SOURCE=1") # asprintf

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c
	cjson/cJSON.c)
target_link_libraries(synthpod.lib
	${EINA_LDFLAGS}
	${ECORE_LDFLAGS}
	${ECORE_CON_LDFLAGS}
	${ECORE_FILE_LDFLAGS}
	${EFREET_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lib DESTINATION lib)

if(BUILD_UI)
# synthpod_ui
	add_library(synthpod_ui.lib SHARED
		synthpod_ui.c
		patcher.c
		smart_slider.c
		smart_meter.c
		smart_spinner.c
		smart_toggle.c)
	target_link_libraries(synthpod_ui.lib
		${ELM_LDFLAGS}
		${LILV_LDFLAGS}
		${LV2_LDFLAGS})
	set_target_properties(synthpod_ui.lib PROPERTIES OUTPUT_NAME "synthpod_ui")
	install(TARGETS synthpod_ui.lib DESTINATION lib)
endif()
