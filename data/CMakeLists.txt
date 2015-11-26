include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# theme
find_program(EDJE_CC NAMES edje_cc)
if(EDJE_CC_NOTFOUND)
	message(SEND_ERROR "edje_cc not found")
else(EDJE_CC_NOTFOUND)
	message(STATUS "edje_cc found: " ${EDJE_CC})
endif(EDJE_CC_NOTFOUND)

add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/synthpod.edj
	COMMAND ${EDJE_CC} ARGS
		"-fd" ${CMAKE_CURRENT_SOURCE_DIR}/font
		"-id" ${CMAKE_CURRENT_SOURCE_DIR}/pix
		${CMAKE_CURRENT_SOURCE_DIR}/synthpod.edc
		${PROJECT_BINARY_DIR}/synthpod.edj
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/modgrid.edc
		${CMAKE_CURRENT_SOURCE_DIR}/modlist.edc
		${CMAKE_CURRENT_SOURCE_DIR}/patcher.edc
		${CMAKE_CURRENT_SOURCE_DIR}/smart_slider.edc
		${CMAKE_CURRENT_SOURCE_DIR}/smart_meter.edc
		${CMAKE_CURRENT_SOURCE_DIR}/smart_spinner.edc
		${CMAKE_CURRENT_SOURCE_DIR}/smart_toggle.edc
		${CMAKE_CURRENT_SOURCE_DIR}/synthpod.edc
		${CMAKE_CURRENT_SOURCE_DIR}/theme.edc
		${CMAKE_CURRENT_SOURCE_DIR}/omk_logo.edc
		${CMAKE_CURRENT_SOURCE_DIR}/entry.edc
		${CMAKE_CURRENT_SOURCE_DIR}/group.edc)
add_custom_target(THEME ALL DEPENDS ${PROJECT_BINARY_DIR}/synthpod.edj)
install(FILES ${PROJECT_BINARY_DIR}/synthpod.edj DESTINATION ${SYNTHPOD_DATA_DIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pix/audio.png DESTINATION ${SYNTHPOD_DATA_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pix/atom.png DESTINATION ${SYNTHPOD_DATA_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pix/control.png DESTINATION ${SYNTHPOD_DATA_DIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pix/cv.png DESTINATION ${SYNTHPOD_DATA_DIR})
