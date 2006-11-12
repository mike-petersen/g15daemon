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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <libdaemon/daemon.h>
#include "g15daemon.h"
#include <libg15.h>

/* if no exitfunc or eventhandler, member should be NULL */
const plugin_info_t generic_info[] = {
    /* TYPE, name, initfunc, updatefreq, exitfunc, eventhandler */
    {G15_PLUGIN_LCD_CLIENT, "BackwardCompatible", NULL, 0, NULL, (void*)internal_generic_eventhandler},
    {G15_PLUGIN_NONE,        ""          , NULL,     0,   NULL, NULL}
};

/* handy function from xine_utils.c */
void *g15_xmalloc(size_t size) {
    void *ptr;

    /* prevent xmalloc(0) of possibly returning NULL */
    if( !size )
        size++;

    if((ptr = calloc(1, size)) == NULL) {
        daemon_log(LOG_WARNING, "g15_xmalloc() failed: %s.\n", strerror(errno));
        return NULL;
    }
    return ptr;
}

void convert_buf(lcd_t *lcd, unsigned char * orig_buf)
{
    unsigned int x,y;
    for(x=0;x<160;x++)
        for(y=0;y<43;y++)
            setpixel(lcd,x,y,orig_buf[x+(y*160)]);
}

/* wrap the libg15 function */
void write_buf_to_g15(lcd_t *lcd)
{
    pthread_mutex_lock(&g15lib_mutex);
    writePixmapToLCD(lcd->buf);
    pthread_mutex_unlock(&g15lib_mutex);
    return;
}

/* basic wbmp loader - wbmps should be inverted for use here */
int load_wbmp(lcd_t *lcd, char *filename)
{
    int wbmp_fd;
    int retval;
    unsigned int width, height, buflen,header=4;
    unsigned char tmpbuf[1024];
    int i;

    wbmp_fd=open(filename,O_RDONLY);
    if(!wbmp_fd){

        return -1;
    }
    retval=read(wbmp_fd,tmpbuf,865);
    close(wbmp_fd);
    if(retval<865){
        return -1;
    }
    if (tmpbuf[2] & 1) {
        width = ((unsigned char)tmpbuf[2] ^ 1) | (unsigned char)tmpbuf[3];
        height = tmpbuf[4];
        header = 5;
    } else {
        width = tmpbuf[2];
        height = tmpbuf[3];
        header = 4;
    }

    buflen = (width/8)*height;

    if(width!=160) {/* FIXME - we ought to scale images I suppose */
        return -1;
    }
    pthread_mutex_lock(&lcdlist_mutex);
    for(i=5;i<retval;i++){
        lcd->buf[i-5]=tmpbuf[i]^0xff;
    }
    pthread_mutex_unlock(&lcdlist_mutex);
}


/* Sleep routine (hackish). */
void pthread_sleep(int seconds) {
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
int pthread_msleep(int milliseconds) {
    
    struct timespec timeout;
    if(milliseconds>999)
        milliseconds=999;
    timeout.tv_sec = 0;
    timeout.tv_nsec = milliseconds*1000000;

    return nanosleep (&timeout, NULL);
}

unsigned int gettimerms(){
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

