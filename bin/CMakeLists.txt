include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc) # osc.h
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

add_definitions("-D_GNU_SOURCE=1") # asprintf

set(SYNTHPOD_BIN_DIR "bin")
set(SYNTHPOD_ICON_DIR "share/icons/hicolor/256x256/apps")
set(SYNTHPOD_DESKTOP_DIR "share/applications")

if(BUILD_JACK OR BUILD_ALSA)
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
		ext_urid.c)
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
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_jack.1.gz" DESTINATION man/man1)
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
		ext_urid.c)
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
	install(FILES "${PROJECT_BINARY_DIR}/synthpod_alsa.1.gz" DESTINATION man/man1)
endif()
