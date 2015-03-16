set(DEST lib/lv2/sherlock.lv2)
set(LIB_EXT ".so")

add_library(rtmidi MODULE
	rtmidi.c
	rtmidi_in.cpp
	rtmidi_out.cpp)
target_link_libraries(rtmidi ${LIBS})
set_target_properties(rtmidi PROPERTIES PREFIX "")
install(TARGETS rtmidi DESTINATION ${DEST})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/plugins/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/plugins/manifest.ttl DESTINATION ${DEST})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi.ttl DESTINATION ${DEST})
