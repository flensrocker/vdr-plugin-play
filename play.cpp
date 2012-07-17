///
///	@file play.cpp		@brief A play plugin for VDR.
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
///	$Id: 917da02a3e4a10a0b3220742dd2710ca03013871 $
//////////////////////////////////////////////////////////////////////////////

#include <vdr/interface.h>
#include <vdr/plugin.h>
#include <vdr/player.h>
#include <vdr/osd.h>
#include <vdr/shutdown.h>
#include <vdr/status.h>
#include <vdr/videodir.h>

#ifdef HAVE_CONFIG
#include "config.h"
#endif

//////////////////////////////////////////////////////////////////////////////

    /// vdr-plugin version number.
    /// Makefile extracts the version number for generating the file name
    /// for the distribution archive.
static const char *const VERSION = "0.0.6"
#ifdef GIT_REV
    "-GIT" GIT_REV
#endif
    ;

    /// vdr-plugin description.
static const char *const DESCRIPTION = trNOOP("A play plugin");

    /// vdr-plugin text of main menu entry
static const char *MAINMENUENTRY = trNOOP("Play");

//////////////////////////////////////////////////////////////////////////////

#define Debug(level, fmt...)	printf(fmt)
#define Warning(fmt...)		fprintf(stderr, fmt)
#define Error(fmt...)		fprintf(stderr, fmt)

//////////////////////////////////////////////////////////////////////////////

static char ConfigHideMainMenuEntry;	///< hide main menu entry
static char ConfigOsdOverlay;		///< show osd overlay
static char ConfigUseSlave;		///< external player use slave mode
static char ConfigFullscreen;		///< external player uses fullscreen
static char *ConfigVideoOut;		///< video out device
static char *ConfigAudioOut;		///< audio out device
static char *ConfigAudioMixer;		///< audio mixer device
static char *ConfigMixerChannel;	///< audio mixer channel
static const char *ConfigMplayer = "/usr/bin/mplayer";	///< mplayer executable
static const char *ConfigX11Display = ":0.0";	///< x11 display

//////////////////////////////////////////////////////////////////////////////
//	X11
//////////////////////////////////////////////////////////////////////////////

#include <xcb/xcb.h>

static xcb_connection_t *Connection;	///< xcb connection
static xcb_colormap_t VideoColormap;	///< video colormap
static xcb_window_t VideoWindow;	///< video window
static xcb_screen_t const *VideoScreen;	///< video screen
static uint32_t VideoBlankTick;		///< blank cursor timer
static xcb_cursor_t VideoBlankCursor;	///< empty invisible cursor

static int VideoWindowX;		///< video output window x coordinate
static int VideoWindowY;		///< video outout window y coordinate
static unsigned VideoWindowWidth;	///< video output window width
static unsigned VideoWindowHeight;	///< video output window height

///
///	Create main window.
///
///	@param parent	parent of new window
///	@param visual	visual of parent
///	@param depth	depth of parent
///
static void VideoCreateWindow(xcb_window_t parent, xcb_visualid_t visual,
    uint8_t depth)
{
    uint32_t values[4];
    xcb_pixmap_t pixmap;
    xcb_cursor_t cursor;

    Debug(3, "video: visual %#0x depth %d\n", visual, depth);

    // Color map
    VideoColormap = xcb_generate_id(Connection);
    xcb_create_colormap(Connection, XCB_COLORMAP_ALLOC_NONE, VideoColormap,
	parent, visual);

    values[0] = 0x00020507;		// ARGB
    values[1] = 0x00020507;
    values[2] =
	XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
	XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
	XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_EXPOSURE |
	XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    values[3] = VideoColormap;
    VideoWindow = xcb_generate_id(Connection);
    xcb_create_window(Connection, depth, VideoWindow, parent, VideoWindowX,
	VideoWindowY, VideoWindowWidth, VideoWindowHeight, 0,
	XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
	XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK |
	XCB_CW_COLORMAP, values);

    // define only available with xcb-utils-0.3.8
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_icccm_set_wm_name(Connection, VideoWindow, XCB_ATOM_STRING, 8,
	sizeof("play control") - 1, "play control");
    xcb_icccm_set_wm_icon_name(Connection, VideoWindow, XCB_ATOM_STRING, 8,
	sizeof("play control") - 1, "play control");
#endif
    // define only available with xcb-utils-0.3.6
#ifdef XCB_NUM_WM_HINTS_ELEMENTS
    // FIXME: utf _NET_WM_NAME
    xcb_set_wm_name(Connection, VideoWindow, XCB_ATOM_STRING,
	sizeof("play control") - 1, "play control");
    xcb_set_wm_icon_name(Connection, VideoWindow, XCB_ATOM_STRING,
	sizeof("play control") - 1, "play control");
#endif

    // FIXME: size hints
    xcb_map_window(Connection, VideoWindow);

    //
    //	hide cursor
    //
    pixmap = xcb_generate_id(Connection);
    xcb_create_pixmap(Connection, 1, pixmap, parent, 1, 1);
    cursor = xcb_generate_id(Connection);
    xcb_create_cursor(Connection, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 1,
	1);

    values[0] = cursor;
    xcb_change_window_attributes(Connection, VideoWindow, XCB_CW_CURSOR,
	values);
    VideoBlankCursor = cursor;
    VideoBlankTick = 0;
    // FIXME: free colormap/cursor/pixmap needed?
}

///
///	Initialize video.
///
int VideoInit(void)
{
    const char *display_name;
    xcb_connection_t *connection;
    xcb_screen_iterator_t iter;
    int screen_nr;
    int i;

    display_name = ConfigX11Display ? ConfigX11Display : getenv("DISPLAY");

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

    VideoWindowX = 0;
    VideoWindowY = 0;
    VideoWindowWidth = 512;
    VideoWindowHeight = 512;

    VideoCreateWindow(VideoScreen->root, VideoScreen->root_visual,
	VideoScreen->root_depth);

    xcb_flush(Connection);

    return 0;
}

///
///	Cleanup video.
///
void VideoExit(void)
{
    if (VideoWindow != XCB_NONE) {
	xcb_destroy_window(Connection, VideoWindow);
	VideoWindow = XCB_NONE;
    }
    if (VideoColormap != XCB_NONE) {
	xcb_free_colormap(Connection, VideoColormap);
	VideoColormap = XCB_NONE;
    }
    if (Connection) {
	xcb_flush(Connection);
	xcb_disconnect(Connection);
	Connection = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//	Menu
//////////////////////////////////////////////////////////////////////////////

#include <sys/types.h>
#include <sys/wait.h>

extern "C"
{
    typedef struct __name_filter_
    {
	int Length;			///< filter string length
	const char *String;		///< filter string
    } NameFilter;			///< browser name filter typedef

    const char ConfigShowHiddenFiles = 0;	///< config show hidden files
    const char *BaseDir;		///< current directory
    const NameFilter *NameFilters;	///< current name filter table

/**
**	Check if directory.
*/
    int IsDirectory(const char *filename)
    {
	struct stat stat_buf;

	if (stat(filename, &stat_buf) < 0)
	{
	    Error("play/menu: can't stat '%s': %s\n", filename,
		strerror(errno));
	    return -1;
	}
	return S_ISDIR(stat_buf.st_mode);
    }

/**
**	Filter for scandir, only directories.
**
**	@param dirent	current directory entry
**
**	@returns true if the @p dirent is a directories.
*/
    static int FilterIsDir(const struct dirent *dirent)
    {
	char *tmp;
	int dir;
	size_t len;

	len = _D_EXACT_NAMLEN(dirent);
	if (len && dirent->d_name[0] == '.') {
	    // hide hidden files
	    if (!ConfigShowHiddenFiles) {
		return 0;
	    }
	    // ignore . and ..
	    if (len == 1 || (len == 2 && dirent->d_name[1] == '.')) {
		return 0;
	    }
	}
#ifdef _DIRENT_HAVE_D_TYPE
	if (dirent->d_type == DT_DIR) {	// only directories files
	    return 1;
#ifdef DT_LNK
	} else if (dirent->d_type == DT_LNK) {	// symbolic link
#endif
	} else if (dirent->d_type != DT_UNKNOWN) {	// no looser filesystem
	    return 0;
	}
#endif

	// DT_UNKOWN or DT_LNK
	tmp = (char *)malloc(strlen(BaseDir) + strlen(dirent->d_name) + 1);
	stpcpy(stpcpy(tmp, BaseDir), dirent->d_name);
	dir = IsDirectory(tmp);
	free(tmp);
	return dir;
    }

/**
**	Filter for scandir, only files.
**
**	@param dirent	current directory entry
**
**	@returns true if the @p dirent is a video.
*/
    static int FilterIsFile(const struct dirent *dirent)
    {
	char *tmp;
	int dir;
	int len;
	int i;

	len = _D_EXACT_NAMLEN(dirent);
	if (len && dirent->d_name[0] == '.') {
	    if (!ConfigShowHiddenFiles) {	// hide hidden files
		return 0;
	    }
	}
	// look through name filter table
	if (NameFilters) {
	    for (i = 0; NameFilters[i].String; ++i) {
		if (len >= NameFilters[i].Length
		    && !strcasecmp(dirent->d_name + len -
			NameFilters[i].Length, NameFilters[i].String)) {
		    goto found;
		}
	    }
	    // no file name matched
	    return 0;
	}
      found:

#ifdef _DIRENT_HAVE_D_TYPE
	if (dirent->d_type == DT_REG) {	// only regular files
	    return 1;
#ifdef DT_LNK
	} else if (dirent->d_type == DT_LNK) {	// symbolic link
#endif
	} else if (dirent->d_type != DT_UNKNOWN) {	// no looser filesystem
	    return 0;
	}
#endif

	// DT_UNKOWN or DT_LNK
	tmp = (char *)malloc(strlen(BaseDir) + strlen(dirent->d_name) + 1);
	stpcpy(stpcpy(tmp, BaseDir), dirent->d_name);
	dir = IsDirectory(tmp);
	free(tmp);
	return !dir;
    }

/**
**	Read directory for menu.
**
**	@param name	directory name
**
**	@retval <0	if any error occurs
**	@retval false	if no error occurs
*/
    int ReadDirectory(const char *name, int flag_dir,
	const NameFilter * filter, void (*cb_add) (void *, const char *),
	void *opaque)
    {
	struct dirent **namelist;
	int n;
	int i;

	Debug(3, "play/menu: read directory '%s'\n", name);

	BaseDir = name;
	NameFilters = filter;

	n = scandir(name, &namelist, flag_dir ? FilterIsDir : FilterIsFile,
	    alphasort);
	if (n < 0) {
	    Error("play/menu: can't scan dir '%s': %s\n", name,
		strerror(errno));
	    return n;
	}
	for (i = 0; i < n; ++i) {
	    //Debug(3, "play/menu:\tadd '%s'\n", namelist[i]->d_name);
	    cb_add(opaque, namelist[i]->d_name);
	    free(namelist[i]);
	}
	free(namelist);
	BaseDir = NULL;

	return 0;
    }

/**
**	Return command line help string.
*/
    const char *CommandLineHelp(void)
    {
	return
	    "  -a audio\tmplayer -ao (alsa:device=hw=0.0) overwrites mplayer.conf\n"
	    "  -d display\tX11 display (default :0.0) overwrites $DISPLAY\n"
	    "  -f\t\tmplayer fullscreen playback\n"
	    "  -m mplayer\tfilename of mplayer executable\n"
	    "  -o\t\tosd overlay experiments\n" "  -s\t\tmplayer slave mode\n"
	    "  -v video\tmplayer -vo (vdpau:deint=4,hqscaling=1) overwrites mplayer.conf\n";
    }

/**
**	Process the command line arguments.
**
**	@param argc	number of arguments
**	@param argv	arguments vector
*/
    int ProcessArgs(int argc, char *const argv[])
    {
	const char *s;

	//
	//  Parse arguments.
	//
#ifdef __FreeBSD__
	if (!strcmp(*argv, "play")) {
	    ++argv;
	    --argc;
	}
#endif
	if ((s = getenv("DISPLAY"))) {
	    ConfigX11Display = s;
	}

	for (;;) {
	    switch (getopt(argc, argv, "-a:d:fm:osv:")) {
		case 'a':		// audio out
		    ConfigAudioOut = optarg;
		    continue;
		case 'd':		// display x11
		    ConfigX11Display = optarg;
		    continue;
		case 'f':		// fullscreen mode
		    ConfigFullscreen = 1;
		    continue;
		case 'm':		// mplayer executable
		    ConfigMplayer = optarg;
		    continue;
		case 'o':		// osd / overlay
		    ConfigOsdOverlay = 1;
		    continue;
		case 's':		// slave mode
		    ConfigUseSlave = 1;
		    continue;
		case 'v':		// video out
		    ConfigVideoOut = optarg;
		    continue;
		case EOF:
		    break;
		case '-':
		    fprintf(stderr, tr("We need no long options\n"));
		    return 0;
		case ':':
		    fprintf(stderr, tr("Missing argument for option '%c'\n"),
			optopt);
		    return 0;
		default:
		    fprintf(stderr, tr("Unkown option '%c'\n"), optopt);
		    return 0;
	    }
	    break;
	}
	while (optind < argc) {
	    fprintf(stderr, tr("Unhandled argument '%s'\n"), argv[optind++]);
	}

	return 1;
    }
}

//////////////////////////////////////////////////////////////////////////////
//	Slave
//////////////////////////////////////////////////////////////////////////////

static pid_t PlayerPid;			///< player pid
static int PipeOut[2];			///< player write pipe
static int PipeIn[2];			///< player read pipe
static char DvdNav;			///< dvdnav active
static int PlayerVolume = -1;		///< volume 0 - 100

/**
**	Poll input pipe.
*/
void PollPipe(void)
{
    pollfd poll_fds[1];
    char buf[1024];
    int n;

    // check if something to read
    poll_fds[0].fd = PipeOut[0];
    poll_fds[0].events = POLLIN;

    switch (poll(poll_fds, 1, 0)) {
	case 0:			// timeout
	    return;
	case -1:			// error
	    printf("player: poll failed: %s\n", strerror(errno));
	    return;
	default:			// ready
	    break;
    }

    if ((n = read(PipeOut[0], buf, sizeof(buf))) < 0) {
	printf("player: read failed: %s\n", strerror(errno));
	return;
    }
    printf("player: %.*s\n", n, buf);
}

/**
**	Execute external player.
*/
void ExecPlayer(const char *filename)
{
    pid_t pid;

    if (ConfigUseSlave) {
	if (pipe(PipeIn)) {
	    printf("play: pipe failed: %s\n", strerror(errno));
	    return;
	}
	if (pipe(PipeOut)) {
	    printf("play: pipe failed: %s\n", strerror(errno));
	    return;
	}
    }

    if ((pid = fork()) == -1) {
	printf("play: fork failed: %s\n", strerror(errno));
	return;
    }
    if (!pid) {				// child
	const char *args[32];
	int argn;
	char volume_buf[32];
	int i;

	if (ConfigUseSlave) {		// connect pipe to stdin/stdout
	    dup2(PipeIn[0], STDIN_FILENO);
	    close(PipeIn[0]);
	    close(PipeIn[1]);
	    close(PipeOut[0]);
	    dup2(PipeOut[1], STDOUT_FILENO);
	    dup2(PipeOut[1], STDERR_FILENO);
	    close(PipeOut[1]);
	}
	// close all file handles
	for (i = getdtablesize() - 1; i > STDERR_FILENO; i--) {
	    close(i);
	}

	// export DISPLAY=

	args[0] = ConfigMplayer;
	args[1] = "-quiet";
	args[2] = "-msglevel";
	// FIXME: play with the options
	args[3] = "all=2:global=2:cplayer=2:identify=4";
	args[4] = "-ontop";
	args[5] = "-noborder";
	args[6] = "-nolirc";
	args[7] = "-nojoystick";	// disable all unwanted inputs
	args[8] = "-noar";
	args[9] = "-nomouseinput";
	args[10] = "-nograbpointer";
	args[11] = "-noconsolecontrols";
	args[12] = "-fixed-vo";
	args[13] = "-nocache";		// dvdnav needs nocache
	argn = 14;
	if (ConfigUseSlave) {
	    args[argn++] = "-slave";
	    //args[argn++] = "-idle";
	}
	if (ConfigFullscreen) {
	    args[argn++] = "-fs";
	    args[argn++] = "-zoom";
	} else {
	    args[argn++] = "-nofs";
	}
	if (ConfigVideoOut) {
	    args[argn++] = "-vo";
	    args[argn++] = ConfigVideoOut;
	    // add options based on selected video out
	    if (!strncmp(ConfigVideoOut, "vdpau", 5)) {
		args[argn++] = "-vc";
		args[argn++] =
		    "ffmpeg12vdpau,ffwmv3vdpau,ffvc1vdpau,ffh264vdpau,ffodivxvdpau,";
	    } else if (!strncmp(ConfigVideoOut, "vaapi", 5)) {
		args[argn++] = "-va";
		args[argn++] = "vaapi";
	    }
	}
	if (ConfigAudioOut) {
	    args[argn++] = "-ao";
	    args[argn++] = ConfigAudioOut;
	    // FIXME: -ac hwac3,hwdts,hwmpa,
	}
	if (ConfigAudioMixer) {
	    args[argn++] = "-mixer";
	    args[argn++] = ConfigAudioMixer;
	}
	if (ConfigMixerChannel) {
	    args[argn++] = "-mixer-channel";
	    args[argn++] = ConfigMixerChannel;
	}
	if (ConfigX11Display) {
	    args[argn++] = "-display";
	    args[argn++] = ConfigX11Display;
	}
	if (PlayerVolume != -1) {
	    // FIXME: here could be a problem with LANG
	    snprintf(volume_buf, sizeof(volume_buf), "%.2f",
		(PlayerVolume * 100.0) / 255);
	    args[argn++] = "-volume";
	    args[argn++] = volume_buf;
	}
	args[argn++] = filename;
	args[argn] = NULL;

	execvp(args[0], (char *const *)args);
	printf("play: execvp of '%s' failed: %s\n", args[0], strerror(errno));
	exit(-1);
    }
    PlayerPid = pid;

    if (ConfigUseSlave) {
	close(PipeIn[0]);
	close(PipeOut[1]);
    }

    printf("play: child %d\n", pid);
}

/**
**	Close pipes.
*/
void ClosePipes(void)
{
    if (ConfigUseSlave) {
	if (PipeIn[0] != -1) {
	    close(PipeIn[0]);
	}
	if (PipeOut[1] != -1) {
	    close(PipeOut[1]);
	}
    }
}

/**
**	Player running?
*/
int IsPlayerRunning(void)
{
    pid_t wpid;
    int status;

    if (!PlayerPid) {			// no player
	return 0;
    }

    wpid = waitpid(PlayerPid, &status, WNOHANG);
    if (wpid <= 0) {
	return 1;
    }
    if (WIFEXITED(status)) {
	Debug(3, "play: player exited (%d)\n", WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
	Debug(3, "play: player killed (%d)\n", WTERMSIG(status));
    }
    PlayerPid = 0;
    return 0;
}

/**
**	Send command to player.
*/
static void SendCommand(const char *format, ...)
{

/*
 dvdnav up
 dvdnav down
 dvdnav left
 dvdnav right
 dvdnav menu
 dvdnav select
 dvdnav prev
 dvdnav mouse
 get_percent_pos
 get_time_length
 get_time_pos
 mute
 pause
 frame_step
 quit
 seek
 volume
*/
    va_list va;
    char buf[256];
    int n;

    if (!PlayerPid) {
	return;
    }
    if (PipeIn[1] == -1) {
	Error(tr("play: no pipe to send command available\n"));
	return;
    }
    va_start(va, format);
    n = vsnprintf(buf, sizeof(buf), format, va);
    va_end(va);

    Debug(3, "play: send '%.*s'\n", n - 1, buf);

    // FIXME: poll pipe if ready
    if (write(PipeIn[1], buf, n) != n) {
	fprintf(stderr, "play: write failed\n");
    }
}

/**
**	Send player quit.
*/
static void PlayerSendQuit(void)
{
    if (ConfigUseSlave) {
	SendCommand("quit\n");
    }
}

/**
**	Send player volume.
*/
static void PlayerSendVolume(void)
{
    if (ConfigUseSlave) {
	// FIXME: %.2f could have a problem with LANG
	SendCommand("volume %.2f 1\n", (PlayerVolume * 100.0) / 255);
    }
}

//////////////////////////////////////////////////////////////////////////////
//	cPlayer
//////////////////////////////////////////////////////////////////////////////

class cMyPlayer:public cPlayer
{
  private:
    char *FileName;			///< file to play
  public:
     cMyPlayer(const char *);		///< player constructor
     virtual ~ cMyPlayer();		///< player destructor
    void Activate(bool);		///< player attached/detached
};

/**
**	Player constructor.
**
**	@param filename	path and name of file to play
*/
cMyPlayer::cMyPlayer(const char *filename)
:cPlayer(pmExtern_THIS_SHOULD_BE_AVOIDED)
{
    Debug(3, "play/%s: '%s'\n", __FUNCTION__, filename);

    PipeIn[0] = -1;
    PipeOut[1] = -1;
    PlayerPid = 0;

    PlayerVolume = cDevice::CurrentVolume();
    Debug(3, "play: initial volume %d\n", PlayerVolume);

    FileName = strdup(filename);
}

/**
**	Player destructor.
*/
cMyPlayer::~cMyPlayer()
{
    int waittime;
    int timeout;

    printf("%s: end\n", __FUNCTION__);
    if (IsPlayerRunning()) {
	waittime = 0;
	timeout = 500;			// 0.5s

	kill(PlayerPid, SIGTERM);
	// wait for player finishing, with timeout
	while (IsPlayerRunning() && waittime++ < timeout) {
	    usleep(1 * 1000);
	}
	if (IsPlayerRunning()) {
	    waittime = 0;
	    timeout = 500;		// 0.5s

	    kill(PlayerPid, SIGKILL);
	    // wait for player finishing, with timeout
	    while (IsPlayerRunning() && waittime++ < timeout) {
		usleep(1 * 1000);
	    }
	    if (IsPlayerRunning()) {
		Error(tr("play: can't stop player\n"));
	    }
	}
    }
    PlayerPid = 0;

    if (ConfigOsdOverlay) {
	VideoExit();
    }
    ClosePipes();
    free(FileName);
}

/**
**	Player attached or detached.
*/
void cMyPlayer::Activate(bool on)
{
    printf("%s: '%s'\n", __FUNCTION__, FileName);

    if (on) {
	if (ConfigOsdOverlay) {
	    VideoInit();
	}
	ExecPlayer(FileName);
	return;
    }
    // FIXME: stop external player
}

//////////////////////////////////////////////////////////////////////////////
//	cStatus
//////////////////////////////////////////////////////////////////////////////

class cMyStatus:public cStatus
{
  public:
    cMyStatus(void);
  protected:
    virtual void SetVolume(int, bool);	///< volume changed

    //bool GetVolume(int &, bool &);
};

static int Volume;			///< current volume
cMyStatus *Status;			///< status monitor for volume

/**
**	Status constructor.
*/
cMyStatus::cMyStatus(void)
{
    Volume = 0;
}

/**
**	Called if volume is set.
*/
void cMyStatus::SetVolume(int volume, bool absolute)
{
    Debug(3, "play: volume %d %s\n", volume, absolute ? "abs" : "rel");
    if (absolute) {
	Volume = volume;
    } else {
	Volume += volume;
    }
    if (Volume != PlayerVolume) {
	PlayerVolume = Volume;
	PlayerSendVolume();
    }
}

/**
**	Get volume.
bool cMyStatus::GetVolume(int &volume, bool &mute)
{
}
*/

//////////////////////////////////////////////////////////////////////////////
//	cControl
//////////////////////////////////////////////////////////////////////////////

class cMyControl:public cControl
{
    cMyPlayer *Player;			///< our player
    cSkinDisplayReplay *Display;	///< our osd display
  public:
    cMyControl(const char *);
    virtual ~ cMyControl();

    virtual eOSState ProcessKey(eKeys);
    virtual void Show(void);
    virtual void Hide(void);
};

/**
**	Control constructor.
*/
cMyControl::cMyControl(const char *filename)
:cControl(Player = new cMyPlayer(filename))
{
    Display = NULL;
    Status = new cMyStatus;		// start monitoring volume
}

/**
**	Control destructor.
*/
cMyControl::~cMyControl()
{
    printf("%s\n", __FUNCTION__);

    delete Player;
    delete Display;
    delete Status;

    //Stop();
    cStatus::MsgReplaying(this, NULL, NULL, false);
}

void cMyControl::Hide(void)
{
    printf("%s:\n", __FUNCTION__);
}

void cMyControl::Show(void)
{
    printf("%s:\n", __FUNCTION__);
}

eOSState cMyControl::ProcessKey(eKeys key)
{
    eOSState state;

    printf("%s: %d\n", __FUNCTION__, key);
    if (!IsPlayerRunning()) {
	Hide();
	//Stop();
	return osEnd;
    }
    if (ConfigUseSlave) {
	PollPipe();
    }
    //state=cOsdMenu::ProcessKey(key);
    state = osContinue;
    switch (key) {
	case kUp:
	    if (DvdNav) {
	    } else {
	    }
	    break;

	case kBack:
	    PlayerSendQuit();
	    // FIXME: need to select old directory and index
	    cRemote::CallPlugin("play");
	    return osBack;
	case kStop:
	    Hide();
	    //Stop();
	    return osEnd;
	default:
	    break;
    }

    return state;
}

/**
**	Play a file.
*/
void PlayFile(const char *filename)
{
    printf("play: play file '%s'\n", filename);
    cControl::Launch(new cMyControl(filename));
}

//////////////////////////////////////////////////////////////////////////////
//	cMenuSetupPage
//////////////////////////////////////////////////////////////////////////////

/**
**	Play plugin menu setup page class.
*/
class cMyMenuSetupPage:public cMenuSetupPage
{
  protected:
    ///
    /// local copies of global setup variables:
    /// @{
    int HideMainMenuEntry;
    /// @}
    virtual void Store(void);
  public:
    cMyMenuSetupPage(void);
    virtual eOSState ProcessKey(eKeys);	// handle input
};

/**
**	Process key for setup menu.
*/
eOSState cMyMenuSetupPage::ProcessKey(eKeys key)
{
    eOSState state;

    state = cMenuSetupPage::ProcessKey(key);

    return state;
}

/**
**	Constructor setup menu.
**
**	Import global config variables into setup.
*/
cMyMenuSetupPage::cMyMenuSetupPage(void)
{
    HideMainMenuEntry = ConfigHideMainMenuEntry;

    Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &HideMainMenuEntry,
	    trVDR("no"), trVDR("yes")));
}

/**
**	Store setup.
*/
void cMyMenuSetupPage::Store(void)
{
    SetupStore("HideMainMenuEntry", ConfigHideMainMenuEntry =
	HideMainMenuEntry);
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdMenu
//////////////////////////////////////////////////////////////////////////////

static char ShowBrowser;		///< flag show browser
static const char *BrowserStartDir;	///< browser start directory
static const NameFilter *BrowserFilters;	////< browser name filters

extern "C" int ReadDirectory(const char *, int, const NameFilter *,
    void (*cb_add) (void *, const char *), void *);
extern "C" int IsDirectory(const char *);

/**
**	Table of supported video suffixes.
*/
static const NameFilter VideoFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
    FILTER(".ts"),
    FILTER(".avi"),
    FILTER(".iso"),
    FILTER(".m4v"),
    FILTER(".mkv"),
    FILTER(".mp4"),
    FILTER(".mpg"),
    FILTER(".vdr"),
    FILTER(".vob"),
    FILTER(".wmv"),
#undef FILTER
};

/**
**	Table of supported audio suffixes.
*/
static const NameFilter AudioFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
    FILTER(".flac"),
    FILTER(".mp3"),
    FILTER(".ogg"),
    FILTER(".wav"),
#undef FILTER
};

/**
**	Menu class.
*/
class cMyMenu:public cOsdMenu
{
  private:
    char *Path;				///< current path
    const NameFilter *Filter;		///< current filter
    void NewDir(const char *, const NameFilter *);
  public:
    cMyMenu(const char *, const char *, const NameFilter *);
    virtual ~ cMyMenu();
    virtual eOSState ProcessKey(eKeys);
};

/**
**	Add item to menu.  Called from C.
**
**	@param obj	cMyMenu object
**	@param text	menu text
*/
extern "C" void cMyMenu__Add(void *obj, const char *text)
{
    cMyMenu *menu;

    // fucking stupid C++ can't assign void* without warning:
    menu = (typeof(menu)) obj;
    menu->Add(new cOsdItem(strdup(text)));
}

/**
**	Create directory menu.
**
**	@param path	directory path file name
**	@param filter	name selection filter
*/
void cMyMenu::NewDir(const char *path, const NameFilter * filter)
{
    int n;

    // FIXME: should show the path somewhere

    Skins.Message(mtStatus, tr("Scanning directory..."));

    free(Path);
    n = strlen(path);
    if (path[n - 1] == '/') {		// force '/' terminated
	Path = strdup(path);
    } else {
	Path = (char *)malloc(n + 2);
	stpcpy(stpcpy(Path, path), "/");
    }

    Add(new cOsdItem(".."));
    ReadDirectory(Path, 1, filter, cMyMenu__Add, this);
    ReadDirectory(Path, 0, filter, cMyMenu__Add, this);
    // FIXME: handle errors!

    Display();				// display build menu

    Skins.Message(mtStatus, NULL);
}

/**
**	Menu constructor.
**
**	@param title	menu title
**	@param path	directory path file name
**	@param filter	name selection filter
*/
cMyMenu::cMyMenu(const char *title, const char *path,
    const NameFilter * filter)
:cOsdMenu(title)
{
    Path = NULL;
    Filter = filter;

    NewDir(path, filter);
}

/**
**	Menu destructor.
*/
cMyMenu::~cMyMenu()
{
    free(Path);
}

/**
**	Handle Menu key event.
**
**	@param key	key event
*/
eOSState cMyMenu::ProcessKey(eKeys key)
{
    eOSState state;
    const cOsdItem *item;
    int current;
    const char *text;
    char *filename;
    char *tmp;

    // call standard function
    state = cOsdMenu::ProcessKey(key);
    dsyslog("[play]%s: %x - %x\n", __FUNCTION__, state, key);

    switch (state) {
	case osUnknown:
	    switch (key) {
		case kOk:
		    current = Current();	// get current menu item index
		    item = Get(current);
		    text = item->Text();
		    if (!strcmp(text, "..")) {	// level up
		case kBack:
			char *last;

			// remove 1 level
			filename = strdup(Path);
			last = NULL;
			for (tmp = filename; *tmp; ++tmp) {
			    if (*tmp == '/' && tmp[1]) {
				last = tmp;
			    }
			}
			if (last) {
			    last[1] = '\0';
			}
			// FIXME: select item, where we go up
		    } else {		// level down
			filename =
			    (char *)malloc(strlen(Path) + strlen(text) + 1);
			// path is '/' terminated
			stpcpy(stpcpy(filename, Path), text);
			if (!IsDirectory(filename)) {
			    PlayFile(filename);
			    free(filename);
			    return osEnd;
			}
			if (!strcmp(text, "VIDEO_TS")) {
			    char *tmp;

			    tmp =
				(char *)malloc(strlen(filename) +
				sizeof("dvdnav:///"));
			    strcpy(stpcpy(tmp, "dvdnav:///"), filename);
			    free(filename);
			    PlayFile(tmp);
			    free(tmp);
			    return osEnd;
			}
		    }
		    Clear();
		    NewDir(filename, Filter);
		    free(filename);
		    // FIXME: if dir fails use keep old!
		    return osContinue;
		    break;
		default:
		    break;
	    }
	    break;
	case osBack:
	    ShowBrowser = 0;
	    break;
	default:
	    break;
    }
    return state;
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdMenu
//////////////////////////////////////////////////////////////////////////////

/**
**	Play plugin menu class.
*/
class cPlayMenu:public cOsdMenu
{
  private:
  public:
    cPlayMenu(const char *, int = 0, int = 0, int = 0, int = 0, int = 0);
    virtual ~ cPlayMenu();
    virtual eOSState ProcessKey(eKeys);
};

/**
**	Play menu constructor.
*/
cPlayMenu::cPlayMenu(const char *title, int c0, int c1, int c2, int c3, int c4)
:cOsdMenu(title, c0, c1, c2, c3, c4)
{
    SetHasHotkeys();

    Add(new cOsdItem(hk(tr("Play DVD")), osUser1));
    Add(new cOsdItem(hk(tr("Browse Video in VideoDir")), osUser2));
    Add(new cOsdItem(hk(tr("Browse Video in Filesystem")), osUser3));
    Add(new cOsdItem(hk(tr("Play CD")), osUser4));
    Add(new cOsdItem(hk(tr("Browse Audio in VideoDir")), osUser5));
    Add(new cOsdItem(hk(tr("Browse Audio in Filesystem")), osUser6));
}

/**
**	Play menu destructor.
*/
cPlayMenu::~cPlayMenu()
{
}

/**
**	Handle play plugin menu key event.
**
**	@param key	key event
*/
eOSState cPlayMenu::ProcessKey(eKeys key)
{
    eOSState state;

    dsyslog("[play]%s: %x\n", __FUNCTION__, key);

    // call standard function
    state = cOsdMenu::ProcessKey(key);

    switch (state) {
	case osUser1:
	    PlayFile("dvdnav://");
	    return osEnd;
	case osUser2:
	    ShowBrowser = 1;
	    BrowserStartDir = VideoDirectory;
	    BrowserFilters = VideoFilters;
	    return osPlugin;		// restart with OSD browser
	case osUser3:
	    ShowBrowser = 1;
	    BrowserStartDir = "/";
	    BrowserFilters = VideoFilters;
	    return osPlugin;		// restart with OSD browser

	case osUser4:
	    PlayFile("cdda://");
	    return osEnd;
	case osUser5:
	    ShowBrowser = 1;
	    BrowserStartDir = VideoDirectory;
	    BrowserFilters = AudioFilters;
	    return osPlugin;		// restart with OSD browser
	case osUser6:
	    ShowBrowser = 1;
	    BrowserStartDir = "/";
	    BrowserFilters = AudioFilters;
	    return osPlugin;		// restart with OSD browser
	default:
	    break;
    }
    return state;
}

//////////////////////////////////////////////////////////////////////////////
//	cPlugin
//////////////////////////////////////////////////////////////////////////////

class cMyPlugin:public cPlugin
{
  public:
    cMyPlugin(void);
    virtual ~ cMyPlugin(void);
    virtual const char *Version(void);
    virtual const char *Description(void);
    virtual const char *CommandLineHelp(void);
    virtual bool ProcessArgs(int, char *[]);
    //virtual bool Initialize(void);
    //virtual bool Start(void);
    //virtual void Stop(void);
    //virtual void Housekeeping(void);
    //virtual void MainThreadHook(void);
    virtual const char *MainMenuEntry(void);
    virtual cOsdObject *MainMenuAction(void);
    virtual cMenuSetupPage *SetupMenu(void);
    virtual bool SetupParse(const char *, const char *);
};

cMyPlugin::cMyPlugin(void)
{
    // Initialize any member variables here.
    // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
    // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
    //dsyslog("[play]%s:\n", __FUNCTION__);
}

cMyPlugin::~cMyPlugin(void)
{
    // Clean up after yourself!
    //dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Return plugin version number.
**
**	@returns version number as constant string.
*/
const char *cMyPlugin::Version(void)
{
    return VERSION;
}

const char *cMyPlugin::Description(void)
{
    return tr(DESCRIPTION);
}

/**
**	Return a string that describes all known command line options.
*/
const char *cMyPlugin::CommandLineHelp(void)
{
    return::CommandLineHelp();
}

/**
**	Process the command line arguments.
*/
bool cMyPlugin::ProcessArgs(int argc, char *argv[])
{
    return::ProcessArgs(argc, argv);
    return true;
}

#if 0

/**
**	Start any background activities the plugin shall perform.
*/
bool cMyPlugin::Initialize(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    // FIXME: can delay until needed?
    //Status = new cMyStatus;		// start monitoring
    // FIXME: destructs memory

    return true;
}

/**
**	 Start any background activities the plugin shall perform.
*/
bool cMyPlugin::Start(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    return true;
}

/**
**	Shutdown plugin.  Stop any background activities the plugin is
**	performing.
*/
void cMyPlugin::Stop(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Perform any cleanup or other regular tasks.
*/
void cMyPlugin::Housekeeping(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);
}

#endif

/**
**	Create main menu entry.
*/
const char *cMyPlugin::MainMenuEntry(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    return ConfigHideMainMenuEntry ? NULL : tr(MAINMENUENTRY);
}

/**
**	Perform the action when selected from the main VDR menu.
*/
cOsdObject *cMyPlugin::MainMenuAction(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    if (ShowBrowser) {
	return new cMyMenu("Browse", BrowserStartDir, BrowserFilters);
    }
    return new cPlayMenu("Play");
}

#if 0

/**
**	Called for every plugin once during every cycle of VDR's main program
**	loop.
*/
void cMyPlugin::MainThreadHook(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    //::MainThreadHook();
}

#endif

/**
**	Return our setup menu.
*/
cMenuSetupPage *cMyPlugin::SetupMenu(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    return new cMyMenuSetupPage;
}

/**
**	Parse setup parameters
**
**	@param name	paramter name (case sensetive)
**	@param value	value as string
**
**	@returns true if the parameter is supported.
*/
bool cMyPlugin::SetupParse(const char *name, const char *value)
{
    //dsyslog("[play]%s: '%s' = '%s'\n", __FUNCTION__, name, value);

    if (!strcasecmp(name, "HideMainMenuEntry")) {
	ConfigHideMainMenuEntry = atoi(value);
	return true;
    }

    return false;
}

VDRPLUGINCREATOR(cMyPlugin);		// Don't touch this!
