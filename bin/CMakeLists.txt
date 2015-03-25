include_directories(${PROJECT_SOURCE_DIR}/lib) # synthpod.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# uv
pkg_search_module(UV REQUIRED libuv>=1.0)
include_directories(${UV_INCLUDE_DIRS})

# jack
pkg_search_module(JACK REQUIRED jack)
include_directories(${JACK_INCLUDE_DIRS})

# synthpod
add_executable(synthpod.jack
	synthpod_jack.c)
target_link_libraries(synthpod.jack
	${UV_LDFLAGS}
	${JACK_LDFLAGS}
	synthpod.lib
	synthpod_ui.lib)
set_target_properties(synthpod.jack PROPERTIES OUTPUT_NAME "synthpod_jack")
install(TARGETS synthpod.jack DESTINATION bin)
