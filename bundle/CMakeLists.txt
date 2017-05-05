# synthpod_bundle.lv2
add_library(synthpod_bundle.lv2 MODULE
	synthpod_bundle.c)
set_target_properties(synthpod_bundle.lv2 PROPERTIES PREFIX "" OUTPUT_NAME "synthpod_bundle")
set_target_properties(synthpod_bundle.lv2 PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
set_target_properties(synthpod_bundle.lv2 PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
install(TARGETS synthpod_bundle.lv2 DESTINATION ${SYNTHPOD_BUNDLE_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/synthpod_bundle.ttl DESTINATION ${SYNTHPOD_BUNDLE_DIR})

# manifest
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.ttl.in ${PROJECT_BINARY_DIR}/bundle/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/bundle/manifest.ttl DESTINATION ${SYNTHPOD_BUNDLE_DIR})
