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
        
simple Clock plugin, replace the various functions with your own, and change the g15plugin_info struct below to suit,
   edit Makefile.am and compile.  Add salt and pepper to taste.  For a more advanced plugin that creates it's own lcd screens on-the-fly, 
   see the tcpserver plugin in this directory.
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
#include <libg15.h>
#include <config.h>
#include <g15daemon.h>
#include <libg15render.h>


static int mode=1;
static int showdate=0;

static int *lcdclock(lcd_t *lcd)
{
    unsigned int col = 0;
    unsigned int len=0;
    int narrows=0;
    int totalwidth=0;
    char buf[10];
    char ampm[3];
    int height = G15_LCD_HEIGHT - 1;
    g15canvas *canvas = (g15canvas *) malloc (sizeof (g15canvas));

    if (canvas != NULL)
      {
      	memset(canvas->buffer, 0, G15_BUFFER_LEN);
	canvas->mode_cache = 0;
	canvas->mode_reverse = 0;
	canvas->mode_xor = 0;
      }

    time_t currtime = time(NULL);
    
    memset(lcd->buf,0,G15_BUFFER_LEN);
    memset(buf,0,10);
    memset(ampm,0,3);
    if(showdate) {
        char buf2[40];
        strftime(buf2,40,"%A %e %B %Y",localtime(&currtime));
        g15r_renderString (canvas,buf2 , 0, G15_TEXT_MED, 80-((strlen(buf2)*5)/2), height-6);
        height-=10;
      }

    if(mode) {
   	strftime(buf,6,"%H:%M",localtime(&currtime));
    } else { 
        strftime(buf,6,"%l:%M",localtime(&currtime));
	strftime(ampm,3,"%p",localtime(&currtime));
    }
    if(buf[0]==49) 
    	narrows=1;
    
    len = strlen(buf); 
    if(buf[0]==' ')
     len++;
    
    if(narrows)
        totalwidth=(len*20)+(15);
    else
        totalwidth=len*20;

    for (col=0;col<len;col++) 
      g15r_drawBigNum (canvas, (80-(totalwidth)/2)+col*20, 1,(80-(totalwidth)/2)+(col+1)*20, height, buf[col]);
    
    if(ampm[0]!=0)
      g15r_renderString (canvas,ampm,0,G15_TEXT_LARGE,totalwidth,height-6);

    memcpy (lcd->buf, canvas->buffer, G15_BUFFER_LEN);
    lcd->ident = currtime+100;
    free(canvas);
    return G15_PLUGIN_OK;
}

static int myeventhandler(plugin_event_t *myevent) {
    
    lcd_t *lcd = (lcd_t*) myevent->lcd;
    config_section_t *clockcfg =NULL;
    switch (myevent->event)
    {
        case G15_EVENT_KEYPRESS:
            clockcfg = g15daemon_cfg_load_section(lcd->masterlist,"Clock");
            if(myevent->value & G15_KEY_L2){
                mode = 1^mode;
                g15daemon_cfg_write_bool(clockcfg, "24hrFormat", mode);
            }
            if(myevent->value & G15_KEY_L3) {
                showdate = 1^showdate;
                g15daemon_cfg_write_bool(clockcfg, "ShowDate", showdate);   
            }
//        printf("Clock plugin received keypress event : %i\n",myevent->value);
          break;
        case G15_EVENT_VISIBILITY_CHANGED:
//        printf("Clock received new visibility status (%i)\n",myevent->value);
          break;
        default:
          break;
    }
    return G15_PLUGIN_OK;
}

/* completely uncessary function called when plugin is exiting */
static void *callmewhenimdone(lcd_t *lcd){
    return NULL;
}

/* completely unnecessary initialisation function which could just as easily have been set to NULL in the g15plugin_info struct */
static void *myinithandler(lcd_t *lcd){
    config_section_t *clockcfg = g15daemon_cfg_load_section(lcd->masterlist,"Clock");
    mode=g15daemon_cfg_read_bool(clockcfg, "24hrFormat",1);
    showdate=g15daemon_cfg_read_bool(clockcfg, "ShowDate",0);
    return NULL;
}

/* if no exitfunc or eventhandler, member should be NULL */
plugin_info_t g15plugin_info[] = {
    /* TYPE, name, initfunc, updatefreq, exitfunc, eventhandler, initfunc */
    {G15_PLUGIN_LCD_CLIENT, "Clock", (void*)lcdclock, 500, (void*)callmewhenimdone, (void*)myeventhandler, (void*)myinithandler},
    {G15_PLUGIN_NONE,               ""          , NULL,     0,   NULL,            NULL,           NULL}
};
