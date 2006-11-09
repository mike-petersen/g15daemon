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
    
    (c) 2006 Mike Lampard, Philip Lawatsch, and others
          
     $Revision$ -  $Date$ $Author$
              
    This daemon listens on localhost port 15550 for client connections,
    and arbitrates LCD display.  Allows for multiple simultaneous clients.
    Client screens can be cycled through by pressing the 'L1' key.
*/
#ifndef G15DAEMON
#define G15DAEMON

#ifndef BLACKnWHITE
#define BLACK 1
#define WHITE 0
#define BLACKnWHITE 
#endif

#define GKEY_OFFSET 167
#define MKEY_OFFSET 185
#define LKEY_OFFSET 189

#define G15KEY_DOWN 1
#define G15KEY_UP 0

#define LCD_WIDTH 160
#define LCD_HEIGHT 43

/* tcp server defines */
#define LISTEN_PORT 15550
#define LISTEN_ADDR "127.0.0.1"
/* any more than this number of simultaneous clients will be rejected. */
#define MAX_CLIENTS 10

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pwd.h>

#define CLIENT_CMD_GET_KEYSTATE 'k'
#define CLIENT_CMD_SWITCH_PRIORITIES 'p'
#define CLIENT_CMD_IS_FOREGROUND 'v'
#define CLIENT_CMD_IS_USER_SELECTED 'u'
#define CLIENT_CMD_BACKLIGHT 0x80
#define CLIENT_CMD_CONTRAST 0x40
#define CLIENT_CMD_MKEY_LIGHTS 0x20
/* if the following CMD is sent from a client, G15Daemon will not send any MR or G? keypresses via uinput, 
 * all M&G keys must be handled by the client.  If the client dies or exits, normal functions resume. */
#define CLIENT_CMD_KEY_HANDLER 0x10

/* plugin types - LCD plugins are provided with a lcd_t and keystates via EVENT when visible */
/* CORE plugins source and sink events, have no screen associated, and are not able to quit.
   by design they implement core functionality..CORE_LIBRARY implements graphic and other 
   functions for use by other plugins.
*/
#define G15_PLUGIN_NONE			0
#define G15_PLUGIN_LCD_CLIENT		1
#define G15_PLUGIN_CORE_KB_INPUT	2
#define G15_PLUGIN_CORE_OS_KB		3
#define G15_PLUGIN_LCD_SERVER		4


/* plugin RETURN values */
#define G15_PLUGIN_QUIT -1
#define G15_PLUGIN_OK	0

/* plugin EVENT types */
#define G15_EVENT_KEYPRESS		1
#define G15_EVENT_VISIBILITY_CHANGED	2
#define G15_EVENT_USER_FOREGROUND	3
#define G15_EVENT_MLED			4
#define G15_EVENT_BACKLIGHT		5
#define G15_EVENT_CONTRAST		6
#define G15_EVENT_REQ_PRIORITY		7
#define G15_EVENT_CYCLE_PRIORITY	8
#define G15_EVENT_EXITNOW		9
/* core event types */
#define G15_COREVENT_KEYPRESS_IN	10
#define G15_COREVENT_KEYPRESS_OUT	11


#define SCR_HIDDEN			0
#define SCR_VISIBLE			1

/* plugin global or local */
enum {
    G15_PLUGIN_NONSHARED = 0,
    G15_PLUGIN_SHARED
};

typedef struct lcd_s 		lcd_t;
typedef struct lcdlist_s 	lcdlist_t;
typedef struct lcdnode_s 	lcdnode_t;

typedef struct plugin_event_s 	plugin_event_t;
typedef struct plugin_info_s 	plugin_info_t;
typedef struct plugin_s 	plugin_t;

typedef struct plugin_info_s 
{
    /* type - see above for valid defines*/
    int type;
    /* short name of the plugin - used only for logging at the moment */
    char *name;
    /* run thread - will be called every update_msecs milliseconds*/
    int *(*plugin_run) (void *);
    unsigned int update_msecs;
    /* plugin process to be called on close or NULL if there isnt one*/
    void *(*plugin_exit) (void *);
    /* plugin process to be called on EVENT (such as keypress)*/
    int *(*event_handler) (void *);
    /* init func if there is one else NULL*/
    int *(*plugin_init) (void *);
} plugin_info_s;

typedef struct plugin_s 
{
    lcdlist_t *masterlist;
    unsigned int type;
    plugin_info_t *info;
    void *plugin_handle;
    void *args;
} plugin_s;

typedef struct lcd_s
{
    lcdlist_t *masterlist;
    int lcd_type;
    unsigned char buf[1048];
    int max_x;
    int max_y;
    int connection;
    long int ident;
    unsigned int backlight_state;
    unsigned int mkey_state;
    unsigned int contrast_state;
    unsigned int state_changed;
    /* set to 1 if user manually selected this screen 0 otherwise*/
    unsigned int usr_foreground;
    /* only used for plugins */
    plugin_t *g15plugin;
    
} lcd_s;


typedef struct plugin_event_s
{
    unsigned int event;
    unsigned long value;
    lcd_t *lcd;
} plugin_event_s;


struct lcdnode_s {
    lcdlist_t *list;
    lcdnode_t *prev;
    lcdnode_t *next;
    lcdnode_t *last_priority;
    lcd_t *lcd;
}lcdnode_s;

struct lcdlist_s
{
    lcdnode_t *head;
    lcdnode_t *tail;
    lcdnode_t *current;
    void *(*keyboard_handler)(void*);
    struct passwd *nobody;
    int numclients;
}lcdlist_s;

pthread_mutex_t lcdlist_mutex;
pthread_mutex_t g15lib_mutex;

/* server hello */
#define SERV_HELO "G15 daemon HELLO"

/* uinput & keyboard control */
#ifdef HAVE_LINUX_UINPUT_H
int g15_init_uinput();
void g15_uinput_keyup(unsigned char code);
void g15_uinput_keydown(unsigned char code);
void g15_exit_uinput();
#endif
    
void g15_process_keys(lcdlist_t *displaylist, unsigned int currentkeys, unsigned int lastkeys);

/* call create_lcd for every new client, and quit it when done */
lcd_t * create_lcd ();
void quit_lcd (lcd_t * lcd);
void write_buf_to_g15(lcd_t *lcd);

void setpixel (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int color);
//void cls (lcd_t * lcd, int color);
void line (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int color);
void rectangle (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, int filled, unsigned int color);
void draw_bignum (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int color, int num);

/* utility functions in utility_func.c */
void pthread_sleep(int seconds);
int pthread_msleep(int milliseconds);

/* linked lists */
lcdlist_t *lcdlist_init();
void lcdlist_destroy(lcdlist_t **displaylist);
lcdnode_t *lcdnode_add(lcdlist_t **display_list);
void lcdnode_remove(lcdnode_t *badnode);

/* create a listening socket */
int init_sockserver();
int g15_clientconnect(lcdlist_t **g15daemon,int listening_socket);
int g15_send(int sock, char *buf, unsigned int len);
int g15_recv(lcdnode_t *lcdnode, int sock, char *buf, unsigned int len);

/* handy function from xine_utils.c */
void *g15_xmalloc(size_t size);
/* internal lcd plugin - the clock/menu */
int internal_clock_eventhandler(plugin_event_t *myevent);
/* generic handler for net clients */
int internal_generic_eventhandler(plugin_event_t *myevent);
/* send event to foreground client's eventlistener */
int send_event(void *caller, unsigned int event, unsigned long value);
#endif
