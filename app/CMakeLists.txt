# synthpod.lib
add_library(synthpod.lib OBJECT
	synthpod_app.c
	synthpod_app_state.c
	synthpod_app_worker.c
	synthpod_app_ui.c
	synthpod_app_mod.c
	synthpod_app_port.c)
set_target_properties(synthpod.lib PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
set_target_properties(synthpod.lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION IPO_SUPPORTED) # -flto
