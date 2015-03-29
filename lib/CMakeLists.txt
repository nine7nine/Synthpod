include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c
	${PROJECT_SOURCE_DIR}/ext_urid.c
	${PROJECT_SOURCE_DIR}/varchunk.c)
target_link_libraries(synthpod.lib
	${EINA_LDFLAGS}
	${UV_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${UUID_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lib DESTINATION lib)

if(BUILD_UI)
# synthpod_ui
	add_library(synthpod_ui.lib SHARED
		synthpod_ui.c
		${PROJECT_SOURCE_DIR}/patcher.c
		${PROJECT_SOURCE_DIR}/varchunk.c)
	target_link_libraries(synthpod_ui.lib
		${ELM_LDFLAGS}
		${UV_LDFLAGS}
		${LILV_LDFLAGS}
		${LV2_LDFLAGS}
		${UUID_LDFLAGS})
	set_target_properties(synthpod_ui.lib PROPERTIES OUTPUT_NAME "synthpod_ui")
	install(TARGETS synthpod_ui.lib DESTINATION lib)
endif()
