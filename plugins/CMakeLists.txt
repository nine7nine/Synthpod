include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../bin)
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_writer.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

if(WIN32)
	set(LIB_EXT ".dll")
elseif(APPLE)
	set(LIB_EXT ".so")
else()
	set(LIB_EXT ".so")
endif()

# synthpod
add_library(synthpod.lv2 MODULE
	synthpod_lv2.c
	synthpod_stereo.c
	synthpod_keyboard.c
	synthpod_cv2control.c
	synthpod_control2cv.c
	synthpod_cv2atom.c
	synthpod_atom2cv.c
	synthpod_audioxfademono.c
	synthpod_audioxfadestereo.c
	synthpod_atom2control.c
	synthpod_control2atom.c
	synthpod_mixer.c)
target_link_libraries(synthpod.lv2 synthpod.lib)
set_target_properties(synthpod.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

if((DEFINED ECORE_X_FOUND) AND ((${ELM_VERSION} VERSION_EQUAL "1.13.0") OR (${ELM_VERSION} VERSION_GREATER "1.13.0")))
	message(STATUS "X11 UI wrap enabled")
	set(X11_UI_WRAP "")
	add_definitions("-DX11_UI_WRAP")
else()
	message(STATUS "X11 UI wrap disabled")
	set(X11_UI_WRAP "#")
endif()

set(EO_UI_WRAP "")
set(UI_UI_WRAP "")
set(KX_UI_WRAP "")

# synthpod_ui
add_library(synthpod_ui.lv2 MODULE
	synthpod_lv2_ui.c
	synthpod_common_ui.c
	synthpod_keyboard_ui.c)
target_link_libraries(synthpod_ui.lv2 synthpod_ui.lib)
set_target_properties(synthpod_ui.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_ui")
install(TARGETS synthpod_ui.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})
