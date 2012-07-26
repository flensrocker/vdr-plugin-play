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

extern "C"
{
#include "video.h"
}

//////////////////////////////////////////////////////////////////////////////

    /// vdr-plugin version number.
    /// Makefile extracts the version number for generating the file name
    /// for the distribution archive.
static const char *const VERSION = "0.0.11"
#ifdef GIT_REV
    "-GIT" GIT_REV
#endif
    ;

    /// vdr-plugin description.
static const char *const DESCRIPTION = trNOOP("A play plugin");

    /// vdr-plugin text of main menu entry
static const char *MAINMENUENTRY = trNOOP("Play");

//////////////////////////////////////////////////////////////////////////////

#define Debug(level, fmt...)	fprintf(stderr, fmt)
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
static uint32_t ConfigColorKey = 0x00020507;	///< color key

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
	    "  -g geometry\tx11 window geometry wxh+x+y\n"
	    "  -k colorkey\tvideo color key (default=0x020507, mplayer2=0x76B901)\n"
	    "  -m mplayer\tfilename of mplayer executable\n"
	    "  -o\t\tosd overlay experiments\n" "  -s\t\tmplayer slave mode\n"
	    "  -v video\tmplayer -vo (vdpau:deint=4:hqscaling=1) overwrites mplayer.conf\n";
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
	    switch (getopt(argc, argv, "-a:d:fg:k:m:osv:")) {
		case 'a':		// audio out
		    ConfigAudioOut = optarg;
		    continue;
		case 'd':		// display x11
		    ConfigX11Display = optarg;
		    continue;
		case 'f':		// fullscreen mode
		    ConfigFullscreen = 1;
		    continue;
		case 'g':		// geometry
		    VideoSetGeometry(optarg);
		    continue;
		case 'k':		// color key
		    ConfigColorKey = strtol(optarg, NULL, 0);
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
static char PipeBuf[1024];		///< pipe buffer
static int PipeCnt;			///< pipe buffer count
static int PipeIdx;			///< pipe buffer index
static int PipeOut[2];			///< player write pipe
static int PipeIn[2];			///< player read pipe
static char DvdNav;			///< dvdnav active
static int PlayerVolume = -1;		///< volume 0 - 100
static char PlayerPaused;		///< player paused
static char PlayerSpeed;		///< player playback speed

/*
static enum __player_state_ {
} PlayerState;				///< player state
*/

/**
**	Parse player output.
*/
void PlayerParseLine(const char *data, int size)
{
    Debug(3, "player: |%.*s|\n", size, data);

    // data is \0 terminated
    if (!strncasecmp(data, "DVDNAV_TITLE_IS_MENU", 20)) {
	DvdNav = 1;
    } else if (!strncasecmp(data, "DVDNAV_TITLE_IS_MOVIE", 21)) {
	DvdNav = 2;
    } else if (!strncasecmp(data, "ID_DVD_VOLUME_ID=", 17)) {
	Debug(3, "DVD_VOLUME = %s\n", data + 17);
    } else if (!strncasecmp(data, "ID_AID_", 7)) {
	char lang[64];
	int aid;

	if ( sscanf(data, "ID_AID_%d_LANG=%s", &aid, lang) == 2 ) {
	    Debug(3, "AID(%d) = %s\n", aid, lang);
	}
    } else if (!strncasecmp(data, "ID_SID_", 7)) {
	char lang[64];
	int sid;

	if ( sscanf(data, "ID_SID_%d_LANG=%s", &sid, lang) == 2 ) {
	    Debug(3, "SID(%d) = %s\n", sid, lang);
	}
    }
}

/**
**	Poll input pipe.
*/
void PollPipe(void)
{
    pollfd poll_fds[1];
    int n;
    int i;
    int l;

    // check if something to read
    poll_fds[0].fd = PipeOut[0];
    poll_fds[0].events = POLLIN;

    switch (poll(poll_fds, 1, 0)) {
	case 0:			// timeout
	    return;
	case -1:			// error
	    Error(tr("play/player: poll failed: %s\n"), strerror(errno));
	    return;
	default:			// ready
	    break;
    }

    // fill buffer
    if ((n = read(PipeOut[0], PipeBuf + PipeCnt,
	    sizeof(PipeBuf) - PipeCnt)) < 0) {
	Error(tr("play/player: read failed: %s\n"), strerror(errno));
	return;
    }

    PipeCnt += n;
    l = 0;
    for (i = PipeIdx; i < PipeCnt; ++i) {
	if (PipeBuf[i] == '\n') {
	    PipeBuf[i] = '\0';
	    PlayerParseLine(PipeBuf + PipeIdx, i - PipeIdx);
	    PipeIdx = i + 1;
	    l = PipeIdx;
	}
    }

    if (l) {				// remove consumed bytes
	if ( PipeCnt - l ) {
	    memmove(PipeBuf, PipeBuf + l, PipeCnt - l);
	}
	PipeCnt -= l;
	PipeIdx -= l;
    } else if (PipeCnt == sizeof(PipeBuf)) {
	// no '\n' in buffer use it as single line
	PipeBuf[sizeof(PipeBuf) - 1] = '\0';
	PlayerParseLine(PipeBuf + PipeIdx, PipeCnt);
	PipeIdx = 0;
	PipeCnt = 0;
    }
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
	char wid_buf[32];
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
#ifdef DEBUG
	args[3] = "all=6:global=4:cplayer=4:identify=4";
#else
	args[3] = "all=2:global=2:cplayer=2:identify=4";
#endif
	if (ConfigOsdOverlay) {
	    args[4] = "-noontop";
	} else {
	    args[4] = "-ontop";
	}
	args[5] = "-noborder";
	args[6] = "-nolirc";
	args[7] = "-nojoystick";	// disable all unwanted inputs
	args[8] = "-noar";
	args[9] = "-nomouseinput";
	args[10] = "-nograbpointer";
	args[11] = "-noconsolecontrols";
	args[12] = "-fixed-vo";
	argn = 13;
	// FIXME: dvd-device
	if (!strncasecmp(filename, "cdda://", 7)) {
	    args[argn++] = "-cache";	// cdrom needs cache
	    args[argn++] = "1000";
	} else {
	    args[argn++] = "-nocache";	// dvdnav needs nocache
	}
	if (ConfigUseSlave) {
	    args[argn++] = "-slave";
	    //args[argn++] = "-idle";
	}
	if (ConfigOsdOverlay) {		// no mplayer osd with overlay
	    args[argn++] = "-osdlevel";
	    args[argn++] = "0";
	}
	if (ConfigFullscreen) {
	    args[argn++] = "-fs";
	    args[argn++] = "-zoom";
	} else {
	    args[argn++] = "-nofs";
	}
	if (VideoGetPlayWindow()) {
	    snprintf(wid_buf, sizeof(wid_buf), "%d", VideoGetPlayWindow());
	    args[argn++] = "-wid";
	    args[argn++] = wid_buf;
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
**	Send player pause.
*/
static void PlayerSendPause(void)
{
    if (ConfigUseSlave) {
	SendCommand("pause\n");
    }
}

/**
**	Send player speed_set.
*/
static void PlayerSendSetSpeed(int speed)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep speed_set %d\n", speed);
    }
}

/**
**	Send player seek.
*/
static void PlayerSendSeek(int seconds)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep seek %+d 0\n", seconds);
    }
}

/**
**	Send player volume.
*/
static void PlayerSendVolume(void)
{
    if (ConfigUseSlave) {
	// FIXME: %.2f could have a problem with LANG
	SendCommand("pausing_keep volume %.2f 1\n",
	    (PlayerVolume * 100.0) / 255);
    }
}

//////////////////////////////////////////////////////////////////////////////
//	Osd
//////////////////////////////////////////////////////////////////////////////

/**
**	Open OSD.
*/
static void OsdOpen(void)
{
    Debug(3, "play: %s\n", __FUNCTION__);

    VideoWindowShow();
}

/**
**	Close OSD.
*/
static void OsdClose(void)
{
    Debug(3, "play: %s\n", __FUNCTION__);

    VideoWindowHide();
    VideoWindowClear();
}

/**
**	Get OSD size and aspect.
**
**	@param width[OUT]	width of OSD
**	@param height[OUT]	height of OSD
**	@param aspect[OUT]	aspect ratio (4/3, 16/9, ...) of OSD
*/
void GetOsdSize(int *width, int *height, double *aspect)
{
    VideoGetOsdSize(width, height);
    *aspect = 16.0 / 9.0 / (double)*width * (double)*height;
}

/**
**	Draw osd pixmap
*/
void OsdDrawARGB(int x, int y, int w, int h, const uint8_t * argb)
{
    Debug(3, "play: %s %d,%d %d,%d\n", __FUNCTION__, x, y, w, h);

    VideoDrawARGB(x, y, w, h, argb);
}

/// C plugin get osd size and ascpect
extern void GetOsdSize(int *, int *, double *);

/// C plugin close osd
extern void OsdClose(void);

/// C plugin draw osd pixmap
extern void OsdDrawARGB(int, int, int, int, const uint8_t *);

//////////////////////////////////////////////////////////////////////////////
//	C Callbacks
//////////////////////////////////////////////////////////////////////////////

/**
**	Device plugin remote class.
*/
class cMyRemote:public cRemote
{
  public:

    /**
    **	Soft device remote class constructor.
    **
    **	@param name	remote name
    */
    cMyRemote(const char *name):cRemote(name)
    {
    }

    /**
    **	Put keycode into vdr event queue.
    **
    **	@param code	key code
    **	@param repeat	flag key repeated
    **	@param release	flag key released
    */
    bool Put(const char *code, bool repeat = false, bool release = false) {
	return cRemote::Put(code, repeat, release);
    }
};

/**
**	Feed key press as remote input (called from C part).
**
**	@param keymap	target keymap "XKeymap" name
**	@param key	pressed/released key name
**	@param repeat	repeated key flag
**	@param release	released key flag
*/
extern "C" void FeedKeyPress(const char *keymap, const char *key, int repeat,
    int release)
{
    cRemote *remote;
    cMyRemote *csoft;

    if (!keymap || !key) {
	return;
    }
    // find remote
    for (remote = Remotes.First(); remote; remote = Remotes.Next(remote)) {
	if (!strcmp(remote->Name(), keymap)) {
	    break;
	}
    }
    // if remote not already exists, create it
    if (remote) {
	csoft = (cMyRemote *) remote;
    } else {
	dsyslog("[play]%s: remote '%s' not found\n", __FUNCTION__, keymap);
	csoft = new cMyRemote(keymap);
    }

    //dsyslog("[softhddev]%s %s, %s\n", __FUNCTION__, keymap, key);
    if (key[1]) {			// no single character
	csoft->Put(key, repeat, release);
    } else if (!csoft->Put(key, repeat, release)) {
	cRemote::Put(KBDKEY(key[0]));	// feed it for edit mode
    }
}

//////////////////////////////////////////////////////////////////////////////
//	cOsd
//////////////////////////////////////////////////////////////////////////////

/**
**	My device plugin OSD class.
*/
class cMyOsd:public cOsd
{
  public:
    static volatile char Dirty;		///< flag force redraw everything

     cMyOsd(int, int, uint);		///< constructor
     virtual ~ cMyOsd(void);		///< destructor
    virtual void Flush(void);		///< commits all data to the hardware
    virtual void SetActive(bool);	///< sets OSD to be the active one
};

volatile char cMyOsd::Dirty;		///< flag force redraw everything

/**
**	Sets this OSD to be the active one.
**
**	@param on	true on, false off
**
**	@note only needed as workaround for text2skin plugin with
**	undrawn areas.
*/
void cMyOsd::SetActive(bool on)
{
    Debug(3, "[play]%s: %d\n", __FUNCTION__, on);

    if (Active() == on) {
	return;				// already active, no action
    }
    cOsd::SetActive(on);
    if (on) {
	Dirty = 1;
	OsdOpen();
    } else {
	OsdClose();
    }
}

/**
**	Constructor OSD.
**
**	Initializes the OSD with the given coordinates.
**
**	@param left	x-coordinate of osd on display
**	@param top	y-coordinate of osd on display
**	@param level	level of the osd (smallest is shown)
*/
cMyOsd::cMyOsd(int left, int top, uint level)
:cOsd(left, top, level)
{
    /* FIXME: OsdWidth/OsdHeight not correct!
       Debug(3, "[play]%s: %dx%d+%d+%d, %d\n", __FUNCTION__, OsdWidth(),
       OsdHeight(), left, top, level);
     */

    SetActive(true);
}

/**
**	OSD Destructor.
**
**	Shuts down the OSD.
*/
cMyOsd::~cMyOsd(void)
{
    Debug(3, "[play]%s:\n", __FUNCTION__);
    SetActive(false);
    // done by SetActive: OsdClose();
}

/**
**	Actually commits all data to the OSD hardware.
*/
void cMyOsd::Flush(void)
{
    cPixmapMemory *pm;

    if (!Active()) {
	return;
    }

    if (!IsTrueColor()) {		// work bitmap
	cBitmap *bitmap;
	int i;

	// draw all bitmaps
	for (i = 0; (bitmap = GetBitmap(i)); ++i) {
	    uint8_t *argb;
	    int x;
	    int y;
	    int w;
	    int h;
	    int x1;
	    int y1;
	    int x2;
	    int y2;

	    // get dirty bounding box
	    if (Dirty) {		// forced complete update
		x1 = 0;
		y1 = 0;
		x2 = bitmap->Width() - 1;
		y2 = bitmap->Height() - 1;
	    } else if (!bitmap->Dirty(x1, y1, x2, y2)) {
		continue;		// nothing dirty continue
	    }
	    // convert and upload only dirty areas
	    w = x2 - x1 + 1;
	    h = y2 - y1 + 1;
	    if (1) {			// just for the case it makes trouble
		int width;
		int height;
		double video_aspect;

		::GetOsdSize(&width, &height, &video_aspect);
		if (w > width) {
		    w = width;
		    x2 = x1 + width - 1;
		}
		if (h > height) {
		    h = height;
		    y2 = y1 + height - 1;
		}
	    }
#ifdef DEBUG
	    if (w > bitmap->Width() || h > bitmap->Height()) {
		esyslog(tr("[play]: dirty area too big\n"));
		abort();
	    }
#endif
	    argb = (uint8_t *) malloc(w * h * sizeof(uint32_t));
	    for (y = y1; y <= y2; ++y) {
		for (x = x1; x <= x2; ++x) {
		    ((uint32_t *) argb)[x - x1 + (y - y1) * w] =
			bitmap->GetColor(x, y);
		}
	    }
	    OsdDrawARGB(Left() + bitmap->X0() + x1, Top() + bitmap->Y0() + y1,
		w, h, argb);

	    bitmap->Clean();
	    // FIXME: reuse argb
	    free(argb);
	}
	cMyOsd::Dirty = 0;
	return;
    }

    LOCK_PIXMAPS;
    while ((pm = RenderPixmaps())) {
	int x;
	int y;
	int w;
	int h;

	x = Left() + pm->ViewPort().X();
	y = Top() + pm->ViewPort().Y();
	w = pm->ViewPort().Width();
	h = pm->ViewPort().Height();

	/*
	   Debug(3, "[play]%s: draw %dx%d+%d+%d %p\n", __FUNCTION__, w, h,
	   x, y, pm->Data());
	 */

	OsdDrawARGB(x, y, w, h, pm->Data());

	delete pm;
    }
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdProvider
//////////////////////////////////////////////////////////////////////////////

/**
**	My device plugin OSD provider class.
*/
class cMyOsdProvider:public cOsdProvider
{
  private:
    static cOsd *Osd;
  public:
    virtual cOsd * CreateOsd(int, int, uint);
    virtual bool ProvidesTrueColor(void);
    cMyOsdProvider(void);
};

cOsd *cMyOsdProvider::Osd;		///< single osd

/**
**	Create a new OSD.
**
**	@param left	x-coordinate of OSD
**	@param top	y-coordinate of OSD
**	@param level	layer level of OSD
*/
cOsd *cMyOsdProvider::CreateOsd(int left, int top, uint level)
{
    Debug(3, "[play]%s: %d, %d, %d\n", __FUNCTION__, left, top, level);

    return Osd = new cMyOsd(left, top, level);
}

/**
**	Check if this OSD provider is able to handle a true color OSD.
**
**	@returns true we are able to handle a true color OSD.
*/
bool cMyOsdProvider::ProvidesTrueColor(void)
{
    return true;
}

/**
**	Create cOsdProvider class.
*/
cMyOsdProvider::cMyOsdProvider(void)
:  cOsdProvider()
{
    Debug(3, "[play]%s:\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////////
//	cPlayer
//////////////////////////////////////////////////////////////////////////////

extern void EnableDummyDevice(void);
extern void DisableDummyDevice(void);

class cMyPlayer:public cPlayer, cThread
{
  private:
    char *FileName;			///< file to play
  public:
     cMyPlayer(const char *);		///< player constructor
     virtual ~ cMyPlayer();		///< player destructor
    void Activate(bool);		///< player attached/detached
    virtual bool GetReplayMode(bool &, bool &, int &);	///< get current replay mode
    // thread
    virtual void Action(void);		///< thread worker
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

    PipeCnt = 0;
    PipeIdx = 0;

    PipeIn[0] = -1;
    PipeIn[1] = -1;
    PipeOut[0] = -1;
    PipeOut[1] = -1;
    PlayerPid = 0;

    PlayerPaused = 0;
    PlayerSpeed = 1;

    DvdNav = 0;

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

    Debug(3, "%s: end\n", __FUNCTION__);

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

    Cancel(2);

    if (ConfigOsdOverlay) {
	DisableDummyDevice();
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
    Debug(3, "[play]%s: '%s'\n", __FUNCTION__, FileName);

    if (on) {
	if (ConfigOsdOverlay) {
	    VideoSetColorKey(ConfigColorKey);
	    VideoInit(ConfigX11Display);
	    EnableDummyDevice();
	}
	ExecPlayer(FileName);
	Start();
	return;
    }
    // FIXME: stop external player
}

/**
**	Get current replay mode.
*/
bool cMyPlayer::GetReplayMode(bool & play, bool & forward, int &speed)
{
    play = !PlayerPaused;
    forward = true;
    speed = play ? PlayerSpeed : -1;
    return true;
}

//////////////////////////////////////////////////////////////////////////////

/**
**	Thread action.
*/
void cMyPlayer::Action(void)
{
    Debug(3, "play: player thread started\n");
    while (Running()) {
	if (ConfigUseSlave) {
	    PollPipe();
	    // FIXME: wait only if pipe not ready
	}
	if (ConfigOsdOverlay) {
	    VideoPollEvents(10);
	} else {
	    usleep(10 * 1000);
	}
    }
    Debug(3, "play: player thread stopped\n");
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
  private:
    cMyPlayer * Player;			///< our player
    cSkinDisplayReplay *Display;	///< our osd display
    void ShowReplayMode(void);		///< display replay mode
    void ShowProgress(void);		///< display progress bar
    virtual void Show(void);		///< show replay control
    virtual void Hide(void);		///< hide replay control
  public:
    cMyControl(const char *);
    virtual ~ cMyControl();

    virtual eOSState ProcessKey(eKeys);

};

/**
**	Show replay mode.
*/
void cMyControl::ShowReplayMode(void)
{
    // use vdr setup
    if (Display || (Setup.ShowReplayMode && !cOsd::IsOpen())) {
	bool play;
	bool forward;
	int speed;

	if (GetReplayMode(play, forward, speed)) {
	    if (!Display) {
		// no need to show normal play
		if (play && forward && speed == 1) {
		    return;
		}
		Display = Skins.Current()->DisplayReplay(true);
	    }
	    Display->SetMode(play, forward, speed);
	}
    }
}

/**
**	Show progress.
*/
void cMyControl::ShowProgress(void)
{
}

/**
**	Show control.
*/
void cMyControl::Show(void)
{
    Debug(3, "%s:\n", __FUNCTION__);
    if (!Display) {
	ShowProgress();
    }
}

/**
**	Control constructor.
**
**	@param filename	pathname of file to play.
*/
cMyControl::cMyControl(const char *filename)
:  cControl(Player = new cMyPlayer(filename))
{
    Display = NULL;
    Status = new cMyStatus;		// start monitoring volume

    //LastSkipKey = kNone;
    //LastSkipSeconds = REPLAYCONTROLSKIPSECONDS;
    //LastSkipTimeout.Set(0);
    cStatus::MsgReplaying(this, filename, filename, true);

    cDevice::PrimaryDevice()->ClrAvailableTracks(true);
}

/**
**	Control destructor.
*/
cMyControl::~cMyControl()
{
    Debug(3, "%s\n", __FUNCTION__);

    delete Player;
    delete Display;
    delete Status;

    Hide();
    cStatus::MsgReplaying(this, NULL, NULL, false);
    //Stop();
}

/**
**	Hide control.
*/
void cMyControl::Hide(void)
{
    Debug(3, "%s:\n", __FUNCTION__);

    if (Display) {
	delete Display;

	Display = NULL;
	SetNeedsFastResponse(false);
    }
}

eOSState cMyControl::ProcessKey(eKeys key)
{
    eOSState state;

    Debug(4, "%s: %d\n", __FUNCTION__, key);
    if (!IsPlayerRunning()) {
	Hide();
	//Stop();
	return osEnd;
    }
    //state=cOsdMenu::ProcessKey(key);
    state = osContinue;
    switch ((int)key) {			// cast to shutup g++ warnings
	case kUp:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav up\n");
		break;
	    }
	case kPlay:
	    Hide();
	    if (PlayerSpeed != 1) {
		PlayerSendSetSpeed(PlayerSpeed = 1);
	    }
	    if (PlayerPaused) {
		PlayerSendPause();
		PlayerPaused ^= 1;
	    }
	    ShowReplayMode();
	    break;

	case kDown:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav down\n");
		break;
	    }
	case kPause:
	    PlayerSendPause();
	    PlayerPaused ^= 1;
	    ShowReplayMode();
	    break;

	case kFastRew|k_Release:
	case kLeft|k_Release:
	    if (Setup.MultiSpeedMode) {
		break;
	    }
	    // FIXME:
	    break;
	case kLeft:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav left\n");
		break;
	    }
	case kFastRew:
	    if (PlayerSpeed > 1) {
		PlayerSendSetSpeed(PlayerSpeed /= 2);
	    } else {
		PlayerSendSeek(-10);
	    }
	    ShowReplayMode();
	    break;
	case kRight:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav right\n");
		break;
	    }
	case kFastFwd:
	    if (PlayerSpeed < 32) {
		PlayerSendSetSpeed(PlayerSpeed *= 2);
	    }
	    ShowReplayMode();
	    break;

	case kRed:
	    // FIXME: TimeSearch();
	    break;

#ifdef USE_JUMPINGSECONDS
	case kGreen|k_Repeat:
	    PlayerSendSeek(-Setup.JumpSecondsRepeat);
	    break;
	case kGreen:
	    PlayerSendSeek(-Setup.JumpSeconds);
	    break;
	case k1|k_Repeat:
	case k1:
	    PlayerSendSeek(-Setup.JumpSecondsSlow);
	    break;
	case k3|k_Repeat:
	case k3:
	    PlayerSendSeek( Setup.JumpSecondsSlow);
	    break;
	case kYellow|k_Repeat:
	    PlayerSendSeek(Setup.JumpSecondsRepeat);
	    break;
	case kYellow:
	    PlayerSendSeek(Setup.JumpSeconds);
	    break;
#else
	case kGreen|k_Repeat:
	case kGreen:
	    PlayerSendSeek(-60); 
	    break;
	case kYellow|k_Repeat:
	case kYellow:
	    PlayerSendSeek(+60);
	    break;
#endif /* JUMPINGSECONDS */
#ifdef USE_LIEMIKUUTIO
#ifndef USE_JUMPINGSECONDS
	case k1|k_Repeat:
	case k1:
	    PlayerSendSeek(-20);
	    break;
	case k3|k_Repeat:
	case k3:
	    PlayerSendSeek(+20);
	    break;
#endif /* JUMPINGSECONDS */
#endif

	case kStop:
	case kBlue:
	    Hide();
	    // FIXME: Stop();
	    return osEnd;

	case kOk:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav select\n");
		// FIXME: DvdNav = 0;
		break;
	    }
	    // FIXME: full mode
	    ShowReplayMode();
	    break;

	case kBack:
	    if (DvdNav > 1) {
		SendCommand("pausing_keep dvdnav prev\n");
		break;
	    }
	    PlayerSendQuit();
	    // FIXME: need to select old directory and index
	    cRemote::CallPlugin("play");
	    return osBack;

	case kMenu:
	    if (DvdNav) {
		SendCommand("pausing_keep dvdnav menu\n");
		break;
	    }
	    break;

	/* VDR: eats the keys
	case kAudio:
	    // FIXME: audio menu
	    SendCommand("pausing_keep switch_audio\n");
	    break;
	case kSubtitles:
	    // FIXME: subtitle menu
	    SendCommand("pausing_keep sub_select\n");
	    break;
	*/

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
    Debug(3, "play: play file '%s'\n", filename);
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
    FILTER(".flv"),
    FILTER(".iso"),
    FILTER(".m4v"),
    FILTER(".mkv"),
    FILTER(".mov"),
    FILTER(".mp4"),
    FILTER(".mpg"),
    FILTER(".vdr"),
    FILTER(".vob"),
    FILTER(".wmv"),
#undef FILTER
    {0, NULL}
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
    {0, NULL}
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
    Debug(3, "[play]%s: %x - %x\n", __FUNCTION__, state, key);

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
			// handle DVD image
			if (!strcmp(text, "AUDIO_TS")
			    || !strcmp(text, "VIDEO_TS")) {
			    char *tmp;

			    free(filename);
			    tmp = (char *)malloc(sizeof("dvdnav:///")
				+ strlen(Path));
			    strcpy(stpcpy(tmp, "dvdnav:///"), Path);
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

    Debug(3, "[play]%s: %x\n", __FUNCTION__, key);

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
//	cDevice
//////////////////////////////////////////////////////////////////////////////

class cMyDevice:public cDevice
{
  public:
    cMyDevice(void);
    virtual ~ cMyDevice(void);

    virtual void GetOsdSize(int &, int &, double &);
  protected:
    virtual void MakePrimaryDevice(bool);
};

/**
**	Device constructor.
*/
cMyDevice::cMyDevice(void)
{
    Debug(3, "[play]%s\n", __FUNCTION__);
}

/**
**	Device destructor. (never called!)
*/
cMyDevice::~cMyDevice(void)
{
    Debug(3, "[play]%s:\n", __FUNCTION__);
}

/**
**	Informs a device that it will be the primary device.
**
**	@param on	flag if becoming or loosing primary
*/
void cMyDevice::MakePrimaryDevice(bool on)
{
    Debug(3, "[play]%s: %d\n", __FUNCTION__, on);

    cDevice::MakePrimaryDevice(on);
    if (on) {
	new cMyOsdProvider();
    }
}

/**
**	Returns the width, height and pixel_aspect ratio the OSD.
**
**	FIXME: Called every second, for nothing (no OSD displayed)?
*/
void cMyDevice::GetOsdSize(int &width, int &height, double &pixel_aspect)
{
    if (!&width || !&height || !&pixel_aspect) {
	esyslog(tr("[softhddev]: GetOsdSize invalid pointer(s)\n"));
	return;
    }
    VideoGetOsdSize(&width, &height);
    pixel_aspect = 16.0 / 9.0 / (double)width *(double)height;
}

//////////////////////////////////////////////////////////////////////////////
//	cPlugin
//////////////////////////////////////////////////////////////////////////////

static cMyDevice *MyDevice;		///< dummy device needed for osd
static volatile int DoMakePrimary;	///< switch primary device to this

class cMyPlugin:public cPlugin
{
  public:
    cMyPlugin(void);
    virtual ~ cMyPlugin(void);
    virtual const char *Version(void);
    virtual const char *Description(void);
    virtual const char *CommandLineHelp(void);
    virtual bool ProcessArgs(int, char *[]);
    virtual bool Initialize(void);
    //virtual bool Start(void);
    //virtual void Stop(void);
    //virtual void Housekeeping(void);
    virtual void MainThreadHook(void);
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
    //Debug(3, "[play]%s:\n", __FUNCTION__);
}

cMyPlugin::~cMyPlugin(void)
{
    // Clean up after yourself!
    //Debug(3, "[play]%s:\n", __FUNCTION__);
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

/**
**	Start any background activities the plugin shall perform.
*/
bool cMyPlugin::Initialize(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);

    // FIXME: can delay until needed?
    //Status = new cMyStatus;		// start monitoring
    // FIXME: destructs memory

    MyDevice = new cMyDevice;
    return true;
}

#if 0

/**
**	 Start any background activities the plugin shall perform.
*/
bool cMyPlugin::Start(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);

    return true;
}

/**
**	Shutdown plugin.  Stop any background activities the plugin is
**	performing.
*/
void cMyPlugin::Stop(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);
}

/**
**	Perform any cleanup or other regular tasks.
*/
void cMyPlugin::Housekeeping(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);
}

#endif

/**
**	Create main menu entry.
*/
const char *cMyPlugin::MainMenuEntry(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);

    return ConfigHideMainMenuEntry ? NULL : tr(MAINMENUENTRY);
}

/**
**	Perform the action when selected from the main VDR menu.
*/
cOsdObject *cMyPlugin::MainMenuAction(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);

    if (ShowBrowser) {
	return new cMyMenu("Browse", BrowserStartDir, BrowserFilters);
    }
    return new cPlayMenu("Play");
}

/**
**	Called for every plugin once during every cycle of VDR's main program
**	loop.
*/
void cMyPlugin::MainThreadHook(void)
{
    // Debug(3, "[play]%s:\n", __FUNCTION__);

    if (DoMakePrimary) {
	Debug(3, "[play]: switching primary device to %d\n", DoMakePrimary);
	cDevice::SetPrimaryDevice(DoMakePrimary);
	DoMakePrimary = 0;
    }
}

/**
**	Return our setup menu.
*/
cMenuSetupPage *cMyPlugin::SetupMenu(void)
{
    //Debug(3, "[play]%s:\n", __FUNCTION__);

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
    //Debug(3, "[play]%s: '%s' = '%s'\n", __FUNCTION__, name, value);

    if (!strcasecmp(name, "HideMainMenuEntry")) {
	ConfigHideMainMenuEntry = atoi(value);
	return true;
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////

int OldPrimaryDevice;			///< old primary device

/**
**	Enable dummy device.
*/
void EnableDummyDevice(void)
{
    OldPrimaryDevice = cDevice::PrimaryDevice()->DeviceNumber() + 1;
    DoMakePrimary = MyDevice->DeviceNumber() + 1;
}

/**
**	Disable dummy device.
*/
void DisableDummyDevice(void)
{
    DoMakePrimary = OldPrimaryDevice;
    OldPrimaryDevice = 0;
}

VDRPLUGINCREATOR(cMyPlugin);		// Don't touch this!
