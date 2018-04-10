dsp_srcs = ['synthpod_lv2.c',
	'synthpod_control2cv.c',
	'synthpod_cv2control.c',
	'synthpod_heavyload.c',
	'synthpod_keyboard.c',
	'synthpod_midisplitter.c',
	'synthpod_panic.c',
	'synthpod_placeholder.c',
	'synthpod_stereo.c']

ui_deps = [m_dep, rt_dep, lv2_dep, lilv_dep]
ui_srcs = ['synthpod_lv2_nk.c',
	'synthpod_keyboard_nk.c',
	'synthpod_common_nk.c']

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
	
conf_data.set('UI_WRAP', '') #FIXME only build with enbaled UI

if host_machine.system() == 'linux'
	conf_data.set('UI_TYPE', 'X11UI')
	ui_deps += gl_dep
	ui_deps += x11_dep
	ui_deps += xext_dep
	ui_srcs += join_paths('..', 'pugl', 'pugl', 'pugl_x11.c')
elif host_machine.system() == 'windows'
	add_languages('cpp')
	conf_data.set('UI_TYPE', 'WindowsUI')
	ui_deps += cc.find_library('opengl32')
	ui_deps += cc.find_library('gdi32')
	ui_deps += cc.find_library('user32')
	ui_srcs += join_paths('..', 'pugl', 'pugl', 'pugl_win.cpp')
elif host_machine.system() == 'darwin'
	#add_languages('objc')
	conf_data.set('UI_TYPE', 'CocoaUI')
	#ui_deps += cc.find_library('Cocoa')
	#ui_deps += cc.find_library('gl')
	#ui_deps += dependency('appleframeworks', modules : 'cocoa')
	#ui_srcs += join_paths('..', 'pugl', 'pugl', 'pugl_osx.m')
endif

ui = shared_module('synthpod_nk', ui_srcs,
	c_args : c_args,
	include_directories : bin_incs,
	name_prefix : '',
	dependencies : ui_deps,
	link_with : [sbox_master],
	install : true,
	install_dir : plug_dir)

configure_file(input : 'manifest.ttl.in', output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : plug_dir)

custom_target('synthpod_ttl',
	input : 'synthpod.ttl',
	output : 'synthpod.ttl',
	command : clone,
	install : true,
	install_dir : plug_dir)

custom_target('synthpod_ui_ttl',
	input : 'synthpod_ui.ttl',
	output : 'synthpod_ui.ttl',
	command : clone,
	install : true,
	install_dir : plug_dir)

#FIXME paths
install_data('../data/font/Abel-Regular.ttf', install_dir : plug_dir)

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
