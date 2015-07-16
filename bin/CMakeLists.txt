include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/libosc) # osc.h
include_directories(${PROJECT_SOURCE_DIR}/varchunk)
include_directories(${PROJECT_SOURCE_DIR}/system_port.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

add_definitions("-D_GNU_SOURCE=1") # asprintf

set(SYNTHPOD_BIN_DIR "bin")
set(SYNTHPOD_ICON_DIR "share/icons/hicolor/256x256/apps")
set(SYNTHPOD_DESKTOP_DIR "share/applications")

if(APPLE)
	find_library(CORE_AUDIO CoreAudio)
	find_library(AUDIO_TOOLBOX AudioToolbox)
	find_library(AUDIO_UNIT AudioUnit)
	find_library(CARBON Carbon)
	
	find_library(CORE_SERVICES CoreServices)
endif(APPLE)

if(BUILD_JACK OR BUILD_PORTAUDIO)
# icon
	install(FILES ${PROJECT_SOURCE_DIR}/data/pix/synthpod.png DESTINATION
		${SYNTHPOD_ICON_DIR})

# man page 
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.1 DESTINATION
		man/man1)
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
		synthpod.lib)
	if(BUILD_UI)
		target_link_libraries(synthpod.jack
			synthpod_ui.lib)
	endif()
	set_target_properties(synthpod.jack PROPERTIES OUTPUT_NAME "synthpod_jack")
	install(TARGETS synthpod.jack DESTINATION bin)

# desktop  file
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_jack.desktop.in
		${PROJECT_BINARY_DIR}/bin/synthpod_jack.desktop @ONLY)
	install(FILES ${PROJECT_BINARY_DIR}/bin/synthpod_jack.desktop DESTINATION
		${SYNTHPOD_DESKTOP_DIR})
endif()

if(BUILD_PORTAUDIO)
# portaudio
	pkg_search_module(PORTAUDIO REQUIRED portaudio-2.0>=19)
	include_directories(${PORTAUDIO_INCLUDE_DIRS})

	if(APPLE)
		set(PORTAUDIO_LIBS ${CORE_AUDIO} ${AUDIO_TOOLBOX} ${AUDIO_UNIT} ${CARBON} "-lportaudio")
	else()
		set(PORTAUDIO_LIBS ${PORTAUDIO_LDFLAGS})
	endif()

# synthpod
	add_executable(synthpod.pa
		synthpod_pa.c
		synthpod_nsm.c
		ext_urid.c)
	target_link_libraries(synthpod.pa
		${ECORE_CON_LDFLAGS}
		${PORTAUDIO_LIBS}
		synthpod.lib)
	if(BUILD_UI)
		target_link_libraries(synthpod.pa
			synthpod_ui.lib)
	endif()
	set_target_properties(synthpod.pa PROPERTIES OUTPUT_NAME "synthpod_pa")
	install(TARGETS synthpod.pa DESTINATION bin)

# desktop  file
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/synthpod_pa.desktop.in
		${PROJECT_BINARY_DIR}/bin/synthpod_pa.desktop @ONLY)
	install(FILES ${PROJECT_BINARY_DIR}/bin/synthpod_pa.desktop DESTINATION
		${SYNTHPOD_DESKTOP_DIR})
endif()
