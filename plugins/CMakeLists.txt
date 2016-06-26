include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/sandbox_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/symap)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_writer.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

add_definitions("-D_GNU_SOURCE=1") # asprintf

# synthpod
add_library(synthpod.lv2 MODULE
	synthpod_lv2.c
	synthpod_stereo.c
	synthpod_keyboard.c
	synthpod_cv2control.c
	synthpod_control2cv.c
	synthpod_cv2atom.c
	synthpod_atom2cv.c
	synthpod_midisplitter.c
	synthpod_audioxfademono.c
	synthpod_audioxfadestereo.c
	synthpod_atom2control.c
	synthpod_midi2control.c
	synthpod_string2control.c
	synthpod_control2atom.c
	synthpod_mixer.c
	synthpod_panic.c
	synthpod_anonymizer.c
	synthpod_monoatom.c)
target_link_libraries(synthpod.lv2 synthpod.lib)
set_target_properties(synthpod.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_ui.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# synthpod_ui
add_library(synthpod_ui.lv2 MODULE
	${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_ui.c
	${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_master.c
	synthpod_lv2_ui.c)
target_link_libraries(synthpod_ui.lv2
	${NANOMSG_LDFLAGS}
	${SRATOM_LDFLAGS})
set_target_properties(synthpod_ui.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_ui")
install(TARGETS synthpod_ui.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

add_executable(sandbox_efl
	${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_efl.c
	${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
	${PROJECT_SOURCE_DIR}/symap/symap.c)
target_link_libraries(sandbox_efl
	${ELM_LDFLAGS}
	${NANOMSG_LDFLAGS}
	${LILV_LDFLAGS})
install(TARGETS sandbox_efl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# synthpod_eo
add_library(synthpod_eo.lv2 MODULE
	synthpod_lv2_eo.c
	synthpod_common_eo.c
	synthpod_keyboard_eo.c)
target_link_libraries(synthpod_eo.lv2 synthpod_ui.lib)
set_target_properties(synthpod_eo.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_eo")
install(TARGETS synthpod_eo.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})
