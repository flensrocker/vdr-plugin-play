///
///	@file video.h	@brief Video module header file
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
///	$Id: f027c9f4ca685306339c889cfc39227d9b711f97 $
//////////////////////////////////////////////////////////////////////////////

/// @addtogroup Video
/// @{

    /// C callback feed key press
extern void FeedKeyPress(const char *, const char *, int, int);

    /// Show window.
extern void VideoWindowShow(void);

    /// Hide window.
extern void VideoWindowHide(void);

    /// Clear window.
extern void VideoWindowClear(void);

    /// Poll video events.
extern void VideoPollEvents(int);

    /// Get player window id.
extern int VideoGetPlayWindow(void);

    /// Draw an OSD ARGB image.
extern void VideoDrawARGB(int, int, int, int, const uint8_t *);

    /// Get OSD size.
extern void VideoGetOsdSize(int *, int *);

    /// Set video geometry.
extern void VideoSetGeometry(const char *);

    /// Set video color key.
extern void VideoSetColorKey(uint32_t);

extern int VideoInit(const char *);	///< Setup video module.
extern void VideoExit(void);		///< Cleanup and exit video module.

/// @}
