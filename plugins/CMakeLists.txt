# synthpod
add_library(synthpod.lv2 MODULE
	synthpod_lv2.c
	synthpod_keyboard.c
	synthpod_cv2control.c
	synthpod_control2cv.c
	synthpod_midisplitter.c
	synthpod_heavyload.c
	synthpod_panic.c
	synthpod_placeholder.c
	synthpod_stereo.c
	$<TARGET_OBJECTS:synthpod.lib>)
target_link_libraries(synthpod.lv2
	${BIN_LIBS})
target_link_libraries(synthpod.lv2 m)
set_target_properties(synthpod.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod")
set_target_properties(synthpod.lv2 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
set_target_properties(synthpod.lv2 PROPERTIES INTERPROCEDURAL_OPTIMIZATION IPO_SUPPORTED) # -flto
install(TARGETS synthpod.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_ui.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})

if(BUILD_UI)
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
		${TAR_NK}
		$<TARGET_OBJECTS:sandbox_master.lib>)
	target_link_libraries(synthpod_nk.lv2
		${LIBS_NK}
		${LILV_LDFLAGS}
		rt pthread)
	set_target_properties(synthpod_nk.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_nk")
	set_target_properties(synthpod_nk.lv2 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod_nk.lv2 PROPERTIES INTERPROCEDURAL_OPTIMIZATION IPO_SUPPORTED) # -flto
	install(TARGETS synthpod_nk.lv2 DESTINATION ${SYNTHPOD_PLUGIN_DIR})

# font
	install(FILES ${PROJECT_SOURCE_DIR}/data/font/Abel-Regular.ttf DESTINATION ${SYNTHPOD_PLUGIN_DIR})
else()
	set(UI_WRAP "#")
endif()

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${SYNTHPOD_PLUGIN_DIR})
