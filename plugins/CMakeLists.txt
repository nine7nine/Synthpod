include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DEST lib/lv2/rtmidi.lv2)
set(LIB_EXT ".so")

# rtmidi
pkg_search_module(RTMIDI REQUIRED librtmidi>=2.1)
include_directories(${RTMIDI_INCLUDE_DIRS})

add_library(rtmidi MODULE
	rtmidi.c
	rtmidi_in.cpp
	rtmidi_out.cpp)
target_link_libraries(rtmidi ${RTMIDI_LDFLAGS})
set_target_properties(rtmidi PROPERTIES PREFIX "")
install(TARGETS rtmidi DESTINATION ${DEST})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi.ttl DESTINATION ${DEST})
