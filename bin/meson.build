bin_incs = [inc_incs, app_incs, xpress_incs, osc_incs, extui_incs, ardour_incs, varchunk_incs, crossclock_incs, lfrtm_incs, mapper_incs, sbox_incs]
bin_deps = [m_dep, rt_dep, lv2_dep, thread_dep, lilv_dep]

bin_srcs = ['synthpod_bin.c',
	'nsmc.c']

bin = static_library('synthpod_bin', bin_srcs,
	include_directories : bin_incs,
	dependencies : bin_deps)

dummy_srcs = ['synthpod_dummy.c']

dummy = executable('synthpod_dummy', dummy_srcs,
	include_directories : bin_incs,
	dependencies : bin_deps,
	link_with : [bin, app, sbox_master],
	install : true)

if alsa_dep.found() and zita_dep.found()
	add_languages('cpp')
	alsa_srcs = ['synthpod_alsa.c', 'pcmi.cpp']

	alsa = executable('synthpod_alsa', alsa_srcs,
		include_directories : bin_incs,
		dependencies : [bin_deps, alsa_dep, zita_dep],
		link_with : [bin, app, sbox_master],
		install : true)
endif

if jack_dep.found()
	jack_srcs = ['synthpod_jack.c']

	jack = executable('synthpod_jack', jack_srcs,
		include_directories : bin_incs,
		dependencies : [bin_deps, jack_dep],
		link_with : [bin, app, sbox_master],
		install : true)
endif

if xcb_dep.found() and xcbicccm_dep.found()
	x11_srcs = ['synthpod_sandbox_x11.c']

	x11 = executable('synthpod_sandbox_x11', x11_srcs,
		include_directories : bin_incs,
		dependencies : [rt_dep, thread_dep, xcb_dep, xcbicccm_dep],
		link_with : [sbox_slave],
		install : true)
endif

if gtk2_dep.found()
	gtk2_srcs = ['synthpod_sandbox_gtk.c']

	gtk2 = executable('synthpod_sandbox_gtk2', gtk2_srcs,
		include_directories : bin_incs,
		dependencies : [rt_dep, thread_dep, gtk2_dep],
		link_with : [sbox_slave],
		install : true)
endif

if gtk3_dep.found()
	gtk3_srcs = ['synthpod_sandbox_gtk.c']

	gtk3 = executable('synthpod_sandbox_gtk3', gtk3_srcs,
		include_directories : bin_incs,
		dependencies : [rt_dep, thread_dep, gtk3_dep],
		link_with : [sbox_slave],
		install : true)
endif

if true #FIXME only build with enabled UI
	show_srcs = ['synthpod_sandbox_show.c']

	gtk3 = executable('synthpod_sandbox_show', show_srcs,
		include_directories : bin_incs,
		dependencies : [rt_dep, thread_dep],
		link_with : [sbox_slave],
		install : true)
endif

if true #FIXME only build with enabled UI
	kx_srcs = ['synthpod_sandbox_kx.c']

	gtk3 = executable('synthpod_sandbox_kx', kx_srcs,
		include_directories : bin_incs,
		dependencies : [rt_dep, thread_dep],
		link_with : [sbox_slave],
		install : true)
endif
