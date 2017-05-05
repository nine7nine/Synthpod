if(BUILD_JACK OR BUILD_ALSA OR BUILD_DUMMY)
# icon
	install(FILES ${PROJECT_SOURCE_DIR}/data/pix/synthpod.png DESTINATION
		${SYNTHPOD_ICON_DIR})

	add_library(synthpod.bin OBJECT
		synthpod_bin.c
		synthpod_nsm.c)

	set_target_properties(synthpod.bin PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.bin PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
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
		set(JACK_LIBS ${CORE_AUDIO} ${CORE_SERVICES} ${AUDIO_UNIT} jack)
	else()
		set(JACK_LIBS ${JACK_LDFLAGS})
	endif()

# synthpod
	add_executable(synthpod.jack
		synthpod_jack.c
		$<TARGET_OBJECTS:synthpod.bin>
		$<TARGET_OBJECTS:synthpod.lib>
		$<TARGET_OBJECTS:sandbox_master.lib>)
	target_link_libraries(synthpod.jack
		${JACK_LIBS}
		${BIN_LIBS})
	set_target_properties(synthpod.jack PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.jack PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
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
		$<TARGET_OBJECTS:synthpod.bin>
		$<TARGET_OBJECTS:synthpod.lib>
		$<TARGET_OBJECTS:sandbox_master.lib>)
	target_link_libraries(synthpod.alsa
		asound #FIXME
		${ZITA_ALSA_PCMI_LIBRARY}
		${BIN_LIBS})
	set_target_properties(synthpod.alsa PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.alsa PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
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
		$<TARGET_OBJECTS:synthpod.bin>
		$<TARGET_OBJECTS:synthpod.lib>
		$<TARGET_OBJECTS:sandbox_master.lib>)
	target_link_libraries(synthpod.dummy
		${BIN_LIBS})
	set_target_properties(synthpod.dummy PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.dummy PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
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

if(BUILD_DOT)
# synthpod
	add_executable(synthpod.dot
		synthpod_dot.c)
	target_link_libraries(synthpod.dot
		${SRATOM_LDFLAGS})
	set_target_properties(synthpod.dot PROPERTIES OUTPUT_NAME "synthpod_dot")
	set_target_properties(synthpod.dot PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.dot PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	install(TARGETS synthpod.dot DESTINATION bin)
endif()

if(BUILD_UI AND BUILD_SANDBOX_X11)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_nk.in
		${PROJECT_BINARY_DIR}/bin/synthpod_nk @ONLY)
	install(PROGRAMS ${PROJECT_BINARY_DIR}/bin/synthpod_nk DESTINATION bin)
endif()

if(  BUILD_SANDBOX_X11
	OR BUILD_SANDBOX_GTK2 OR BUILD_SANDBOX_GTK3
	OR BUILD_SANDBOX_QT4  OR BUILD_SANDBOX_QT5
	OR BUILD_SANDBOX_SHOW OR BUILD_SANDBOX_KX)
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

if(BUILD_SANDBOX_X11)
	pkg_search_module(XCB REQUIRED xcb>=1.10) # XCB
	pkg_search_module(XCB_ICCCM REQUIRED xcb-icccm>=0.4) # XCB-ICCCM

	add_executable(synthpod.sandbox.x11
		synthpod_sandbox_x11.c
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.x11
		${XCB_LDFLAGS}
		${XCB_ICCCM_LDFLAGS}
		${SANDBOX_LIBS})
	target_include_directories(synthpod.sandbox.x11 PUBLIC ${XCB_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.x11 PUBLIC ${XCB_ICCCM_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.x11 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.x11 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	set_target_properties(synthpod.sandbox.x11 PROPERTIES OUTPUT_NAME "synthpod_sandbox_x11")
	install(TARGETS synthpod.sandbox.x11 DESTINATION bin)
endif()

if(BUILD_SANDBOX_GTK2)
	pkg_search_module(GTK2 REQUIRED gtk+-2.0) # gtk+-2.0

	add_executable(synthpod.sandbox.gtk2
		synthpod_sandbox_gtk.c
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.gtk2
		${GTK2_LDFLAGS}
		${SANDBOX_LIBS})
	target_include_directories(synthpod.sandbox.gtk2 PUBLIC ${GTK2_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.gtk2 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.gtk2 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	set_target_properties(synthpod.sandbox.gtk2 PROPERTIES OUTPUT_NAME "synthpod_sandbox_gtk2")
	install(TARGETS synthpod.sandbox.gtk2 DESTINATION bin)
endif()

if(BUILD_SANDBOX_GTK3)
	pkg_search_module(GTK3 REQUIRED gtk+-3.0) # gtk+-3.0

	add_executable(synthpod.sandbox.gtk3
		synthpod_sandbox_gtk.c
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.gtk3
		${GTK3_LDFLAGS}
		${SANDBOX_LIBS})
	target_include_directories(synthpod.sandbox.gtk3 PUBLIC ${GTK3_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.gtk3 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.gtk3 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	set_target_properties(synthpod.sandbox.gtk3 PROPERTIES OUTPUT_NAME "synthpod_sandbox_gtk3")
	install(TARGETS synthpod.sandbox.gtk3 DESTINATION bin)
endif()

if(BUILD_SANDBOX_QT4)
	pkg_search_module(QT4CORE REQUIRED QtCore) # Qt4Core
	pkg_search_module(QT4GUI REQUIRED QtGui) # Qt4Gui

	add_executable(synthpod.sandbox.qt4
		synthpod_sandbox_qt.cpp
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.qt4
		${QT4CORE_LDFLAGS}
		${QT4GUI_LDFLAGS}
		${SANDBOX_LIBS})
	target_include_directories(synthpod.sandbox.qt4 PUBLIC ${QT4CORE_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt4 PUBLIC ${QT4GUI_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES COMPILE_DEFINITIONS "SYNTHPOD_SANDBOX_QT=4")
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES OUTPUT_NAME "synthpod_sandbox_qt4")
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.qt4 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	install(TARGETS synthpod.sandbox.qt4 DESTINATION bin)
endif()

if(BUILD_SANDBOX_QT5)
	pkg_search_module(QT5CORE REQUIRED Qt5Core) # Qt5Core
	pkg_search_module(QT5WIDGETS REQUIRED Qt5Widgets) # Qt5Widgets
	pkg_search_module(QT5GUI REQUIRED Qt5Gui) # Qt5Gui

	add_executable(synthpod.sandbox.qt5
		synthpod_sandbox_qt.cpp
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.qt5
		${QT5CORE_LDFLAGS}
		${QT5WIDGETS_LDFLAGS}
		${QT5GUI_LDFLAGS}
		${SANDBOX_LIBS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5CORE_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5WIDGETS_INCLUDE_DIRS})
	target_include_directories(synthpod.sandbox.qt5 PUBLIC ${QT5GUI_INCLUDE_DIRS})
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES COMPILE_DEFINITIONS "SYNTHPOD_SANDBOX_QT=5")
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES OUTPUT_NAME "synthpod_sandbox_qt5")
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.qt5 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	install(TARGETS synthpod.sandbox.qt5 DESTINATION bin)
endif()

if(BUILD_SANDBOX_SHOW)
	add_executable(synthpod.sandbox.show
		synthpod_sandbox_show.c
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.show
		${SANDBOX_LIBS})
	set_target_properties(synthpod.sandbox.show PROPERTIES OUTPUT_NAME "synthpod_sandbox_show")
	set_target_properties(synthpod.sandbox.show PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.show PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	install(TARGETS synthpod.sandbox.show DESTINATION bin)
endif()

if(BUILD_SANDBOX_KX)
	add_executable(synthpod.sandbox.kx
		synthpod_sandbox_kx.c
		$<TARGET_OBJECTS:sandbox_slave.lib>)
	target_link_libraries(synthpod.sandbox.kx
		${SANDBOX_LIBS})
	set_target_properties(synthpod.sandbox.kx PROPERTIES OUTPUT_NAME "synthpod_sandbox_kx")
	set_target_properties(synthpod.sandbox.kx PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
	set_target_properties(synthpod.sandbox.kx PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
	install(TARGETS synthpod.sandbox.kx DESTINATION bin)
endif()
