project('mapper.lv2', 'c', default_options : [
	'buildtype=release',
	'warning_level=3',
	'werror=false',
	'b_lto=false',
	'c_std=gnu11'])

version = run_command('cat', 'VERSION').stdout().strip()

add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
cc = meson.get_compiler('c')

m_dep = cc.find_library('m')
lv2_dep = dependency('lv2')
thread_dep = dependency('threads')
deps = [m_dep, lv2_dep, thread_dep,]

if host_machine.system() != 'darwin'
	mapper_test = executable('mapper_test',
		join_paths('test', 'mapper_test.c'),
		dependencies : deps,
		install : false)

	nonrt = '0'
	rt = '1'
	seed = '1234567890'

	test(' 1 threads non-rt', mapper_test,
		args : ['1', nonrt, seed],
		timeout : 360)
	test(' 2 threads rt', mapper_test,
		args : ['2', rt, seed],
		timeout : 360)
	test(' 4 threads non-rt', mapper_test,
		args : ['4', nonrt, seed],
		timeout : 360)
	test(' 8 threads rt', mapper_test,
		args : ['8', rt, seed],
		timeout : 360)
	if host_machine.system() == 'linux'
		test('16 threads non-rt', mapper_test,
			args : ['16', nonrt, seed],
			timeout : 360)
		test('32 threads rt', mapper_test,
			args : ['32', rt, seed],
			timeout : 360)
	endif
endif
