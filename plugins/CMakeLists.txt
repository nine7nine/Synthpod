include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libosc_stream)

set(DEST lib/lv2/synthpod.lv2)
set(LIB_EXT ".so")

# rtmidi
pkg_search_module(RTMIDI REQUIRED librtmidi>=2.1)
include_directories(${RTMIDI_INCLUDE_DIRS})

# libuv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

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
