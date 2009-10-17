/*
        This file is part of g15daemon.

        g15daemon is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2 of the License, or
        (at your option) any later version.

        g15daemon is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with g15daemon; if not, write to the Free Software
        Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

        (c) 2006-2009 Mike Lampard

        $Revision: 508 $ -  $Date: 2009-06-02 16:52:42 +0200 (Tue, 02 Jun 2009) $ $Author: steelside $

        This daemon listens on localhost port 15550 for client connections,
        and arbitrates LCD display.  Allows for multiple simultaneous clients.
        Client screens can be cycled through by pressing the 'L1' key.

        This is a macro recorder and playback utility for the G15 and g15daemon.
*/

#ifndef __G15MACRO_H__
#define __G15MACRO_H__
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/time.h>
#include <config.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#ifdef HAVE_X11_EXTENSIONS_XTEST_H
#include <X11/extensions/XTest.h>
#endif
#include <X11/XF86keysym.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>
#include "config.h"

#define  XK_MISCELLANY
#define XK_LATIN1
#define XK_LATIN2
#include <X11/keysymdef.h>

#define G15MACRO_CONF_VER 2

int g15screen_fd;
int config_fd;
g15canvas *canvas;

Display *dpy;
Window root_win;

pthread_mutex_t x11mutex;
pthread_mutex_t config_mutex;
pthread_mutex_t gui_select;

int have_xtest;
unsigned char recstring[1024];

typedef struct keypress_s {
	unsigned long keycode;
	unsigned long time_ms;
	unsigned char pressed;
	unsigned long modifiers;
	unsigned int mouse_x;
	unsigned int mouse_y;
	unsigned int buttons;
}keypress_t;

#define MAX_KEYSTEPS 1024
typedef struct keysequence_s {
	keypress_t recorded_keypress[MAX_KEYSTEPS];
	unsigned int record_steps;
} keysequence_t;

typedef struct gkeys_s{
	unsigned int recorded;
	keysequence_t keysequence;
	char* execFile;
}gkeys_t;

typedef struct mstates_s {
	gkeys_t gkeys[18];
}mstates_t;

mstates_t *mstates[3];


int mmedia_codes[6];
const long mmedia_defaults[6];
int gkeycodes[18];
const char *gkeystring[19];
const long gkeydefaults[54];


// This variable = G15 keyboard version -1
// So G15v1 == (G15Version = 0)
// G15v2 == (G15Version = 1)
int G15Version;

char configpath[1024];

char configDir[1024];
char GKeyCodeCfg[1024];

#define MAX_CONFIGS 32
unsigned int numConfigs;
unsigned int currConfig;
unsigned int gui_selectConfig;

//char *configs[MAX_CONFIGS]; // Max posible configs are 32. Too lazy to create a struct for storing dynamically.
typedef struct configs_s
{
	char *configfile;
	unsigned int confver;
}configs_t;
configs_t *configs[MAX_CONFIGS];

unsigned int gui_oldConfig; // To make sure it will be redrawn at first
unsigned char was_recording;

unsigned int mled_state;
int mkey_state;
int recording;


//g15macro.c
int calc_mkey_offset();
void change_keymap();
void emptyMstates(int purge);




//fileHandling.c
unsigned char* getConfigName(unsigned int id);
int writeKeyDefs(char *filename);
void getKeyDefs(char *filename);
void dump_config(FILE *configfile);
void save_macros(char *filename);

#endif //__G15MACRO_H__