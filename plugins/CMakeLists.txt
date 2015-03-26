include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc_stream)

set(DEST lib/lv2/synthpod.lv2)
set(LIB_EXT ".so")

# rtmidi
pkg_search_module(RTMIDI REQUIRED librtmidi>=2.1)
include_directories(${RTMIDI_INCLUDE_DIRS})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${DEST})

# RtMidi
add_library(rtmidi MODULE
	rtmidi.c
	rtmidi_in.cpp
	rtmidi_out.cpp)
target_link_libraries(rtmidi ${RTMIDI_LDFLAGS})
set_target_properties(rtmidi PROPERTIES PREFIX "")
install(TARGETS rtmidi DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi.ttl DESTINATION ${DEST})

# OSC
add_library(osc_io MODULE
	libosc/osc.c
	libosc_stream/osc_stream.c
	libosc_stream/osc_stream_udp.c
	libosc_stream/osc_stream_tcp.c
	libosc_stream/osc_stream_pipe.c
	libosc_stream/osc_stream_slip.c
	osc_io.c
	osc_io_io.c)
target_link_libraries(osc_io ${UV_LDFLAGS})
set_target_properties(osc_io PROPERTIES PREFIX "")
install(TARGETS osc_io DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/osc_io.ttl DESTINATION ${DEST})

# synthpod
add_library(synthpod.lv2 MODULE
	synthpod_lv2.c
	synthpod_stereo.c)
target_link_libraries(synthpod.lv2 synthpod.lib)
set_target_properties(synthpod.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod")
install(TARGETS synthpod.lv2 DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod.ttl DESTINATION ${DEST})

# synthpod_ui
add_library(synthpod_ui.lv2 MODULE
	synthpod_lv2_ui.c
	synthpod_common_ui.c)
target_link_libraries(synthpod_ui.lv2 synthpod_ui.lib)
set_target_properties(synthpod_ui.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_ui")
install(TARGETS synthpod_ui.lv2 DESTINATION ${DEST})
