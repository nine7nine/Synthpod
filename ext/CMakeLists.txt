include_directories(${CMAKE_CURRENT_SOURCE_DIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl DESTINATION lib/lv2/eoui.lv2)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/eoui.h DESTINATION lib/lv2/eoui.lv2)
