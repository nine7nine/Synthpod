include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod_app.h, synthpod_ui.h

if(APPLE)
	find_library(CORE_AUDIO CoreAudio)
	find_library(AUDIO_TOOLBOX AudioToolbox)
	find_library(AUDIO_UNIT AudioUnit)
	find_library(CARBON Carbon)
	
	find_library(CORE_SERVICES CoreServices)
endif(APPLE)

if(BUILD_JACK)
# jack
	pkg_search_module(JACK REQUIRED jack)
	include_directories(${JACK_INCLUDE_DIRS})

	if(APPLE)
		set(JACK_LIBS ${CORE_AUDIO} ${CORE_SERVICES} ${AUDIO_UNIT} "-ljack")
	else()
		set(JACK_LIBS ${JACK_LDFLAGS})
	endif()

# synthpod
	add_executable(synthpod.jack
		synthpod_jack.c
		varchunk.c)
	target_link_libraries(synthpod.jack
		${UV_LDFLAGS}
		${JACK_LIBS}
		synthpod.lib)
	if(BUILD_UI)
		target_link_libraries(synthpod.jack
			synthpod_ui.lib)
	endif()
	set_target_properties(synthpod.jack PROPERTIES OUTPUT_NAME "synthpod_jack")
	install(TARGETS synthpod.jack DESTINATION bin)
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
		varchunk.c)
	target_link_libraries(synthpod.pa
		${UV_LDFLAGS}
		${PORTAUDIO_LIBS}
		synthpod.lib)
	if(BUILD_UI)
		target_link_libraries(synthpod.pa
			synthpod_ui.lib)
	endif()
	set_target_properties(synthpod.pa PROPERTIES OUTPUT_NAME "synthpod_pa")
	install(TARGETS synthpod.pa DESTINATION bin)
endif()

if(BUILD_TEST)
# varchunk test
	add_executable(test_varchunk
		test_varchunk.c
		varchunk.c)
	target_link_libraries(test_varchunk ${UV_LDFLAGS})
endif()
