# Synthpod

## Lightweight Nonlinear LV2 Plugin Container

### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/synthpod/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/synthpod/commits/master)

### About

Get more detailed information at [http://open-music-kontrollers.ch/lv2/synthpod/#](http://open-music-kontrollers.ch/lv2/synthpod/#).

### Usage

#### Server - client

Synthpod comes as server - client combo, e.g. the server doing the DSP side of things runs in its own
process and the client showing the GUI side of things runs in its own process.

By default, synthpod just runs the server. There's a command line argument to automatically run the GUI,
if you want. Please consult the manual page to find out more.

#### GUI

##### Mouse actions

* Plugin actions:
	* Mouse-over: show connections
	* Right-click: toggle selection
	* Left-click-down: start connecting
	* Left-click-up: end connecting

* Connection matrix actions:
	* Mouse-over: show connections
	* Right-click: toggle selection
	* Left-click: toggle connection
	* Mouse-wheel: toggle connection

#### Key actions

* a: (de)select all nodes
* b: start drawing selection box
* g: start moving selected nodes
* v: toggle plugin GUIs of selected nodes
* x: remove selected nodes
* i: reinstantiate selected nodes

### Mandatory dependencies

* [LV2](http://lv2plug.in) (LV2 plugin specification)
* [lilv](http://drobilla.net/software/lilv/) (LV2 plugin host library)
* [sratom](http://drobilla.net/software/sratom/) (LV2 atom serialization library)

### Optional dependencies for JACK backend

* [JACK](http://jackaudio.org/) (JACK audio connection kit)

### Optional dependencies for ALSA backend

* [ALSA](http://alsa-project.org) (Advanced Linux Sound Architecture)
* [zita-alsa-pcmi](http://kokkinizita.linuxaudio.org/linuxaudio/) (ALSA PCM high-level API)

### Optional dependencies for plugin UIs

* [libxcb](https://xcb.freedesktop.org/) (X protocol C-language Binding)
* [Gtk2](http://www.gtk.org/) (cross-platform UI toolkit)
* [Gtk3](http://www.gtk.org/) (cross-platform UI toolkit)
* [Qt4](https://www.qt.io/) (cross-platform UI toolkit)
* [Qt5](https://www.qt.io/) (cross-platform UI toolkit)

### Build / install

	git clone https://git.open-music-kontrollers.ch/lv2/synthpod
	cd synthpod 
	meson build
	cd build
	ninja -j4
	sudo ninja install

#### ArchLinux

	# mandatory build dependencies
	sudo pacman -S meson ninja lv2 lilv
	# optional build dependencies
	sudo pacman -S jack alsa zita-alsa-pcmi libxcb gtk2 gtk3 qt4 qt

### License (everything but synthpod\_alsa)

Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)

This is free software: you can redistribute it and/or modify
it under the terms of the Artistic License 2.0 as published by
The Perl Foundation.

This source is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
Artistic License 2.0 for more details.

You should have received a copy of the Artistic License 2.0
along the source as a COPYING file. If not, obtain it from
<http://www.perlfoundation.org/artistic_license_2_0>.

### License (synthpod\_alsa only)

Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
