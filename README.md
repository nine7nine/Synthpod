# Synthpod

## Lightweight Nonlinear LV2 Plugin Container

### Build status

[![Build Status](https://travis-ci.org/OpenMusicKontrollers/synthpod.svg?branch=master)](https://travis-ci.org/OpenMusicKontrollers/synthpod)

### About

### Dependencies

* [lv2](http://lv2plug.in) (LV2 plugin specification)
* [lilv](http://drobilla.net/software/lilv/) (LV2 plugin host library)
* [JACK](http://jackaudio.org/) (JACK audio connection kit)
* [Elementary](http://docs.enlightenment.org/auto/elementary/) (EFL UI toolkit)

### Build / install

	git clone https://github.com/OpenMusicKontrollers/synthpod.git
	cd synthpod 
	mkdir build
	cd build
	cmake ..
	make
	sudo make install

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
