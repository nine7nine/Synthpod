project('lfrtm', 'c', default_options : [
	'buildtype=release',
	'warning_level=3',
	'werror=true',
	'b_lto=true',
	'c_std=c11'])

version = run_command('cat', 'VERSION').stdout().strip()

add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
cc = meson.get_compiler('c')

thread_dep = dependency('threads')
deps = [thread_dep]

c_args = []

if host_machine.system() == 'darwin'
	c_args += '-Wno-deprecated-declarations' # shm_init/destroy
endif

dummy_calloc = shared_library('dummy_calloc',
	join_paths('test', 'dummy_calloc.c'),
	c_args : c_args,
	dependencies : deps,
	install : false)

lfrtm_test = executable('lfrtm_test',
	join_paths('test', 'lfrtm_test.c'),
	c_args : c_args,
	dependencies : deps,
	install : false)

test(' 1 threads', lfrtm_test,
	args : ['1'],
	timeout : 240)
test(' 2 threads', lfrtm_test,
	args : ['1'],
	timeout : 240)
test(' 4 threads', lfrtm_test,
	args : ['1'],
	timeout : 240)
test(' 8 threads', lfrtm_test,
	args : ['1'],
	timeout : 240)
if host_machine.system() == 'linux'
	test('16 threads', lfrtm_test,
		args : ['1'],
		timeout : 240)
	test('32 threads', lfrtm_test,
		args : ['1'],
		timeout : 240)
endif
#test('dummy alloc', lfrtm_test,
#	args : ['2', '1'],
#	timeout : 240,
#	env : ['LD_PRELOAD='+dummy_calloc.full_path()])
