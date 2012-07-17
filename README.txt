@file README.txt		@brief A play plugin for VDR

Copyright (c) 2012 by Johns.  All Rights Reserved.

Contributor(s):

License: AGPLv3

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

$Id: $

A play plugin for VDR


To compile you must have the 'requires' installed.

Good luck
johns

Quickstart:
-----------

Just type make and use.

Install:
--------
	1a) git

	git clone git://projects.vdr-developer.org/vdr-plugin-play.git
	cd vdr-plugin-play
	make VDRDIR=<path-to-your-vdr-files> LIBDIR=.
	gentoo: make VDRDIR=/usr/include/vdr LIBDIR=.

	2a) tarball

	Download latest version from:
	    http://projects.vdr-developer.org/projects/plg-play/files

	tar vxf vdr-play-*.tar.bz2
	cd play-*
	make VDRDIR=<path-to-your-vdr-files> LIBDIR=.

	You can edit Makefile to enable/disable some features.

Setup:	environment
------
	Following is supported:

	DISPLAY=:0.0
		x11 display name

Setup: /etc/vdr/setup.conf
------
	Following is supported:

	play.HideMainMenuEntry = 0
	0 = show play main menu entry, 1 = hide entry

Commandline:
------------

	Use vdr -h to see the command line arguments supported by the plugin.

    -a audio_device

	Selects audio output module and device.

SVDRP:
------

	Use 'svdrpsend.pl plug play HELP'
	or 'svdrpsend plug play HELP' to see the SVDRP commands help
	and which are supported by the plugin.

Keymacros:
----------

	See keymacros.conf how to setup the macros.

	This are the supported key sequences:

Running:
--------

Known Bugs:
-----------

Requires:
---------
	x11-libs/libxcb,
		X C-language Bindings library
		http://xcb.freedesktop.org
	x11-libs/xcb-util,
	x11-libs/xcb-util-wm,
	x11-libs/xcb-util-keysyms
		X C-language Bindings library
		http://xcb.freedesktop.org
		Only versions >= 0.3.8 are good supported

	media-video/mplayer
		Media Player for Linux
		http://www.mplayerhq.hu/
    or
	media-video/mplayer2
		Media Player for Linux
		http://www.mplayer2.org/

	GNU Make 3.xx
		http://www.gnu.org/software/make/make.html

Optional:
---------
