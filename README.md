# Synthpod

## Lightweight Nonlinear LV2 Plugin Container

### Build status

[![Build Status](https://travis-ci.org/OpenMusicKontrollers/synthpod.svg?branch=master)](https://travis-ci.org/OpenMusicKontrollers/synthpod)

### About

Get more detailed information at [http://open-music-kontrollers.ch/lv2/synthpod/#](http://open-music-kontrollers.ch/lv2/synthpod/#).

### Dependencies

* [ALSA](http://alsa-project.org) (Advanced Linux Sound Architecture)
* [Elementary](http://docs.enlightenment.org/auto/elementary/) (EFL UI toolkit)
* [lilv](http://drobilla.net/software/lilv/) (LV2 plugin host library)
* [JACK](http://jackaudio.org/) (JACK audio connection kit)
* [zita-alsa-pcmi](http://kokkinizita.linuxaudio.org/linuxaudio/) (ALSA PCM high-level API)
* [LV2](http://lv2plug.in) (LV2 plugin specification)

### Build / install

	git clone https://github.com/OpenMusicKontrollers/synthpod.git
	cd synthpod 
	mkdir build
	cd build
	cmake ..
	make
	sudo make install

#### ArchLinux

	# build dependencies
	sudo pacman -S cmake alsa elementary lilv jack zita-alsa-pcmi lv2

#### Debian Jessie, Ubuntu Vivid and derivatives

	# build dependencies
	sudo apt-get install cmake git libasound2-dev libelementary-dev libevas1-engines-x liblilv-dev libjack-dev libzita-alsa-pcmi-dev lv2-dev

#### Ubuntu (<= 14.04)

	# synthpod needs libefl(>=1.8) and libelementary(>=1.8), you may thus need to add a ppa
	sudo add-apt-repository -y ppa:enlightenment-git/ppa

#### Fedora 22 and derivatives

	# build dependencies
	sudo dnf install dnf-plugins-core
	sudo dnf config-manager --add-repo http://download.opensuse.org/repositories/home:/edogawa/Fedora_22/home:edogawa.repo
	sudo dnf install alsa-lib-devel cmake elementary-devel gcc gcc-c++ git lilv-devel lv2-devel make jack-audio-connection-kit-devel libzita-alsa-pcmi0-devel

#### OpenSuse 13.2
	
	# build dependencies
	sudo zypper addrepo http://download.opensuse.org/repositories/home:/edogawa/openSUSE_13.2/home:edogawa.repo
	sudo zypper addrepo http://download.opensuse.org/repositories/home:/rncbc/openSUSE_13.2/home:rncbc.repo
	sudo zypper install cmake gcc gcc-c++ git alsa-devel elementary-devel liblilv-devel libjack-devel libzita-alsa-pcmi0-devel lv2-devel pkg-config

### License (everything but synthpod\_alsa)

Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)

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

Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)

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
