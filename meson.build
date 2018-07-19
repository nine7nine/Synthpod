project('synthpod', ['c', 'cpp'], default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=false',
	'c_std=c11'])

cc = meson.get_compiler('c')

inc_incs = include_directories('include')
app_incs = include_directories('app')
xpress_incs = include_directories('xpress.lv2')
osc_incs = include_directories('osc.lv2')
canvas_incs = include_directories('canvas.lv2')
extui_incs = include_directories('ext_ui.lv2')
ardour_incs = include_directories('ardour.lv2')
varchunk_incs = include_directories('varchunk')
crossclock_incs = include_directories('cross_clock')
lfrtm_incs = include_directories('lfrtm')
mapper_incs = include_directories('mapper.lv2')
sbox_incs = include_directories('sandbox_ui.lv2')
netatom_incs = include_directories('netatom.lv2')
props_incs = include_directories('props.lv2')
pugl_incs = include_directories('pugl')
jackey_incs = include_directories('jackey')
self_incs = include_directories('')

bin_incs = [inc_incs, app_incs, xpress_incs, osc_incs, canvas_incs, extui_incs, ardour_incs, varchunk_incs, crossclock_incs, lfrtm_incs, mapper_incs, sbox_incs, props_incs, pugl_incs, jackey_incs, self_incs]

static_link = meson.is_cross_build() and false #FIXME

m_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
lv2_dep = dependency('lv2', version : '>=1.14.0')
thread_dep = dependency('threads')
lilv_dep = dependency('lilv-0', version : '>=0.24.0', static : static_link)
alsa_dep = dependency('alsa', version : '>=1.1.0', required : false)
zita_dep = cc.find_library('zita-alsa-pcmi', required : false)
jack_dep = dependency('jack', required : false)
gl_dep = dependency('gl', required : false)
x11_dep = dependency('x11', version : '>=1.6.0', required : false)
xext_dep = dependency('xext', version : '>=1.3.0', required : false)
xcb_dep = dependency('xcb', version : '>=1.12', required : false)
xcbicccm_dep = dependency('xcb-icccm', version : '>=0.4.0', required : false)
gtk2_dep = dependency('gtk+-2.0', version : '>=2.24', required : false)
gtk3_dep = dependency('gtk+-3.0', version : '>=3.22', required : false)
qt4_dep = dependency('qt4', modules : ['Core', 'Gui'], required : false)
qt5_dep = dependency('qt5', modules : ['Core', 'Widgets', 'Gui'], required : false)
cairo_dep = dependency('cairo', static : static_link, required : false)

bin_deps = [m_dep, rt_dep, lv2_dep, thread_dep, lilv_dep]

rawvers = run_command('cat', 'VERSION').stdout().strip()
version = rawvers.split('.')

conf_data = configuration_data()
conf_data.set('MAJOR_VERSION', version[0])
conf_data.set('MINOR_VERSION', version[1])
conf_data.set('MICRO_VERSION', version[2])

prefix = get_option('prefix')
bndl_dir = join_paths(get_option('libdir'), meson.project_name(), 'lv2')
plug_dir = join_paths(get_option('libdir'), 'lv2', meson.project_name()+'.lv2')

if get_option('dynamic-parallelizer')
	message('using dynamic parallelizer')
	add_project_arguments('-DUSE_DYNAMIC_PARALLELIZER', language : 'c')
endif

if gl_dep.found()
	add_project_arguments('-DPUGL_HAVE_GL', language : 'c')
endif

if xcb_dep.found() and xcbicccm_dep.found()
	message('building X11 sandbox')
	add_project_arguments('-DSANDBOX_X11', language : 'c')
endif

if gtk2_dep.found()
	message('building GTK2 sandbox')
	add_project_arguments('-DSANDBOX_GTK2', language : 'c')
endif

if gtk3_dep.found()
	message('building GTK3 sandbox')
	add_project_arguments('-DSANDBOX_GTK3', language : 'c')
endif

if qt4_dep.found()
	message('building QT4 sandbox')
	add_project_arguments('-DSANDBOX_QT4', language : 'c')
endif

if qt5_dep.found()
	message('building QT5 sandbox')
	add_project_arguments('-DSANDBOX_QT5', language : 'c')
endif

if cairo_dep.found()
	message('using cairo canvas')
	add_project_arguments('-DUSE_CAIRO_CANVAS', language : 'c')
endif

add_project_arguments('-DSANDBOX_SHOW', language : 'c')

add_project_arguments('-DSANDBOX_KX', language : 'c')

if lilv_dep.version() >= '0.22.0'
	message('building against recent lilv')
	add_project_arguments('-DLILV_0_22', language : 'c')
endif

add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-DSYNTHPOD_VERSION="'+rawvers+'"', language : 'c')
add_project_arguments('-DSYNTHPOD_MINOR_VERSION='+version[1]+'', language : 'c')
add_project_arguments('-DSYNTHPOD_MICRO_VERSION='+version[2]+'', language : 'c')
add_project_arguments('-DSYNTHPOD_BUNDLE_DIR="'+join_paths(prefix, bndl_dir, '')+'"', language : 'c')
add_project_arguments('-DSYNTHPOD_BIN_DIR="'+join_paths(prefix, get_option('bindir'), '')+'"', language : 'c')
add_project_arguments('-DSYNTHPOD_PLUGIN_DIR="'+join_paths(prefix, plug_dir, '')+'"', language : 'c')

builtin_assume_aligned = '''
int main(int argc, char **argv)
{
	void *src;
	void *dst = __builtin_assume_aligned(src, 16);

	return 0;
}
'''

if cc.compiles(builtin_assume_aligned, name : 'builtin_assume_aligned')
	add_project_arguments('-DHAS_BUILTIN_ASSUME_ALIGNED', language : 'c')
endif

cp = find_program('cp')
clone = [cp, '@INPUT@', '@OUTPUT@']

c_args = ['-fvisibility=hidden',
	'-ffast-math',
	'-Wno-attributes',
	'-Wno-unused-function',
	'-Wno-unused-variable',
	'-Wno-stringop-overflow']

if cc.has_argument('-ftree-vectorize')
	c_args += '-ftree-vectorize'
endif

if cc.has_argument('-ftree-vectorizer-verbose=1')
	c_args += '-ftree-vectorizer-verbose=1'
endif

if cc.has_argument('-Rpass=loop-vectorize')
	c_args += '-Rpass=loop-vectorize'
endif

subdir('sandbox_ui.lv2')
subdir('app')
subdir('plugins')
subdir('bin')
subdir('bundle')
