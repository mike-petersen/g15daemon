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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "g15daemon.h"
#include <libg15.h>
#include <stdarg.h>

extern unsigned int g15daemon_debug;
#define G15DAEMON_PIDFILE "/var/run/g15daemon.pid"


/* if no exitfunc or eventhandler, member should be NULL */
const plugin_info_t generic_info[] = {
    /* TYPE, name, initfunc, updatefreq, exitfunc, eventhandler */
    {G15_PLUGIN_LCD_CLIENT, "BackwardCompatible", NULL, 0, NULL, (void*)internal_generic_eventhandler},
    {G15_PLUGIN_NONE,        ""          , NULL,     0,   NULL, NULL}
};

/* handy function from xine_utils.c */
void *g15daemon_xmalloc(size_t size) {
    void *ptr;

    /* prevent xmalloc(0) of possibly returning NULL */
    if( !size )
        size++;

    if((ptr = calloc(1, size)) == NULL) {
        g15daemon_log(LOG_WARNING, "g15_xmalloc() failed: %s.\n", strerror(errno));
        return NULL;
    }
    return ptr;
}

int uf_return_running(){
    int fd;
    char pidtxt[128];
    int pid;
    int l;
    
    if ((fd = open(G15DAEMON_PIDFILE, O_RDWR, 0644)) < 0) {
            return -1;
    }
    if((l = read(fd,pidtxt,sizeof(pidtxt)-1)) < 0) {
        unlink (G15DAEMON_PIDFILE);
        close(fd);
        return -1;
    }
    if((pid = atoi(pidtxt)) <= 0) {
        g15daemon_log(LOG_ERR,"pidfile corrupt");
        unlink(G15DAEMON_PIDFILE);
        close(fd);
        return -1;
    }
    if((kill(pid,0) != 0) && errno != EPERM ) {
        g15daemon_log(LOG_ERR,"Process died - removing pidfile");
        unlink(G15DAEMON_PIDFILE);
        close(fd);
        return -1;
    }
    
    return pid;
    
}


int uf_create_pidfile() {
    
    char pidtxt[128];
    size_t l;
    int fd;
    
    if(!uf_return_running() &&  (fd = open(G15DAEMON_PIDFILE, O_CREAT|O_RDWR|O_EXCL, 0644)) < 0) {
        g15daemon_log(LOG_ERR,"previous G15Daemon process died.  removing pidfile");
        unlink(G15DAEMON_PIDFILE);
    }
    if ((fd = open(G15DAEMON_PIDFILE, O_CREAT|O_RDWR|O_EXCL, 0644)) < 0) {
        return 1;
    }
    
    snprintf(pidtxt, sizeof(pidtxt), "%lu\n", (unsigned long) getpid());

    if (write(fd, pidtxt, l = strlen(pidtxt)) != l) {
        g15daemon_log(LOG_WARNING, "write(): %s", strerror(errno));
        unlink(G15DAEMON_PIDFILE);
    }
    
    if(fd>0) {
        close(fd);
        return 0;
    }
    return 1;
}

/* syslog wrapper */
int g15daemon_log (int priority, const char *fmt, ...) {

   va_list argp;
   va_start (argp, fmt);
   if(g15daemon_debug == 0)
     vsyslog(priority, fmt, argp);
   else {
     vfprintf(stderr,fmt,argp);
     fprintf(stderr,"\n");
   }
   va_end (argp);
   
   return 0;
}

void g15daemon_convert_buf(lcd_t *lcd, unsigned char * orig_buf)
{
    unsigned int x,y,val;
    for(x=0;x<160;x++)
        for(y=0;y<43;y++)
	  {
		unsigned int pixel_offset = y * LCD_WIDTH + x;
    		unsigned int byte_offset = pixel_offset / 8;
    		unsigned int bit_offset = 7-(pixel_offset % 8);

		val = orig_buf[x+(y*160)];

    		if (val)
        		lcd->buf[byte_offset] = lcd->buf[byte_offset] | 1 << bit_offset;
    		else
        		lcd->buf[byte_offset] = lcd->buf[byte_offset]  &  ~(1 << bit_offset);
	  }
}

/* wrap the libg15 function */
void uf_write_buf_to_g15(lcd_t *lcd)
{
    pthread_mutex_lock(&g15lib_mutex);
    writePixmapToLCD(lcd->buf);
    pthread_mutex_unlock(&g15lib_mutex);
    return;
}

/* Sleep routine (hackish). */
void g15daemon_sleep(int seconds) {
    pthread_mutex_t dummy_mutex;
    static pthread_cond_t dummy_cond = PTHREAD_COND_INITIALIZER;
    struct timespec timeout;

    /* Create a dummy mutex which doesn't unlock for sure while waiting. */
    pthread_mutex_init(&dummy_mutex, NULL);
    pthread_mutex_lock(&dummy_mutex);

    timeout.tv_sec = time(NULL) + seconds;
    timeout.tv_nsec = 0;

    pthread_cond_timedwait(&dummy_cond, &dummy_mutex, &timeout);

    /*    pthread_cond_destroy(&dummy_cond); */
    pthread_mutex_unlock(&dummy_mutex);
    pthread_mutex_destroy(&dummy_mutex);
}

/* millisecond sleep routine. */
int g15daemon_msleep(int milliseconds) {
    
    struct timespec timeout;
    if(milliseconds>999)
        milliseconds=999;
    timeout.tv_sec = 0;
    timeout.tv_nsec = milliseconds*1000000;

    return nanosleep (&timeout, NULL);
}

unsigned int g15daemon_gettime_ms(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec*1000+tv.tv_usec/1000);
}

/* generic event handler used unless overridden (only loading a plugin will override currently)*/
int internal_generic_eventhandler(plugin_event_t *event) {
    
    lcd_t *lcd = (lcd_t*) event->lcd;
    
    switch (event->event)
    {
        case G15_EVENT_KEYPRESS: {
            if(lcd->g15plugin->plugin_handle){ /* loadable plugin */
                 /* plugin had null for eventhandler therefore doesnt want events.. throw them away */
            }
        }
        break;
        case G15_EVENT_VISIBILITY_CHANGED:
          break;
        default:
          break;
    }
    return G15_PLUGIN_OK;
}

