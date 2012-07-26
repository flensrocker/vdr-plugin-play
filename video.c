///
///	@file video.c	@brief Video module
///
///	Copyright (c) 2012 by Johns.  All Rights Reserved.
///
///	Contributor(s):
///
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id: $
//////////////////////////////////////////////////////////////////////////////

///
///	@defgroup Video The video module.
///
///	This module contains all video rendering functions.
///

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <libintl.h>
#define _(str) gettext(str)		///< gettext shortcut
#define _N(str) str			///< gettext_noop shortcut

#include "misc.h"
#include "video.h"

//////////////////////////////////////////////////////////////////////////////
//	X11 / XCB
//////////////////////////////////////////////////////////////////////////////

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_pixel.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>			// keysym XK_
#include <X11/XF86keysym.h>		// XF86XK_
#include <poll.h>

static xcb_connection_t *Connection;	///< xcb connection
static xcb_colormap_t VideoColormap;	///< video colormap
static xcb_window_t VideoOsdWindow;	///< video osd window
static xcb_window_t VideoPlayWindow;	///< video player window
static xcb_screen_t const *VideoScreen;	///< video screen
static uint32_t VideoBlankTick;		///< blank cursor timer
static xcb_pixmap_t VideoPixmap;	///< blank cursor pixmap
static xcb_cursor_t VideoBlankCursor;	///< empty invisible cursor

static uint32_t VideoColorKey;		///< color key pixel value

static int VideoWindowX;		///< video output window x coordinate
static int VideoWindowY;		///< video outout window y coordinate
static unsigned VideoWindowWidth;	///< video output window width
static unsigned VideoWindowHeight;	///< video output window height

///
///	Create X11 window.
///
///	@param parent	parent of new window
///	@param visual	visual of parent
///	@param depth	depth of parent
///
///	@returns created X11 window id
///
static xcb_window_t VideoCreateWindow(xcb_window_t parent,
    xcb_visualid_t visual, uint8_t depth)
{
    uint32_t values[5];
    xcb_window_t window;

    Debug(3, "video: visual %#0x depth %d\n", visual, depth);

    //
    // create color map
    //
    if (VideoColormap == XCB_NONE) {
	VideoColormap = xcb_generate_id(Connection);
	xcb_create_colormap(Connection, XCB_COLORMAP_ALLOC_NONE, VideoColormap,
	    parent, visual);
    }
    //
    //	create blank cursor
    //
    if (VideoBlankCursor == XCB_NONE) {
	VideoPixmap = xcb_generate_id(Connection);
	xcb_create_pixmap(Connection, 1, VideoPixmap, parent, 1, 1);
	VideoBlankCursor = xcb_generate_id(Connection);
	xcb_create_cursor(Connection, VideoBlankCursor, VideoPixmap,
	    VideoPixmap, 0, 0, 0, 0, 0, 0, 1, 1);
	VideoBlankTick = 0;
    }

    values[0] = VideoColorKey;		// ARGB
    values[1] = VideoColorKey;
    values[2] =
	XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
	XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
	XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE |
	XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    values[3] = VideoColormap;
    values[4] = VideoBlankCursor;
    window = xcb_generate_id(Connection);
    xcb_create_window(Connection, depth, window, parent, VideoWindowX,
	VideoWindowY, VideoWindowWidth, VideoWindowHeight, 0,
	XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
	XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK |
	XCB_CW_COLORMAP | XCB_CW_CURSOR, values);

    // define only available with xcb-utils-0.3.8
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_icccm_set_wm_name(Connection, window, XCB_ATOM_STRING, 8,
	sizeof("play control") - 1, "play control");
    xcb_icccm_set_wm_icon_name(Connection, window, XCB_ATOM_STRING, 8,
	sizeof("play control") - 1, "play control");
#endif
    // define only available with xcb-utils-0.3.6
#ifdef XCB_NUM_WM_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_set_wm_name(Connection, window, XCB_ATOM_STRING,
	sizeof("play control") - 1, "play control");
    xcb_set_wm_icon_name(Connection, window, XCB_ATOM_STRING,
	sizeof("play control") - 1, "play control");
#endif

    // FIXME: size hints

    // window above parent
    values[0] = parent;
    values[1] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(Connection, window,
	XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, values);

    return window;
}

///
///	Draw a ARGB image.
///
///	@param x	x position of image in osd
///	@param y	y position of image in osd
///	@param width	width of image
///	@param height	height of image
///	@param argb	argb image
///
void VideoDrawARGB(int x, int y, int width, int height, const uint8_t * argb)
{
    xcb_image_t *xcb_image;
    xcb_gcontext_t gc;
    int sx;
    int sy;

    if (!Connection) {
	Debug(3, "play: FIXME: must restore osd provider\n");
	return;
    }

    gc = xcb_generate_id(Connection);
    xcb_create_gc(Connection, gc, VideoOsdWindow, 0, NULL);

    xcb_image =
	xcb_image_create_native(Connection, width, height,
	XCB_IMAGE_FORMAT_Z_PIXMAP, VideoScreen->root_depth, NULL, 0L, NULL);

    //	fast 32it versions
    if (xcb_image->bpp == 32) {
	if (xcb_image->byte_order == XCB_IMAGE_ORDER_LSB_FIRST) {
	    for (sy = 0; sy < height; ++sy) {
		for (sx = 0; sx < width; ++sx) {
		    uint32_t pixel;

		    if (argb[(width * sy + sx) * 4 + 3] < 200) {
			pixel = VideoColorKey;
		    } else {
			pixel = argb[(width * sy + sx) * 4 + 0] << 0;
			pixel |= argb[(width * sy + sx) * 4 + 1] << 8;
			pixel |= argb[(width * sy + sx) * 4 + 2] << 16;
		    }
		    xcb_image_put_pixel_Z32L(xcb_image, sx, sy, pixel);
		}
	    }
	} else {
	    Error(_("play: unsupported put_image\n"));
	}
    } else {
	for (sy = 0; sy < height; ++sy) {
	    for (sx = 0; sx < width; ++sx) {
		uint32_t pixel;

		if (argb[(width * sy + sx) * 4 + 3] < 200) {
		    pixel = argb[(width * sy + sx) * 4 + 3] << 0;
		    pixel |= argb[(width * sy + sx) * 4 + 3] << 8;
		    pixel |= argb[(width * sy + sx) * 4 + 3] << 16;
		} else {
		    pixel = argb[(width * sy + sx) * 4 + 0] << 0;
		    pixel |= argb[(width * sy + sx) * 4 + 1] << 8;
		    pixel |= argb[(width * sy + sx) * 4 + 2] << 16;
		}
		xcb_image_put_pixel(xcb_image, sx, sy, pixel);
	    }
	}
    }

    // render xcb_image to color data pixmap
    xcb_image_put(Connection, VideoOsdWindow, gc, xcb_image, x, y, 0);
    // release xcb_image
    xcb_image_destroy(xcb_image);
    xcb_free_gc(Connection, gc);
    xcb_flush(Connection);
}

///
///	Show window.
///
void VideoWindowShow(void)
{
    if (!Connection) {
	Debug(3, "play: FIXME: must restore osd provider\n");
	return;
    }
    xcb_map_window(Connection, VideoOsdWindow);
}

///
///	Hide window.
///
void VideoWindowHide(void)
{
    if (!Connection) {
	Debug(3, "play: FIXME: must restore osd provider\n");
	return;
    }
    xcb_unmap_window(Connection, VideoOsdWindow);
}

///
///	Clear window.
///
void VideoWindowClear(void)
{
    if (!Connection) {
	Debug(3, "play: FIXME: must restore osd provider\n");
	return;
    }
    xcb_clear_area(Connection, 0, VideoOsdWindow, 0, 0, VideoWindowWidth,
	VideoWindowHeight);
    xcb_flush(Connection);
}

static xcb_key_symbols_t *XcbKeySymbols;	///< Keyboard symbols
static uint16_t NumLockMask;		///< mod mask for num-lock
static uint16_t ShiftLockMask;		///< mod mask for shift-lock
static uint16_t CapsLockMask;		///< mod mask for caps-lock
static uint16_t ModeSwitchMask;		///< mod mask for mode-switch

///
///	Handle key press event.
///
static void VideoKeyPress(const xcb_key_press_event_t * event)
{
    char buf[2];
    xcb_keysym_t ks0;
    xcb_keysym_t ks1;
    xcb_keysym_t keysym;
    xcb_keycode_t keycode;
    unsigned modifier;

    if (!XcbKeySymbols) {
	XcbKeySymbols = xcb_key_symbols_alloc(Connection);
	if (!XcbKeySymbols) {
	    Error(_("play/event: can't read key symbols\n"));
	    return;
	}
	NumLockMask = ShiftLockMask = CapsLockMask = ModeSwitchMask = 0;

	// FIXME: lock and mode keys are not prepared!
    }

    keycode = event->detail;
    modifier = event->state;
    // handle mode-switch
    if (modifier & ModeSwitchMask) {
	ks0 = xcb_key_symbols_get_keysym(XcbKeySymbols, keycode, 2);
	ks1 = xcb_key_symbols_get_keysym(XcbKeySymbols, keycode, 3);
    } else {
	ks0 = xcb_key_symbols_get_keysym(XcbKeySymbols, keycode, 0);
	ks1 = xcb_key_symbols_get_keysym(XcbKeySymbols, keycode, 1);
    }
    // use first keysym, if second keysym didn't exists
    if (ks1 == XCB_NO_SYMBOL) {
	ks1 = ks0;
    }
    // see xcb-util-0.3.6/keysyms/keysyms.c:
    if (!(modifier & XCB_MOD_MASK_SHIFT) && !(modifier & XCB_MOD_MASK_LOCK)) {
	keysym = ks0;
    } else {
	// FIXME: more cases

	keysym = ks0;
    }

    // FIXME: use xcb_lookup_string
    switch (keysym) {
	case XK_space:
	    FeedKeyPress("XKeySym", "space", 0, 0);
	    break;
	case XK_exclam ... XK_slash:
	case XK_0 ... XK_9:
	case XK_A ... XK_Z:
	case XK_a ... XK_z:
	    buf[0] = keysym;
	    buf[1] = '\0';
	    FeedKeyPress("XKeySym", buf, 0, 0);
	    break;

	case XK_BackSpace:
	    FeedKeyPress("XKeySym", "BackSpace", 0, 0);
	    break;
	case XK_Tab:
	    FeedKeyPress("XKeySym", "Tab", 0, 0);
	    break;
	case XK_Return:
	    FeedKeyPress("XKeySym", "Return", 0, 0);
	    break;
	case XK_Escape:
	    FeedKeyPress("XKeySym", "Escape", 0, 0);
	    break;
	case XK_Delete:
	    FeedKeyPress("XKeySym", "Delete", 0, 0);
	    break;

	case XK_Home:
	    FeedKeyPress("XKeySym", "Home", 0, 0);
	    break;
	case XK_Left:
	    FeedKeyPress("XKeySym", "Left", 0, 0);
	    break;
	case XK_Up:
	    FeedKeyPress("XKeySym", "Up", 0, 0);
	    break;
	case XK_Right:
	    FeedKeyPress("XKeySym", "Right", 0, 0);
	    break;
	case XK_Down:
	    FeedKeyPress("XKeySym", "Down", 0, 0);
	    break;
	case XK_Page_Up:
	    FeedKeyPress("XKeySym", "Page_Up", 0, 0);
	    break;
	case XK_Page_Down:
	    FeedKeyPress("XKeySym", "Page_Down", 0, 0);
	    break;
	case XK_End:
	    FeedKeyPress("XKeySym", "End", 0, 0);
	    break;
	case XK_Begin:
	    FeedKeyPress("XKeySym", "Begin", 0, 0);
	    break;

	case XK_F1:
	    FeedKeyPress("XKeySym", "F1", 0, 0);
	    break;
	case XK_F2:
	    FeedKeyPress("XKeySym", "F2", 0, 0);
	    break;
	case XK_F3:
	    FeedKeyPress("XKeySym", "F3", 0, 0);
	    break;
	case XK_F4:
	    FeedKeyPress("XKeySym", "F4", 0, 0);
	    break;
	case XK_F5:
	    FeedKeyPress("XKeySym", "F5", 0, 0);
	    break;
	case XK_F6:
	    FeedKeyPress("XKeySym", "F6", 0, 0);
	    break;
	case XK_F7:
	    FeedKeyPress("XKeySym", "F7", 0, 0);
	    break;
	case XK_F8:
	    FeedKeyPress("XKeySym", "F8", 0, 0);
	    break;
	case XK_F9:
	    FeedKeyPress("XKeySym", "F9", 0, 0);
	    break;
	case XK_F10:
	    FeedKeyPress("XKeySym", "F10", 0, 0);
	    break;
	case XK_F11:
	    FeedKeyPress("XKeySym", "F11", 0, 0);
	    break;
	case XK_F12:
	    FeedKeyPress("XKeySym", "F12", 0, 0);
	    break;

	case XF86XK_Red:
	    FeedKeyPress("XKeySym", "Red", 0, 0);
	    break;
	case XF86XK_Green:
	    FeedKeyPress("XKeySym", "Green", 0, 0);
	    break;
	case XF86XK_Yellow:
	    FeedKeyPress("XKeySym", "Yellow", 0, 0);
	    break;
	case XF86XK_Blue:
	    FeedKeyPress("XKeySym", "Blue", 0, 0);
	    break;

	case XF86XK_AudioLowerVolume:
	    FeedKeyPress("XKeySym", "AudioLowerVolume", 0, 0);
	    break;
	case XF86XK_AudioMute:
	    FeedKeyPress("XKeySym", "AudioMute", 0, 0);
	    break;
	case XF86XK_AudioRaiseVolume:
	    FeedKeyPress("XKeySym", "AudioRaiseVolume", 0, 0);
	    break;
	case XF86XK_AudioPlay:
	    FeedKeyPress("XKeySym", "AudioPlay", 0, 0);
	    break;
	case XF86XK_AudioStop:
	    FeedKeyPress("XKeySym", "AudioStop", 0, 0);
	    break;
	case XF86XK_AudioPrev:
	    FeedKeyPress("XKeySym", "AudioPrev", 0, 0);
	    break;
	case XF86XK_AudioNext:
	    FeedKeyPress("XKeySym", "AudioNext", 0, 0);
	    break;

	default:
	    Debug(3, "play/event: keycode %d\n", event->detail);
	    break;

    }
}

///
///	Poll video events.
///
///	@param timeout	timeout in milliseconds
///
void VideoPollEvents(int timeout)
{
    struct pollfd fds[1];
    xcb_generic_event_t *event;
    int n;
    int delay;

    if (!Connection) {
	Debug(3, "play: poll without connection\n");
	return;
    }

    fds[0].fd = xcb_get_file_descriptor(Connection);
    fds[0].events = POLLIN | POLLPRI;

    delay = timeout;
    for (;;) {
	xcb_flush(Connection);

	// wait for events or timeout
	if ((n = poll(fds, 1, delay)) <= 0) {
	    // error or timeout
	    if (n) {			// error
		Error(_("play/event: poll failed: %s\n"), strerror(errno));
	    }
	    return;
	}
	if (fds[0].revents & (POLLIN | POLLPRI)) {
	    if ((event = xcb_poll_for_event(Connection))) {

		switch (XCB_EVENT_RESPONSE_TYPE(event)) {
#if 0
			// background pixmap no need to redraw
		    case XCB_EXPOSE:
			// collapse multi expose
			if (!((xcb_expose_event_t *) event)->count) {
			    xcb_clear_area(Connection, 0, Window, 0, 0, 64,
				64);
			    // flush the request
			    xcb_flush(Connection);
			}
			break;
#endif
		    case XCB_MAP_NOTIFY:
			Debug(3, "video/event: MapNotify\n");
			// hide cursor after mapping
			xcb_change_window_attributes(Connection,
			    VideoOsdWindow, XCB_CW_CURSOR, &VideoBlankCursor);
			xcb_change_window_attributes(Connection,
			    VideoPlayWindow, XCB_CW_CURSOR, &VideoBlankCursor);
			break;
		    case XCB_DESTROY_NOTIFY:
			return;
		    case XCB_KEY_PRESS:
			VideoKeyPress((xcb_key_press_event_t *) event);
			break;
		    case XCB_KEY_RELEASE:
		    case XCB_BUTTON_PRESS:
		    case XCB_BUTTON_RELEASE:
			break;
		    case XCB_MOTION_NOTIFY:
			break;

		    case 0:
			// error_code
			Debug(3, "play/event: error %x\n",
			    event->response_type);
			break;
		    default:
			// unknown event type, ignore it
			Debug(3, "play/event: unknown %x\n",
			    event->response_type);
			break;
		}

		free(event);
	    } else {
		// no event, can happen, but we must check for close
		if (xcb_connection_has_error(Connection)) {
		    return;
		}
	    }
	}
    }
}

///
///	Get OSD size.
///
///	@param[out] width	OSD width
///	@param[out] height	OSD height
///
void VideoGetOsdSize(int *width, int *height)
{
    *width = 1920;
    *height = 1080;			// unknown default
    if (VideoWindowWidth && VideoWindowHeight) {
	*width = VideoWindowWidth;
	*height = VideoWindowHeight;
    }
}

///
///	Get player video window id.
///
int VideoGetPlayWindow(void)
{
    return VideoPlayWindow;
}

///
///	Set video geometry.
///
///	@todo write/search the good version
///
void VideoSetGeometry(const char *geometry)
{
    sscanf(geometry, "%dx%d%d%d", &VideoWindowWidth, &VideoWindowHeight,
	&VideoWindowX, &VideoWindowY);
}

///
///	Set video color key.
///
///	Should be called before VideoInit().
///
void VideoSetColorKey(uint32_t color_key)
{
    VideoColorKey = color_key;
}

///
///	Initialize video.
///
int VideoInit(const char *display)
{
    const char *display_name;
    xcb_connection_t *connection;
    xcb_screen_iterator_t iter;
    int screen_nr;
    int i;

    display_name = display ? display : getenv("DISPLAY");

    //	Open the connection to the X server.
    connection = xcb_connect(display_name, &screen_nr);
    if (!connection || xcb_connection_has_error(connection)) {
	fprintf(stderr, "play: can't connect to X11 server on %s\n",
	    display_name);
	return -1;
    }
    Connection = connection;

    //	Get the requested screen number
    iter = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (i = 0; i < screen_nr; ++i) {
	xcb_screen_next(&iter);
    }
    VideoScreen = iter.data;

    //
    //	Default window size
    //
    if (!VideoWindowHeight) {
	if (VideoWindowWidth) {
	    VideoWindowHeight = (VideoWindowWidth * 9) / 16;
	} else {			// default to fullscreen
	    VideoWindowHeight = VideoScreen->height_in_pixels;
	    VideoWindowWidth = VideoScreen->width_in_pixels;
	}
    }
    if (!VideoWindowWidth) {
	VideoWindowWidth = (VideoWindowHeight * 16) / 9;
    }

    VideoPlayWindow =
	VideoCreateWindow(VideoScreen->root, VideoScreen->root_visual,
	VideoScreen->root_depth);
    xcb_map_window(Connection, VideoPlayWindow);
    VideoOsdWindow =
	VideoCreateWindow(VideoPlayWindow, VideoScreen->root_visual,
	VideoScreen->root_depth);
    Debug(3, "play: osd %x, play %x\n", VideoOsdWindow, VideoPlayWindow);

    VideoWindowClear();
    // done by clear: xcb_flush(Connection);

    return 0;
}

///
///	Cleanup video.
///
void VideoExit(void)
{
    if (VideoOsdWindow != XCB_NONE) {
	xcb_destroy_window(Connection, VideoOsdWindow);
	VideoOsdWindow = XCB_NONE;
    }
    if (VideoPlayWindow != XCB_NONE) {
	xcb_destroy_window(Connection, VideoPlayWindow);
	VideoPlayWindow = XCB_NONE;
    }
    if (VideoColormap != XCB_NONE) {
	xcb_free_colormap(Connection, VideoColormap);
	VideoColormap = XCB_NONE;
    }
    if (VideoBlankCursor != XCB_NONE) {
	xcb_free_cursor(Connection, VideoBlankCursor);
	VideoBlankCursor = XCB_NONE;
    }
    if (VideoPixmap != XCB_NONE) {
	xcb_free_pixmap(Connection, VideoPixmap);
	VideoPixmap = XCB_NONE;
    }

    if (Connection) {
	xcb_flush(Connection);
	xcb_disconnect(Connection);
	Connection = NULL;
    }
}
