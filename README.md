## Synthpod

### Lightweight Nonlinear LV2 Plugin Container

#### Build status

[![build status](https://gitlab.com/OpenMusicKontrollers/synthpod/badges/master/build.svg)](https://gitlab.com/OpenMusicKontrollers/synthpod/commits/master)

### Binaries

For GNU/Linux (64-bit, 32-bit, armv7), Windows (64-bit, 32-bit) and MacOS
(64/32-bit univeral).

To install the plugin bundle on your system, simply copy the __synthpod__
folder out of the platform folder of the downloaded package into your
[LV2 path](http://lv2plug.in/pages/filesystem-hierarchy-standard.html).

<!--
#### Stable release

* [synthpod-0.16.0.zip](https://dl.open-music-kontrollers.ch/synthpod/stable/synthpod-0.16.0.zip) ([sig](https://dl.open-music-kontrollers.ch/synthpod/stable/synthpod-0.16.0.zip.sig))
-->

#### Unstable (nightly) release

* [synthpod-latest-unstable.zip](https://dl.open-music-kontrollers.ch/synthpod/unstable/synthpod-latest-unstable.zip) ([sig](https://dl.open-music-kontrollers.ch/synthpod/unstable/synthpod-latest-unstable.zip.sig))

### Sources

<!--
#### Stable release

* [synthpod-0.16.0.tar.xz](https://git.open-music-kontrollers.ch/lv2/synthpod/snapshot/synthpod-0.16.0.tar.xz)
-->

#### Git repository

* <https://git.open-music-kontrollers.ch/lv2/synthpod>

### Packages

* [ArchLinux](https://aur.archlinux.org/packages/synthpod-git/)

### Bugs and feature requests

* [Gitlab](https://gitlab.com/OpenMusicKontrollers/synthpod)
* [Github](https://github.com/OpenMusicKontrollers/synthpod)

### About

Synthpod is an LV2 host. It can be run as a standalone app
and be used as a tool for live performances or general audio and event filtering. 

It was conceptualized to fill the gap between pure textual 
(e.g. [SuperCollider](http://supercollider.github.io)) and
pure visual flow (e.g. [Pure Data](http://puredata.info))
audio programming paradigms.

Potential fields of application may include:

* Live audio synthesis
* Real-time event scripting
* Non-linear signal routing
* Advanced control automation
* Advanced event filtering
* Live mixing
* Live coding
* Algorithmic composition
* Interfacing to expressive controllers

The standalone host saves its state in the same format as an LV2 plugin instance,

It may be run on top of an audio system (JACK or ALSA) and
on top of an event system (MIDI and OSC). It can be run with a GUI or
headless. You can e.g. prepare a patch on your desktop machine and then
transfer it to a wearable synth.

Synthpod takes a totally modular approach whereby it provides only the
minimal necessary host infrastructure expected by a given plugin.

All additional, non strictly necessary glue shall be implemented with
plugins. Synthpod e.g. can be extended with [OSC](http://opensoundcontrol.org)
via [Eteroj](/lv2/eteroj/#). Sequencing and looping may be added via
plugins from the [Orbit](/lv2/orbit/#) bundle.
When paired with realtime scripting via [Moony](/lv2/moony/#),
it turns Synthpod into a versatile realtime programmable, remote controllable
LV2 host framework.

![Synthpod screenshot](https://git.open-music-kontrollers.ch/lv2/synthpod/plain/screenshots/screenshot_1.png)

### LV2 specifications support status

As Synthpod tries to be a lightweight LV2 host, it may not (fully) support
the more exotic extensions.
Get an up-to-date overview of current extensions support for Synthpod
in the table below.

The full LV2 specification is located at <http://lv2plug.in/ns/>.

<table>
	<tr>
		<th>Specification</th>
		<th>API</th>
		<th>Description</th>
		<th>Support status</th>
		<th>Notes</th>
	</tr><tr>
		<td>Atom</td>
		<td>atom</td>
		<td>A generic value container and several data types.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Buf Size</td>
		<td>buf-size	</td>
		<td>Access to, and restrictions on, buffer sizes.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Data Access</td>
		<td>data-access</td>
		<td>Provides access to LV2_Descriptor::extension_data().</td>
		<td style="color:#b00;">No</td>
		<td>won't, ever</td>
	</tr><tr>
		<td>Dynamic Manifest</td>
		<td>dynmanifest</td>
		<td>Support for dynamic data generation.</td>
		<td style="color:#b00;">No</td>
		<td></td>
	</tr><tr>
		<td>Event</td>
		<td>event</td>
		<td>A port-based real-time generic event interface.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Instance Access</td>
		<td>instance-access</td>
		<td>Provides access to the LV2_Handle of a plugin.</td>
		<td style="color:#b00;">No</td>
		<td>won't, ever</td>
	</tr><tr>
		<td>Log</td>
		<td>log</td>
		<td>A feature for writing log messages.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>LV2</td>
		<td>lv2core</td>
		<td>An audio plugin interface specification.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>MIDI</td>
		<td>midi</td>
		<td>A normalised definition of raw MIDI.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Morph</td>
		<td>morph</td>
		<td>Ports that can dynamically change type.</td>
		<td style="color:#b00;">No</td>
		<td></td>
	</tr><tr>
		<td>Options</td>
		<td>options</td>
		<td>Instantiation time options.</td>
		<td style="color:#0b0;">Yes</td> 
		<td></td>
	</tr><tr>
		<td>Parameters</td>
		<td>parameters</td>
		<td>Common parameters for audio processing.</td>
		<td style="color:#b00;"></td>
		<td>data-only</td>
	</tr><tr>
		<td>Patch</td>
		<td>patch</td>
		<td>Messages for accessing and manipulating properties.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Port Groups</td>
		<td>port-groups</td>
		<td>Multi-channel groups of LV2 ports.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Port Properties</td>
		<td>port-props</td>
		<td>Various port properties.</td>
		<td style="color:#b00;"></td>
		<td>data-only</td>
	</tr><tr>
		<td>Presets</td>
		<td>presets</td>
		<td>Presets for LV2 plugins.</td>
		<td style="color:#0b0;">Yes</td> 
		<td></td>
	</tr><tr>
		<td>Resize Port</td>
		<td>resize-port</td>
		<td>Dynamically sized LV2 port buffers.</td>
		<td style="color:#00b;">Partial</td> 
		<td>no dynamic resize</td>
	</tr><tr>
		<td>State</td>
		<td>state</td>
		<td>An interface for LV2 plugins to save and restore state.</td>
		<td style="color:#0b0;">Yes</td> 
		<td></td>
	</tr><tr>
		<td>Time</td>
		<td>time</td>
		<td>Properties for describing time.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>UI</td>
		<td>ui</td>
		<td>LV2 plugin UIs of any type.</td>
		<td style="color:#0b0;">Yes</td> 
		<td>X11UI, Gt2kUI, Gtk3UI, Qt4UI, Qt5UI, Show/Idle-Interface, external-ui</td>
	</tr><tr>
		<td>Units</td>
		<td>units</td>
		<td>Units for LV2 values.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>URI Map</td>
		<td>uri-map</td>
		<td>A feature for mapping URIs to integers.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>URID</td>
		<td>urid</td>
		<td>Features for mapping URIs to and from integers.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr><tr>
		<td>Worker</td>
		<td>worker</td>
		<td>Support for a non-realtime plugin worker method.</td>
		<td style="color:#0b0;">Yes</td>
		<td></td>
	</tr>
</table>

### Hosts

Currently the following hosts are contained in this software bundle:

* JACK
* ALSA
* DUMMY

#### JACK

Synthpod as host built on top of [JACK](http://jackaudio.org)
with support for native JACK audio, MIDI, OSC and CV in/out ports.
The right choice on GNU/Linux for modular setups.

This standalone host supports
[NON session management](http://non.tuxfamily.org/nsm/API.html)
to neatly integrate into modular setups.

#### ALSA

Synthpod as host built on top of [ALSA](http://alsa-project.org)
with support for native ALSA audio and MIDI sequencer in/out ports.
The right choice on GNU/Linux for live setups, embedded devices or
when you don't need audio routing to other apps.

This standalone host supports
[NON session management](http://non.tuxfamily.org/nsm/API.html)
to neatly integrate into modular setups.

#### DUMMY

Synthpod as host built on top of a dummy driver, mainly useful for
debugging purposes.

This standalone host supports
[NON session management](http://non.tuxfamily.org/nsm/API.html)
to neatly integrate into modular setups.

### Plugins
	
#### Control to CV

Convert between Control Voltage and control ports.
	
#### CV to Control

Convert between Control Voltage and control ports.

#### Heavyload

Just burn CPU cycles away for debugging.

#### Keyboard

A rudimentary graphical keyboard with a 2 octave range, mainly meant for
simple test cases.

#### MIDI splitter

Split MIDI events based on their channel.

#### Panic

Silence MIDI downstream plugins upon panic.

#### Stereo

The Synthpod LV2 non-linear plugin container run as a plugin itself in an
other host or itself. It features stereo audio in/out ports, atom event
in/out ports and 4 control in/out ports.

Use this to add support for non-linear plugin routing in a strictly
linear host.

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
