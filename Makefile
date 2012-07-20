#
# Makefile for a Video Disk Recorder plugin
#
# $Id: 14fbc0969bcd384cea8b8b420b49d49a6c3ef381 $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
# IMPORTANT: the presence of this macro is important for the Make.config
# file. So it must be defined, even if it is not used here!
#
PLUGIN = play

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*const VERSION *=' $(PLUGIN).cpp | awk '{ print $$7 }' | sed -e 's/[";]//g')
GIT_REV = $(shell git describe --always 2>/dev/null)

### Configuration (edit this for your needs)

CONFIG := #-DDEBUG
CONFIG +=

### The C++ compiler and options:

CC	?= gcc
CXX	?= g++
CFLAGS	?= -g -O2 -W -Wall -Wextra -Winit-self \
	-Wdeclaration-after-statement
CXXFLAGS ?= -g -O2 -W -Wall -Wextra -Werror=overloaded-virtual

### The directory environment:

VDRDIR ?= ../../..
LIBDIR ?= ../../lib
TMPDIR ?= /tmp

### Make sure that necessary options are included:

-include $(VDRDIR)/Make.global

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The version number of VDR's plugin API (taken from VDR's "config.h"):

APIVERSION = $(shell sed -ne '/define APIVERSION/s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/config.h)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### Includes, Defines and dependencies (add further entries here):

INCLUDES += -I$(VDRDIR)/include

DEFINES += $(CONFIG) -D_GNU_SOURCE -DPLUGIN_NAME_I18N='"$(PLUGIN)"' \
	$(if $(GIT_REV), -DGIT_REV='"$(GIT_REV)"')

_CFLAGS = $(DEFINES) $(INCLUDES) \
	$(shell pkg-config --cflags xcb xcb-event xcb-keysyms xcb-icccm xcb-image) 

#override _CFLAGS  += -Werror
override CXXFLAGS += $(_CFLAGS)
override CFLAGS	  += $(_CFLAGS)

LIBS += \
	$(shell pkg-config --libs xcb xcb-keysyms xcb-event xcb-icccm xcb-image) 

### The object files (add further files here):

OBJS = $(PLUGIN).o video.o
SRCS = $(wildcard $(OBJS:.o=.c)) $(PLUGIN).cpp

### The main target:

all: libvdr-$(PLUGIN).so i18n

### Implicit rules:
#
#%.o: %.cpp
#	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Dependencies:

MAKEDEP = $(CC) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(SRCS) >$@

$(OBJS): Makefile

-include $(DEPFILE)

### Internationalization (I18N):

PODIR	  = po
LOCALEDIR = $(VDRDIR)/locale
I18Npo	  = $(wildcard $(PODIR)/*.po)
I18Nmsgs  = $(addprefix $(LOCALEDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot	  = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.cpp)  $(wildcard *.c)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP \
	-k_ -k_N --package-name=VDR --package-version=$(VDRVERSION) \
	--msgid-bugs-address='<see README>' -o $@ $^

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

$(I18Nmsgs): $(LOCALEDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	@mkdir -p $(dir $@)
	cp $< $@

.PHONY: i18n
i18n: $(I18Nmsgs) $(I18Npot)

### Targets:

libvdr-$(PLUGIN).so: $(OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -fPIC $(OBJS) -o $@ $(LIBS)
	@cp --remove-destination $@ $(LIBDIR)/$@.$(APIVERSION)

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~ $(PODIR)/*.mo $(PODIR)/*.pot

install:	libvdr-$(PLUGIN).so
	cp --remove-destination libvdr-$(PLUGIN).so \
		/usr/lib/vdr/plugins/libvdr-$(PLUGIN).so.$(APIVERSION)

HDRS=	$(wildcard *.h)

indent:
	for i in $(wildcard $(OBJS:.o=.c)) $(HDRS); do \
		indent $$i; unexpand -a $$i > $$i.up; mv $$i.up $$i; \
	done
