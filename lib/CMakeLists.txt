include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/eo_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_writer.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/cjson)
		
add_definitions("-D_GNU_SOURCE=1") # asprintf

include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("
	int main(int argc, char **argv)
	{
		void *src;
		void *dst = __builtin_assume_aligned(src, 16);
		return 0;
	}" HAS_BUILTIN_ASSUME_ALIGNED)
if(${HAS_BUILTIN_ASSUME_ALIGNED})
	add_definitions("-DHAS_BUILTIN_ASSUME_ALIGNED")
endif()

# synthpod
add_library(synthpod.lib SHARED
	synthpod_app.c
	cjson/cJSON.c)
target_link_libraries(synthpod.lib
	${EINA_LDFLAGS}
	${ECORE_LDFLAGS}
	${ECORE_CON_LDFLAGS}
	${ECORE_FILE_LDFLAGS}
	${EFREET_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES
	OUTPUT_NAME "synthpod"
	VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod.lib DESTINATION lib)

# synthpod_ui
add_library(synthpod_ui.lib SHARED
	synthpod_ui.c
	patcher.c
	smart_slider.c
	smart_meter.c
	smart_spinner.c
	smart_toggle.c)
target_link_libraries(synthpod_ui.lib
	${ELM_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS})
set_target_properties(synthpod_ui.lib PROPERTIES
	OUTPUT_NAME "synthpod_ui"
	VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod_ui.lib DESTINATION lib)
