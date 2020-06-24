bndl_srcs = ['synthpod_bundle.c']

bndl = shared_module('synthpod_bundle', bndl_srcs,
	c_args : c_args,
	name_prefix : '',
	dependencies : [m_dep, lv2_dep],
	install : true,
	install_dir : bndl_dir)

bndl_manifest_ttl = configure_file(
	input : 'manifest.ttl.in',
	output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : bndl_dir)

bndl_dsp_ttl = configure_file(
	input : 'synthpod_bundle.ttl',
	output : 'synthpod_bundle.ttl',
	copy : true,
	install : true,
	install_dir : bndl_dir)

if lv2_validate.found() and sord_validate.found()
	test('LV2 validate', lv2_validate,
		args : [bndl_manifest_ttl, bndl_dsp_ttl])
endif

if lv2lint.found()
	test('LV2 lint', lv2lint,
		args : ['-I', join_paths(build_root, 'bundle', ''),
			'-Ewarn',
			'http://open-music-kontrollers.ch/lv2/synthpod#source',
			'http://open-music-kontrollers.ch/lv2/synthpod#sink',
			'http://open-music-kontrollers.ch/lv2/synthpod#osc_source',
			'http://open-music-kontrollers.ch/lv2/synthpod#osc_sink',
			'http://open-music-kontrollers.ch/lv2/synthpod#cv_source',
			'http://open-music-kontrollers.ch/lv2/synthpod#cv_sink',
			'http://open-music-kontrollers.ch/lv2/synthpod#audio_source',
			'http://open-music-kontrollers.ch/lv2/synthpod#audio_sink',
			'http://open-music-kontrollers.ch/lv2/synthpod#midi_source',
			'http://open-music-kontrollers.ch/lv2/synthpod#midi_sink'])
endif
