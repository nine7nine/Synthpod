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

	# build/runtime dependencies
	sudo pacman -S cmake alsa elementary lilv jack zita-alsa-pcmi lv2

#### Debian (>= jessie) and derivatives (Ubuntu, Mint, ...)

	# build dependencies
	sudo apt-get install cmake libasound2-dev libelementary-dev liblilv-dev libjack-dev libzita-alsa-pcmi-dev lv2-dev
	# runtime dependencies
	sudo apt-get install libasound2 libelementary2 libevas1-engines-x liblilv-0-0 jackd libzita-alsa-pcmi0

#### Ubuntu (<= 14.04)

	# synthpod needs libefl(>=1.8) and libelementary(>=1.8), you may thus need to add a ppa
	sudo add-apt-repository -y ppa:enlightenment-git/ppa

### License

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
