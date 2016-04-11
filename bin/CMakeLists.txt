include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/sandbox_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/libosc) # osc.h
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/jackey)
include_directories(${PROJECT_SOURCE_DIR}/symap)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

add_definitions("-D_GNU_SOURCE=1") # asprintf

set(SYNTHPOD_BIN_DIR "bin")
set(SYNTHPOD_ICON_DIR "share/icons/hicolor/256x256/apps")
set(SYNTHPOD_DESKTOP_DIR "share/applications")

if(BUILD_JACK OR BUILD_ALSA OR BUILD_DUMMY)
# icon
	install(FILES ${PROJECT_SOURCE_DIR}/data/pix/synthpod.png DESTINATION
		${SYNTHPOD_ICON_DIR})
endif()

if(BUILD_JACK)
# jack
	pkg_search_module(JACK REQUIRED jack)
	include_directories(${JACK_INCLUDE_DIRS})

# metadata
	include(CheckIncludeFiles)
	CHECK_INCLUDE_FILES("jack/metadata.h" JACK_HAS_METADATA_API)
	if(${JACK_HAS_METADATA_API})
		add_definitions("-DJACK_HAS_METADATA_API")
	endif()
	
	include(CheckLibraryExists)
	CHECK_LIBRARY_EXISTS(jack jack_get_cycle_times "" JACK_HAS_CYCLE_TIMES)
	if(${JACK_HAS_CYCLE_TIMES})
		add_definitions("-DJACK_HAS_CYCLE_TIMES")
	endif()

	if(APPLE)
		set(JACK_LIBS ${CORE_AUDIO} ${CORE_SERVICES} ${AUDIO_UNIT} "-ljack")
	else()
		set(JACK_LIBS ${JACK_LDFLAGS})
	endif()

# synthpod
	add_executable(synthpod.jack
		synthpod_jack.c
		synthpod_nsm.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.jack
		${ECORE_CON_LDFLAGS}
		${JACK_LIBS}
		"-lm"
		synthpod.lib
		synthpod_ui.lib)
	set_target_properties(synthpod.jack PROPERTIES OUTPUT_NAME "synthpod_jack")
	install(TARGETS synthpod.jack DESTINATION bin)

# desktop  file
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_jack.desktop.in
		${PROJECT_BINARY_DIR}/bin/synthpod_jack.desktop @ONLY)
	install(FILES ${PROJECT_BINARY_DIR}/bin/synthpod_jack.desktop DESTINATION
		${SYNTHPOD_DESKTOP_DIR})

# man page 
	add_custom_command(
		OUTPUT ${PROJECT_BINARY_DIR}/synthpod_jack.1.gz
		COMMAND ${GZIP} ARGS
		"-c9" ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_jack.1
			">" ${PROJECT_BINARY_DIR}/synthpod_jack.1.gz
		DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/synthpod_jack.1)
	add_custom_target(SYNTHPOD_JACK_MAN_GZ ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod_jack.1.gz)
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_jack.1.gz" DESTINATION share/man/man1)
endif()

if(BUILD_ALSA)
# zita_alsa_pcmi
	find_library(ZITA_ALSA_PCMI_LIBRARY NAMES zita-alsa-pcmi)
	find_path(ZITA_ALSA_PCMI_INCLUDE_DIR zita-alsa-pcmi.h)
	if(NOT (ZITA_ALSA_PCMI_LIBRARY AND ZITA_ALSA_PCMI_INCLUDE_DIR))
		message(FATAL_ERROR "zita-alsa-pcmi not found")
	endif()

	include_directories(${ZITA_ALSA_PCMI_INCLUDE_DIR})

# ALSA
	pkg_search_module(ALSA REQUIRED alsa>=1.0)
	include_directories(${ALSA_INCLUDE_DIRS})

# synthpod
	add_executable(synthpod.alsa
		pcmi.cpp
		synthpod_alsa.c
		synthpod_nsm.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.alsa
		${ECORE_CON_LDFLAGS}
		"-lasound" #FIXME
		${ZITA_ALSA_PCMI_LIBRARY}
		"-lm"
		synthpod.lib
		synthpod_ui.lib)
	set_target_properties(synthpod.alsa PROPERTIES OUTPUT_NAME "synthpod_alsa")
	install(TARGETS synthpod.alsa DESTINATION bin)

# desktop  file
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_alsa.desktop.in
		${PROJECT_BINARY_DIR}/bin/synthpod_alsa.desktop @ONLY)
	install(FILES ${PROJECT_BINARY_DIR}/bin/synthpod_alsa.desktop DESTINATION
		${SYNTHPOD_DESKTOP_DIR})

# man page 
	add_custom_command(
		OUTPUT ${PROJECT_BINARY_DIR}/synthpod_alsa.1.gz
		COMMAND ${GZIP} ARGS
		"-c9" ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_alsa.1
			">" ${PROJECT_BINARY_DIR}/synthpod_alsa.1.gz
		DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/synthpod_alsa.1)
	add_custom_target(SYNTHPOD_ALSA_MAN_GZ ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod_alsa.1.gz)
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_alsa.1.gz" DESTINATION share/man/man1)
endif()

if(BUILD_DUMMY)
# synthpod
	add_executable(synthpod.dummy
		synthpod_dummy.c
		synthpod_nsm.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.dummy
		${ECORE_CON_LDFLAGS}
		"-lm"
		synthpod.lib
		synthpod_ui.lib)
	set_target_properties(synthpod.dummy PROPERTIES OUTPUT_NAME "synthpod_dummy")
	install(TARGETS synthpod.dummy DESTINATION bin)

# desktop  file
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_dummy.desktop.in
		${PROJECT_BINARY_DIR}/bin/synthpod_dummy.desktop @ONLY)
	install(FILES ${PROJECT_BINARY_DIR}/bin/synthpod_dummy.desktop DESTINATION
		${SYNTHPOD_DESKTOP_DIR})

# man page 
	add_custom_command(
		OUTPUT ${PROJECT_BINARY_DIR}/synthpod_dummy.1.gz
		COMMAND ${GZIP} ARGS
		"-c9" ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_dummy.1
			">" ${PROJECT_BINARY_DIR}/synthpod_dummy.1.gz
		DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/synthpod_dummy.1)
	add_custom_target(SYNTHPOD_DUMMY_MAN_GZ ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod_dummy.1.gz)
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_dummy.1.gz" DESTINATION share/man/man1)
endif()

if(BUILD_SANDBOX_X11)
	pkg_search_module(XCB REQUIRED xcb>=1.10) # XCB

	add_executable(synthpod.sandbox.x11
		synthpod_sandbox_x11.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.x11
		${XCB_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	target_include_directories(synthpod.sandbox.x11 PUBLIC ${XCB_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.x11 PROPERTIES OUTPUT_NAME "synthpod_sandbox_x11")
	install(TARGETS synthpod.sandbox.x11 DESTINATION bin)
endif()

if(BUILD_SANDBOX_GTK2)
	pkg_search_module(GTK2 REQUIRED gtk+-2.0) # gtk+-2.0

	add_executable(synthpod.sandbox.gtk2
		synthpod_sandbox_gtk.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.gtk2
		${GTK2_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	target_include_directories(synthpod.sandbox.gtk2 PUBLIC ${GTK2_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.gtk2 PROPERTIES OUTPUT_NAME "synthpod_sandbox_gtk2")
	install(TARGETS synthpod.sandbox.gtk2 DESTINATION bin)
endif()

if(BUILD_SANDBOX_GTK3)
	pkg_search_module(GTK3 REQUIRED gtk+-3.0) # gtk+-3.0

	add_executable(synthpod.sandbox.gtk3
		synthpod_sandbox_gtk.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.gtk3
		${GTK3_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	target_include_directories(synthpod.sandbox.gtk3 PUBLIC ${GTK3_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.gtk3 PROPERTIES OUTPUT_NAME "synthpod_sandbox_gtk3")
	install(TARGETS synthpod.sandbox.gtk3 DESTINATION bin)
endif()

if(BUILD_SANDBOX_QT4)
	pkg_search_module(QT4CORE REQUIRED QtCore) # Qt4Core
	pkg_search_module(QT4GUI REQUIRED QtGui) # Qt4Gui

	add_executable(synthpod.sandbox.qt4
		synthpod_sandbox_qt.cpp
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.qt4
		${QT4CORE_LDFLAGS}
		${QT4GUI_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	target_include_directories(synthpod.sandbox.qt4 PUBLIC ${QT4CORE_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt4 PUBLIC ${QT4GUI_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES COMPILE_DEFINITIONS "SYNTHPOD_SANDBOX_QT=4")
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES OUTPUT_NAME "synthpod_sandbox_qt4")
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES COMPILE_FLAGS -fPIC)
	install(TARGETS synthpod.sandbox.qt4 DESTINATION bin)
endif()

if(BUILD_SANDBOX_QT5)
	pkg_search_module(QT5CORE REQUIRED Qt5Core) # Qt5Core
	pkg_search_module(QT5WIDGETS REQUIRED Qt5Widgets) # Qt5Widgets
	pkg_search_module(QT5GUI REQUIRED Qt5Gui) # Qt5Gui

	add_executable(synthpod.sandbox.qt5
		synthpod_sandbox_qt.cpp
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.qt5
		${QT5CORE_LDFLAGS}
		${QT5WIDGETS_LDFLAGS}
		${QT5GUI_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5CORE_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5WIDGETS_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5GUI_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES COMPILE_DEFINITIONS "SYNTHPOD_SANDBOX_QT=5")
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES OUTPUT_NAME "synthpod_sandbox_qt5")
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES COMPILE_FLAGS -fPIC)
	install(TARGETS synthpod.sandbox.qt5 DESTINATION bin)
endif()

if(BUILD_SANDBOX_EFL)
	add_executable(synthpod.sandbox.efl
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_efl.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.efl
		${ELM_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	set_target_properties(synthpod.sandbox.efl PROPERTIES OUTPUT_NAME "synthpod_sandbox_efl")
	install(TARGETS synthpod.sandbox.efl DESTINATION bin)
endif()

if(BUILD_SANDBOX_SHOW)
	add_executable(synthpod.sandbox.show
		synthpod_sandbox_show.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.show
		${ELM_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	set_target_properties(synthpod.sandbox.show PROPERTIES OUTPUT_NAME "synthpod_sandbox_show")
	install(TARGETS synthpod.sandbox.show DESTINATION bin)
endif()

if(BUILD_SANDBOX_KX)
	add_executable(synthpod.sandbox.kx
		synthpod_sandbox_kx.c
		${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_slave.c
		${PROJECT_SOURCE_DIR}/symap/symap.c)
	target_link_libraries(synthpod.sandbox.kx
		${ELM_LDFLAGS}
		${NANOMSG_LDFLAGS}
		${LILV_LDFLAGS})
	set_target_properties(synthpod.sandbox.kx PROPERTIES OUTPUT_NAME "synthpod_sandbox_kx")
	install(TARGETS synthpod.sandbox.kx DESTINATION bin)
endif()

if(BUILD_SANDBOX_X11 OR BUILD_SANDBOX_EFL
OR BUILD_SANDBOX_GTK2 OR BUILD_SANDBOX_GTK3
OR BUILD_SANDBOX_QT4 OR BUILD_SANDBOX_QT5)
# man page 
	add_custom_command(
		OUTPUT ${PROJECT_BINARY_DIR}/synthpod_sandbox.1.gz
		COMMAND ${GZIP} ARGS
		"-c9" ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_sandbox.1
			">" ${PROJECT_BINARY_DIR}/synthpod_sandbox.1.gz
		DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/synthpod_sandbox.1)
	add_custom_target(SYNTHPOD_SANDBOX_MAN_GZ ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod_sandbox.1.gz)
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_sandbox.1.gz" DESTINATION share/man/man1)
endif()
