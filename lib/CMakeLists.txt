include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/sandbox_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/ext_ui.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_worker.lv2)
include_directories(${PROJECT_SOURCE_DIR}/zero_writer.lv2)
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)
include_directories(${PROJECT_SOURCE_DIR}/symap)
		
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
	synthpod_app.c)
target_link_libraries(synthpod.lib
	${EINA_LDFLAGS}
	${ECORE_LDFLAGS}
	${ECORE_CON_LDFLAGS}
	${ECORE_FILE_LDFLAGS}
	${EFREET_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS}
	${SRATOM_LDFLAGS})
set_target_properties(synthpod.lib PROPERTIES
	OUTPUT_NAME "synthpod"
	VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod.lib DESTINATION lib)

# synthpod_ui
if(BUILD_SANDBOX_LIB)
	set(SANDBOX_MASTER_SOURCE ${PROJECT_SOURCE_DIR}/sandbox_ui.lv2/sandbox_master.c)
else()
	unset(SANDBOX_MASTER_SOURCE)
endif()

add_library(synthpod_ui.lib SHARED
	synthpod_ui.c
	patcher.c
	smart_slider.c
	smart_meter.c
	smart_spinner.c
	smart_toggle.c
	smart_bitmask.c
	${SANDBOX_MASTER_SOURCE})
target_link_libraries(synthpod_ui.lib
	${ELM_LDFLAGS}
	${LILV_LDFLAGS}
	${LV2_LDFLAGS})
if(BUILD_SANDBOX_LIB)
	target_link_libraries(synthpod_ui.lib
		${NANOMSG_LDFLAGS})
endif()
set_target_properties(synthpod_ui.lib PROPERTIES
	OUTPUT_NAME "synthpod_ui"
	VERSION "${SYNTHPOD_VERSION}"
	SOVERSION "${SYNTHPOD_MAJOR_VERSION}")
install(TARGETS synthpod_ui.lib DESTINATION lib)

if(BUILD_SANDBOX_LIB)
	add_definitions("-DSANDBOX_LIB")
	if(BUILD_SANDBOX_X11)
		add_definitions("-DSANDBOX_X11")
	endif()
	if(BUILD_SANDBOX_GTK2)
		add_definitions("-DSANDBOX_GTK2")
	endif()
	if(BUILD_SANDBOX_GTK3)
		add_definitions("-DSANDBOX_GTK3")
	endif()
	if(BUILD_SANDBOX_QT4)
		add_definitions("-DSANDBOX_QT4")
	endif()
	if(BUILD_SANDBOX_QT5)
		add_definitions("-DSANDBOX_QT5")
	endif()
	if(BUILD_SANDBOX_EFL)
		add_definitions("-DSANDBOX_EFL")
	endif()

# nanomsg
	pkg_search_module(NANOMSG REQUIRED libnanomsg>=2.0)
	include_directories(${NANOMSG_INCLUDE_DIRS})
endif()
