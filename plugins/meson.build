dsp_srcs = ['synthpod_lv2.c',
	'synthpod_control2cv.c',
	'synthpod_cv2control.c',
	'synthpod_heavyload.c',
	'synthpod_keyboard.c',
	'synthpod_midisplitter.c',
	'synthpod_panic.c',
	'synthpod_placeholder.c',
	'synthpod_stereo.c']

nk_deps = [m_dep, rt_dep, thread_dep, lv2_dep, lilv_dep, cairo_dep, nk_pugl_dep, xcb_dep, xcbicccm_dep, xcbxrm_dep]
nk_srcs = ['synthpod_lv2_nk.c',
	'synthpod_keyboard_nk.c',
	'synthpod_common_nk.c',
	join_paths('..', 'sandbox_ui.lv2', 'sandbox_slave.c'),
	join_paths('..', 'bin', 'synthpod_sandbox_x11_driver.c')]

ui_deps = [m_dep, rt_dep, thread_dep, lv2_dep, lilv_dep, d2tk_dep, xcb_dep, xcbicccm_dep, xcbxrm_dep]
ui_srcs = ['synthpod_lv2_d2tk.c',
	'synthpod_common_d2tk.c',
	join_paths('..', 'sandbox_ui.lv2', 'sandbox_slave.c'),
	join_paths('..', 'bin', 'synthpod_sandbox_x11_driver.c')]

dsp = shared_module('synthpod', dsp_srcs,
	c_args : c_args,
	include_directories : bin_incs,
	name_prefix : '',
	dependencies : bin_deps,
	link_with : [app, sbox_master],
	install : true,
	install_dir : plug_dir)

suffix = dsp.full_path().strip().split('.')[-1]
conf_data.set('MODULE_SUFFIX', '.' + suffix)
	
if host_machine.system() == 'linux'
	conf_data.set('UI_TYPE', 'X11UI')
elif host_machine.system() == 'windows'
	conf_data.set('UI_TYPE', 'WindowsUI')
elif host_machine.system() == 'darwin'
	conf_data.set('UI_TYPE', 'CocoaUI')
endif

ttls = []

if use_x11
	nk = shared_module('synthpod_nk', nk_srcs,
		c_args : c_args,
		include_directories : bin_incs,
		name_prefix : '',
		dependencies : nk_deps,
		link_with : [sbox_master],
		install : true,
		install_dir : plug_dir)

	ui = shared_module('synthpod_ui', ui_srcs,
		c_args : c_args,
		include_directories : bin_incs,
		name_prefix : '',
		dependencies : ui_deps,
		link_with : [sbox_master],
		install : true,
		install_dir : plug_dir)

	configure_file(
		input : join_paths('..', 'data', 'font', 'Abel-Regular.ttf'),
		output : 'Abel-Regular.ttf',
		copy : true,
		install : true,
		install_dir : plug_dir)

	ui_ttl = configure_file (
		input : 'synthpod_ui.ttl',
		output : 'synthpod_ui.ttl',
		copy : true,
		install : true,
		install_dir : plug_dir)
	ttls += ui_ttl
endif

manifest_ttl = configure_file(
	input : 'manifest.ttl.in',
	output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : plug_dir)
ttls += manifest_ttl

dsp_ttl = configure_file(
	input : 'synthpod.ttl',
	output : 'synthpod.ttl',
	copy : true,
	install : true,
	install_dir : plug_dir)
ttls += dsp_ttl

#FIXME paths
install_data('../data/pix/audio.png', install_dir : plug_dir )
install_data('../data/pix/control.png', install_dir : plug_dir )
install_data('../data/pix/cv.png', install_dir : plug_dir )
install_data('../data/pix/event.png', install_dir : plug_dir )
install_data('../data/pix/event_inverted.png', install_dir : plug_dir )
install_data('../data/pix/atom.png', install_dir : plug_dir )
install_data('../data/pix/atom_inverted.png', install_dir : plug_dir )

install_data('../data/pix/midi.png', install_dir : plug_dir )
install_data('../data/pix/osc.png', install_dir : plug_dir )
install_data('../data/pix/time.png', install_dir : plug_dir )
install_data('../data/pix/patch.png', install_dir : plug_dir )
install_data('../data/pix/xpress.png', install_dir : plug_dir )
install_data('../data/pix/automaton.png', install_dir : plug_dir )

install_data('../data/png/plus.png', install_dir : plug_dir )
install_data('../data/png/download.png', install_dir : plug_dir )
install_data('../data/png/cancel.png', install_dir : plug_dir )
install_data('../data/png/house.png', install_dir : plug_dir )
install_data('../data/png/layers.png', install_dir : plug_dir )
install_data('../data/png/user.png', install_dir : plug_dir )
install_data('../data/png/settings.png', install_dir : plug_dir )
install_data('../data/png/menu.png', install_dir : plug_dir )

if lv2_validate.found() and sord_validate.found()
	test('LV2 validate', lv2_validate,
		args : ttls)
endif

if lv2lint.found()
	test('LV2 lint', lv2lint,
		args : ['-I', join_paths(build_root, 'plugins', ''),
			'-Ewarn',
			'http://open-music-kontrollers.ch/lv2/synthpod#control2cv',
			'http://open-music-kontrollers.ch/lv2/synthpod#cv2control',
			'http://open-music-kontrollers.ch/lv2/synthpod#heavyload',
			'http://open-music-kontrollers.ch/lv2/synthpod#keyboard',
			'http://open-music-kontrollers.ch/lv2/synthpod#midisplitter',
			'http://open-music-kontrollers.ch/lv2/synthpod#panic',
			'http://open-music-kontrollers.ch/lv2/synthpod#placeholder',
			'http://open-music-kontrollers.ch/lv2/synthpod#stereo'])
endif
