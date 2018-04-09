dummy_srcs = ['synthpod_dummy.c',
	'synthpod_bin.c',
	'nsmc.c'
]

incs = [inc_incs, app_incs, xpress_incs, osc_incs, extui_incs, ardour_incs, varchunk_incs, crossclock_incs, lfrtm_incs, mapper_incs, sbox_incs]
deps = [m_dep, rt_dep, lv2_dep, thread_dep, lilv_dep]

dummy = executable('synthpod_dummy', dummy_srcs,
	include_directories : incs,
	dependencies : deps,
	link_with : [app, sbox_master],
	install : true)
