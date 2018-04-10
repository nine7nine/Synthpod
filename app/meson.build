srcs = ['synthpod_app.c',
	'synthpod_app_mod.c',
	'synthpod_app_port.c',
	'synthpod_app_state.c',
	'synthpod_app_ui.c',
	'synthpod_app_worker.c'
]

incs = [inc_incs, app_incs, xpress_incs, osc_incs, extui_incs, ardour_incs, varchunk_incs, crossclock_incs]
deps = [m_dep, rt_dep, lv2_dep, thread_dep, lilv_dep]

app = static_library('synthpod_app', srcs,
	include_directories : incs,
	c_args : c_args,
	dependencies : deps)
