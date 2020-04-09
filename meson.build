project('synthpod', ['c', 'cpp'], default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=false',
	'c_std=gnu11'])

build_root = meson.build_root()

d2tk = subproject('d2tk')
nk_pugl = subproject('nk_pugl')

if get_option('use-backend-nanovg').enabled()
	d2tk_dep = d2tk.get_variable('d2tk_nanovg')
elif get_option('use-backend-cairo').enabled()
	d2tk_dep = d2tk.get_variable('d2tk_cairo')
else
	error('no valid UI backend given')
endif
	
nk_pugl_dep = nk_pugl.get_variable('nk_pugl_gl')

cc = meson.get_compiler('c')
lv2_validate = find_program('lv2_validate', native : true, required : false)
sord_validate = find_program('sord_validate', native : true, required : false)
lv2lint = find_program('lv2lint', required : false)

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
jackey_incs = include_directories('jackey')
nsmc_incs = include_directories('nsmc')
self_incs = include_directories('')
d2tk_incs = include_directories(join_paths('subprojects', 'd2tk'))
nk_pugl_incs = include_directories(join_paths('subprojects', 'nk_pugl'))

bin_incs = [inc_incs, app_incs, xpress_incs, osc_incs, canvas_incs, extui_incs, ardour_incs, varchunk_incs, crossclock_incs, lfrtm_incs, mapper_incs, sbox_incs, props_incs, jackey_incs, nsmc_incs, self_incs, d2tk_incs, nk_pugl_incs]

static_link = meson.is_cross_build() and false #FIXME

use_jack = get_option('use-jack')
use_alsa = get_option('use-alsa')
use_dummy = get_option('use-dummy')
use_x11 = get_option('use-x11')
use_gtk2 = get_option('use-gtk2')
use_gtk3 = get_option('use-gtk3')
use_qt4 = get_option('use-qt4')
use_qt5 = get_option('use-qt5')

m_dep = cc.find_library('m')
rt_dep = cc.find_library('rt')
lv2_dep = dependency('lv2', version : '>=1.17.2')
thread_dep = dependency('threads')
lilv_dep = dependency('lilv-0', version : '>=0.24.0', static : static_link)
alsa_dep = dependency('alsa', version : '>=1.1.0', required : use_alsa)
zita_dep = cc.find_library('zita-alsa-pcmi', required : use_alsa)
jack_dep = dependency('jack', required : use_jack)
gl_dep = dependency('gl', required : use_x11)
x11_dep = dependency('x11', version : '>=1.6.0', required : use_x11)
xext_dep = dependency('xext', version : '>=1.3.0', required : use_x11)
xcb_dep = dependency('xcb', version : '>=1.12', required : use_x11)
xcbicccm_dep = dependency('xcb-icccm', version : '>=0.4.0', required : use_x11)
xcbxrm_dep = dependency('xcb-xrm', version : '>=1.0', required : use_x11)
gtk2_dep = dependency('gtk+-2.0', version : '>=2.24', required : use_gtk2)
gtk3_dep = dependency('gtk+-3.0', version : '>=3.22', required : use_gtk3)
qt4_dep = dependency('qt4', modules : ['Core', 'Gui'], required : use_qt4)
qt5_dep = dependency('qt5', modules : ['Core', 'Widgets', 'Gui'], required : use_qt5)
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

if use_x11 and gl_dep.found()
	add_project_arguments('-DPUGL_HAVE_GL', language : 'c')
endif

if use_x11 and xcb_dep.found() and xcbicccm_dep.found() and xcbxrm_dep.found()
	message('building X11 sandbox')
endif

if use_gtk2 and gtk2_dep.found()
	message('building GTK2 sandbox')
endif

if use_gtk3 and gtk3_dep.found()
	message('building GTK3 sandbox')
endif

if use_qt4 and qt4_dep.found()
	message('building QT4 sandbox')
endif

if use_qt5 and qt5_dep.found()
	message('building QT5 sandbox')
endif

if use_x11 and cairo_dep.found()
	message('using cairo canvas')
	add_project_arguments('-DUSE_CAIRO_CANVAS', language : 'c')
endif

if lilv_dep.version() >= '0.22.0'
	message('building against recent lilv')
	add_project_arguments('-DLILV_0_22', language : 'c')
endif

add_project_arguments('-D_GNU_SOURCE', language : 'c')
add_project_arguments('-DSYNTHPOD_VERSION="'+rawvers+'"', language : 'c')
add_project_arguments('-DSYNTHPOD_MINOR_VERSION='+version[1]+'', language : 'c')
add_project_arguments('-DSYNTHPOD_MICRO_VERSION='+version[2]+'', language : 'c')
add_project_arguments('-DSYNTHPOD_BUNDLE_DIR="'+join_paths(prefix, bndl_dir, '')+'"', language : 'c')
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

c_args = ['-fvisibility=hidden',
	'-Wno-attributes',
	'-Wno-unused-function',
	'-Wno-unused-variable']

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
