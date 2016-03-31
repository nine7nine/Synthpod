# Synthpod

## Lightweight Nonlinear LV2 Plugin Container

### Build status

[![Build Status](https://travis-ci.org/OpenMusicKontrollers/synthpod.svg?branch=master)](https://travis-ci.org/OpenMusicKontrollers/synthpod)

### About

Get more detailed information at [http://open-music-kontrollers.ch/lv2/synthpod/#](http://open-music-kontrollers.ch/lv2/synthpod/#).

### Mandatory dependencies

* [Elementary](http://docs.enlightenment.org/auto/elementary/) (EFL UI toolkit)
* [lilv](http://drobilla.net/software/lilv/) (LV2 plugin host library)
* [LV2](http://lv2plug.in) (LV2 plugin specification)

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

	git clone https://github.com/OpenMusicKontrollers/synthpod.git
	cd synthpod 
	mkdir build
	cd build
	cmake ..
	make
	sudo make install

#### ArchLinux

	# mandatory build dependencies
	sudo pacman -S cmake elementary lilv lv2
	# optional build dependencies
	sudo pacman -S jack alsa zita-alsa-pcmi nanomsg libxcb gtk2 gtk3 qt4 qt

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
