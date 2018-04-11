bin_srcs = ['synthpod_bin.c',
	'nsmc.c']

bin = static_library('synthpod_bin', bin_srcs,
	include_directories : bin_incs,
	c_args : c_args,
	dependencies : bin_deps)

if true
	dummy_srcs = ['synthpod_dummy.c']

	dummy = executable('synthpod_dummy', dummy_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : bin_deps,
		link_with : [bin, app, sbox_master],
		install : true)

	install_man('synthpod_dummy.1')
endif

if alsa_dep.found() and zita_dep.found()
	alsa_srcs = ['synthpod_alsa.c', 'pcmi.cpp']

	alsa = executable('synthpod_alsa', alsa_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [bin_deps, alsa_dep, zita_dep],
		link_with : [bin, app, sbox_master],
		install : true)

	install_man('synthpod_alsa.1')
endif

if jack_dep.found()
	jack_srcs = ['synthpod_jack.c']

	jack = executable('synthpod_jack', jack_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [bin_deps, jack_dep],
		link_with : [bin, app, sbox_master],
		install : true)

	install_man('synthpod_jack.1')
endif

if xcb_dep.found() and xcbicccm_dep.found()
	x11_srcs = ['synthpod_sandbox_x11.c']

	x11 = executable('synthpod_sandbox_x11', x11_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [rt_dep, thread_dep, xcb_dep, xcbicccm_dep],
		link_with : [sbox_slave],
		install : true)
endif

if gtk2_dep.found()
	gtk2_srcs = ['synthpod_sandbox_gtk.c']

	gtk2 = executable('synthpod_sandbox_gtk2', gtk2_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [rt_dep, thread_dep, gtk2_dep],
		link_with : [sbox_slave],
		install : true)
endif

if gtk3_dep.found()
	gtk3_srcs = ['synthpod_sandbox_gtk.c']

	gtk3 = executable('synthpod_sandbox_gtk3', gtk3_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [rt_dep, thread_dep, gtk3_dep],
		link_with : [sbox_slave],
		install : true)
endif

if qt4_dep.found()
	qt4_mod = import('qt4')
	qt4_srcs = ['synthpod_sandbox_qt.cpp']
	qt4_mocs = qt4_mod.preprocess(moc_sources : qt4_srcs)

	qt4 = executable('synthpod_sandbox_qt4', [qt4_srcs, qt4_mocs],
		include_directories : bin_incs,
		cpp_args : '-DSYNTHPOD_SANDBOX_QT=4',
		c_args : c_args,
		dependencies : [rt_dep, thread_dep, qt4_dep],
		link_with : [sbox_slave],
		install : true)
endif

if qt5_dep.found()
	qt5_mod = import('qt5')
	qt5_srcs = ['synthpod_sandbox_qt.cpp']
	qt5_mocs = qt5_mod.preprocess(moc_sources : qt5_srcs)

	qt5 = executable('synthpod_sandbox_qt5', [qt5_srcs, qt5_mocs],
		include_directories : bin_incs,
		cpp_args : '-DSYNTHPOD_SANDBOX_QT=5',
		c_args : c_args,
		dependencies : [rt_dep, thread_dep, qt5_dep],
		link_with : [sbox_slave],
		install : true)
endif

if true
	show_srcs = ['synthpod_sandbox_show.c']

	show = executable('synthpod_sandbox_show', show_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [rt_dep, thread_dep],
		link_with : [sbox_slave],
		install : true)
endif

if true
	kx_srcs = ['synthpod_sandbox_kx.c']

	kx = executable('synthpod_sandbox_kx', kx_srcs,
		include_directories : bin_incs,
		c_args : c_args,
		dependencies : [rt_dep, thread_dep],
		link_with : [sbox_slave],
		install : true)
endif

install_man('synthpod_sandbox.1')
