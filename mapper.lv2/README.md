# mapper.lv2

## Concurrent lock-free implementation of the LV2 URID extension

### Properties

* Is lock-free
* Uses a simplistic API
* Can map a fixed size of elements only
* Has fast URI mapping with constant expected time
* Has immediate URID unmaping with O(1)
* When combined with an rt-safe memory allocator
	* Is wait-free
	* Is rt-safe

### Reference

* <http://lv2plug.in/ns/ext/urid>
* <http://preshing.com/20130605/the-worlds-simplest-lock-free-hash-table/>
* <https://en.wikipedia.org/wiki/Linear_probing>
* <https://en.wikipedia.org/wiki/MurmurHash#MurmurHash3>

### License

Copyright (c) 2017 Hanspeter Portner (dev@open-music-kontrollers.ch)

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
