project('synthpod', 'c', default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=true',
	'c_std=c11'])

cc = meson.get_compiler('c')

inc_incs = include_directories('include')
app_incs = include_directories('app')
xpress_incs = include_directories('xpress.lv2')
osc_incs = include_directories('osc.lv2')
extui_incs = include_directories('ext_ui.lv2')
ardour_incs = include_directories('ardour.lv2')
varchunk_incs = include_directories('varchunk')
crossclock_incs = include_directories('cross_clock')
lfrtm_incs = include_directories('lfrtm')
mapper_incs = include_directories('mapper.lv2')
sbox_incs = include_directories('sandbox_ui.lv2')
netatom_incs = include_directories('netatom.lv2')

m_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
lv2_dep = dependency('lv2', version : '>=1.14.0')
thread_dep = dependency('threads')
lilv_dep = dependency('lilv-0', version : '>=0.24.0',
	static : meson.is_cross_build())

rawvers = run_command('cat', 'VERSION').stdout().strip()
version = rawvers.split('.')

conf_data = configuration_data()
conf_data.set('MAJOR_VERSION', version[0])
conf_data.set('MINOR_VERSION', version[1])
conf_data.set('MICRO_VERSION', version[2])

add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-DSYNTHPOD_VERSION="'+rawvers+'"', language : 'c')
add_project_arguments('-DSYNTHPOD_MINOR_VERSION="'+version[1]+'"', language : 'c') #FIXME
add_project_arguments('-DSYNTHPOD_MICRO_VERSION="'+version[2]+'"', language : 'c') #FIXME
add_project_arguments('-DSYNTHPOD_BUNDLE_DIR="/usr/local/lib/synthpod/"', language : 'c') #FIXME
add_project_arguments('-DSYNTHPOD_BIN_DIR="/usr/local/bin/"', language : 'c') #FIXME
add_project_arguments('-DSYNTHPOD_PLUGIN_DIR="/usr/local/lib/lv2/"', language : 'c') #FIXME

cp = find_program('cp')
clone = [cp, '@INPUT@', '@OUTPUT@']

c_args = ['-fvisibility=hidden',
	'-ffast-math']

subdir('sandbox_ui.lv2')
subdir('app')
subdir('bin')
#subdir('plugins')
