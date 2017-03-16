include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/sandbox_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/mapper.lv2)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_writer.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/pugl)
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
	synthpod_heavyload.c
	synthpod_audioxfademono.c
	synthpod_audioxfadestereo.c
	synthpod_atom2control.c
	synthpod_midi2control.c
	synthpod_string2control.c
	synthpod_control2atom.c
	synthpod_mixer.c
	synthpod_panic.c
	synthpod_anonymizer.c)
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
	${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c)
target_link_libraries(sandbox_efl
	${ELM_LDFLAGS}
	${NANOMSG_LDFLAGS}
	${LILV_LDFLAGS})
install(TARGETS sandbox_efl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# synthpod_eo
add_library(synthpod_eo.lv2 MODULE
	synthpod_lv2_eo.c
	synthpod_common_eo.c)
target_link_libraries(synthpod_eo.lv2 synthpod_ui.lib)
set_target_properties(synthpod_eo.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_eo")
install(TARGETS synthpod_eo.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# nk_pugl
if(WIN32)
	set(NK_UI_TYPE "WindowsUI")
elseif(APPLE)
	set(NK_UI_TYPE "CocoaUI")
else()
	set(NK_UI_TYPE "X11UI")
endif()
set(LIBS_NK m)

find_package(OpenGL)
if(${OPENGL_FOUND})
	set(LIBS_NK ${LIBS_NK} ${OPENGL_LIBRARIES})
else() # try pkg-config
	pkg_search_module(GL REQUIRED gl)
	if(${GL_FOUND})
		set(LIBS_NK ${LIBS_NK} ${GL_LDFLAGS})
	else()
		message(FATAL_ERROR "OpenGL not found")
	endif()
endif()
add_definitions("-DPUGL_HAVE_GL")

if(WIN32)
	find_library(GDI32_LIBRARY NAMES gdi32)
	if(GDI32_LIBRARY)
		set(LIBS_NK ${LIBS_NK} ${GDI32_LIBRARY})
	else()
		message(FATAL_ERROR "gdi32 library not found")
	endif()

	find_library(USER32_LIBRARY NAMES user32)
	if(USER32_LIBRARY)
		set(LIBS_NK ${LIBS_NK} ${USER32_LIBRARY})
	else()
		message(FATAL_ERROR "user32 library not found")
	endif()

	set(TAR_NK ${TAR_NK} ${PROJECT_SOURCE_DIR}/pugl/pugl/pugl_win.cpp)

elseif(APPLE)
	find_library(COCOA_LIBRARY NAMES Cocoa)
	if(COCOA_LIBRARY)
		set(LIBS_NK ${LIBS_NK} ${COCOA_LIBRARY})
	else()
		message(FATAL_ERROR "Cocoa framework not found")
	endif()

	set(TAR_NK ${TAR_NK} ${PROJECT_SOURCE_DIR}pugl/pugl/pugl_osx.m)

else() # GNU/Linux
	pkg_search_module(X11 REQUIRED x11>=1.6)
	include_directories(${X11_INCLUDE_DIRS})
	set(LIBS_NK ${LIBS_NK} ${X11_LDFLAGS})

	pkg_search_module(XEXT REQUIRED xext>=1.3)
	include_directories(${XEXT_INCLUDE_DIRS})
	set(LIBS_NK ${LIBS_NK} ${XEXT_LDFLAGS})

	set(TAR_NK ${TAR_NK} ${PROJECT_SOURCE_DIR}/pugl/pugl/pugl_x11.c)
endif()

# synthpod_nk
add_library(synthpod_nk.lv2 MODULE
	synthpod_lv2_nk.c
	synthpod_common_nk.c
	synthpod_keyboard_nk.c
	${TAR_NK})
target_link_libraries(synthpod_nk.lv2
	${LIBS_NK}
	${LILV_LDFLAGS})
set_target_properties(synthpod_nk.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_nk")
install(TARGETS synthpod_nk.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# font
install(FILES ${PROJECT_SOURCE_DIR}/nuklear/extra_font/Cousine-Regular.ttf DESTINATION ${SYNTHPOD_PLUGIN_DIR})
