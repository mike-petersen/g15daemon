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
#define G15DAEMON_BUILD 1

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

#include <config.h>
#include <libg15.h>
#include "g15daemon.h"

#ifndef LIBG15_VERSION
#define LIBG15_VERSION 1000
#endif

/* all threads will exit if leaving >0 */
int leaving = 0;
unsigned int g15daemon_debug = 0;
unsigned int cycle_key;
unsigned int client_handles_keys = 0;
struct lcd_t *keyhandler = NULL;

/* send event to foreground client's eventlistener */
int g15daemon_send_event(void *caller, unsigned int event, unsigned long value)
{
    switch(event) {
        case G15_EVENT_KEYPRESS: {
            static unsigned long lastkeys;
            if(!(value & cycle_key) && !(lastkeys & cycle_key)){
                lcd_t *lcd = (lcd_t*)caller;
                int *(*plugin_listener)(plugin_event_t *newevent) = (void*)lcd->g15plugin->info->event_handler;
                plugin_event_t *newevent=malloc(sizeof(plugin_event_t));
                newevent->event = event;
                newevent->value = value;
                newevent->lcd = lcd;
                (*plugin_listener)((void*)newevent);
        	/* hack - keyboard events are always sent from the foreground even when they aren't 
                send keypress event to the OS keyboard_handler plugin */
                if(lcd->masterlist->keyboard_handler != NULL) {
                    int *(*keyboard_handler)(plugin_event_t *newevent) = (void*)lcd->masterlist->keyboard_handler;
                    (*keyboard_handler)((void*)newevent);
                }
                free(newevent);
            }else{
                /* hacky attempt to double-time the use of L1, if the key is pressed less than half a second, it cycles the screens.  If held for longer, the key is sent to the application for use instead */
                lcd_t *lcd = (lcd_t*)caller;
                lcdlist_t* displaylist = lcd->masterlist;
                static unsigned int clicktime;
                if(value & cycle_key) {
                    clicktime=g15daemon_gettime_ms();
                }else{
                    unsigned int unclick=g15daemon_gettime_ms();
                    if ((unclick-clicktime)<500) {
                        g15daemon_lcdnode_cycle(displaylist);
                    }
                    else 
                    {
                        plugin_event_t *clickevent=malloc(sizeof(plugin_event_t));
                        int *(*plugin_listener)(plugin_event_t *clickevent) = (void*)lcd->g15plugin->info->event_handler;
                        clickevent->event = event;
                	clickevent->value = value|cycle_key;
                	clickevent->lcd = lcd;
                        (*plugin_listener)((void*)clickevent);
                        clickevent->event = event;
                	clickevent->value = value&~cycle_key;
                	clickevent->lcd = lcd;
                        (*plugin_listener)((void*)clickevent);
                    }
                }
            }
            lastkeys = value;
            break;
        }
        case G15_EVENT_CYCLE_PRIORITY:{
            lcd_t *lcd = (lcd_t*)caller;
            lcdlist_t* displaylist = lcd->masterlist;
            if(value)
                g15daemon_lcdnode_cycle(displaylist);
            break;
        }
        case G15_EVENT_REQ_PRIORITY: {
            lcdnode_t *lcdnode=(lcdnode_t*)caller;
            /* client wants to switch priorities */
            pthread_mutex_lock(&lcdlist_mutex);
            if(lcdnode->list->current != lcdnode){
                lcdnode->last_priority = lcdnode->list->current;
                lcdnode->list->current = lcdnode;
            }
            else {
                if(lcdnode->list->current == lcdnode->last_priority){
                    lcdnode->list->current = lcdnode->list->current->prev;
                } else{
                    if(lcdnode->last_priority != NULL) {
                        lcdnode->list->current = lcdnode->last_priority;
                        lcdnode->last_priority = NULL;
                    }
                    else
                        lcdnode->list->current = lcdnode->list->current->prev;
                }
            }
            pthread_mutex_unlock(&lcdlist_mutex);
            break;
        }
        default: {
            lcd_t *lcd = (lcd_t*)caller;
            int *(*plugin_listener)(plugin_event_t *newevent) = (void*)lcd->g15plugin->info->event_handler;
            plugin_event_t *newevent=malloc(sizeof(plugin_event_t));
            newevent->event = event;
            newevent->value = value;
            newevent->lcd = lcd;
            (*plugin_listener)((void*)newevent);
        }
    }
}

static void *keyboard_watch_thread(void *lcdlist){
    
    lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
    
    unsigned int keypresses = 0;
    int retval = 0;
    
    while (!leaving) {
        
        pthread_mutex_lock(&g15lib_mutex);
        retval = getPressedKeys(&keypresses, 40);
        pthread_mutex_unlock(&g15lib_mutex);
        
        if(retval == G15_NO_ERROR){
            g15daemon_send_event(displaylist->current->lcd, 
                       G15_EVENT_KEYPRESS, keypresses);
        }
        g15daemon_msleep(10);
    }
    
    return NULL;
}

static void *lcd_draw_thread(void *lcdlist){

    lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
    static long int lastlcd = 1;
    
    lcd_t *displaying = displaylist->tail->lcd;
    memset(displaying->buf,0,1024);
    
    g15daemon_sleep(2);

    while (!leaving) {
        pthread_mutex_lock(&lcdlist_mutex);
        
        displaying = displaylist->current->lcd;
        
        if(displaying->ident != lastlcd){
           uf_write_buf_to_g15(displaying);
           lastlcd = displaying->ident;
        }
        
        if(displaying->state_changed ){
            setLCDContrast(displaying->contrast_state);
            setLEDs(displaying->mkey_state);
            setLCDBrightness(displaying->backlight_state);
            displaying->state_changed = 0;
        }
            
        pthread_mutex_unlock(&lcdlist_mutex);
        
        g15daemon_msleep(5);
    }
    return NULL;
}

void g15daemon_sighandler(int sig) {
    switch(sig){
         case SIGINT:
         case SIGQUIT:
              leaving = 1;
               break;
         case SIGPIPE:
               break;
    }
}


int main (int argc, char *argv[])
{
    pid_t daemonpid;
    int retval;
    int i;
    struct sigaction new_action, old_action;
    cycle_key = G15_KEY_MR;
    unsigned char user[256];
    
    pthread_t keyboard_thread;
    pthread_t lcd_thread;
    

    for (i=0;i<argc;i++) {
        char daemonargs[20];
        memset(daemonargs,0,20);
        strncpy(daemonargs,argv[i],19);
        if (!strncmp(daemonargs, "-k",2) || !strncmp(daemonargs, "--kill",6)) {
                   daemonpid = uf_return_running();
                   if(daemonpid>0) {
                       kill(daemonpid,SIGINT);
                   } else
                       printf("G15Daemon not running\n");
 		   exit(0);
        }
        if (!strncmp(daemonargs, "-v",2) || !strncmp(daemonargs, "--version",9)) {
            float lg15ver = LIBG15_VERSION;
            printf("G15Daemon version %s - %s\n",VERSION,uf_return_running() >= 0 ?"Loaded & Running":"Not Running");
            printf("compiled with libg15 version %.3f\n\n",lg15ver/1000);
            exit(0);
        }    
        
        if (!strncmp(daemonargs, "-h",2) || !strncmp(daemonargs, "--help",6)) {
            printf("G15Daemon version %s - %s\n",VERSION,uf_return_running() >= 0 ?"Loaded & Running":"Not Running");
            printf("%s -h (--help) or -k (--kill) or -s (--switch) or -d (--debug) or -v (--version)\n\n -k will kill a previous incarnation,\n -h shows this help\n -s changes the screen-switch key from MR to L1\n -d debug mode - stay in foreground and output all debug messages to STDERR\n -v show version\n",argv[0]);
            exit(0);
        }

        if (!strncmp(daemonargs, "-s",2) || !strncmp(daemonargs, "--switch",8)) {
            cycle_key = G15_KEY_L1;
        }

        if (!strncmp(daemonargs, "-d",2) || !strncmp(daemonargs, "--debug",7)) {
            g15daemon_debug = 1;
        }
        
        if (!strncmp(daemonargs, "-u",2) || !strncmp(daemonargs, "--user",7)) {
            if(argv[i+1]!=NULL){
                strncpy(user,argv[i+1],128);
                i++;
            }
        }

    }
    if(uf_return_running()>=0) {
        g15daemon_log(LOG_ERR,"G15Daemon already running.. Exiting");
        exit(0);
    }
    if(!g15daemon_debug)
        daemon(0,0);

    if(uf_create_pidfile() == 0) {
        
        int fd;
        fd_set fds;
        lcdlist_t *lcdlist;
        pthread_attr_t attr;
        struct passwd *nobody;
        unsigned char location[1024];

        openlog("g15daemon", LOG_PID, LOG_DAEMON);
        if(strlen(user)==0){
            nobody = getpwnam("nobody");
        }else {
            nobody = getpwnam(user);
        }
        if (nobody==NULL)
        {
            nobody = getpwuid(geteuid());
            g15daemon_log(LOG_WARNING,"BEWARE: running as effective uid %i\n",nobody->pw_uid);
        }

        /* init stuff here..  */
        retval = initLibG15();
        setLCDContrast(1); 
        setLEDs(0);
        
        if(retval != G15_NO_ERROR){
            g15daemon_log(LOG_ERR,"Unable to find G15 keyboard or the keyboard is already handled. Exiting");
            goto exitnow;
        }
    
        /* all other processes/threads should be seteuid nobody */
        if(nobody!=NULL) {
            seteuid(nobody->pw_uid);
            setegid(nobody->pw_gid);
        }

        /* initialise the linked list */
        lcdlist = ll_lcdlist_init();
        lcdlist->nobody = nobody;
        pthread_mutex_init(&g15lib_mutex, NULL);
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr,512*1024); /* set stack to 512k - dont need 8Mb !! */
        pthread_attr_setscope(&attr,PTHREAD_SCOPE_SYSTEM);
        int thread_policy=SCHED_RR;
        pthread_attr_setschedpolicy(&attr,thread_policy);

        if (pthread_create(&keyboard_thread, &attr, keyboard_watch_thread, lcdlist) != 0) {
            g15daemon_log(LOG_ERR,"Unable to create keyboard listener thread.  Exiting");
            goto exitnow;
        }
        pthread_attr_setstacksize(&attr,128*1024); 
        /* all other threads have a lower priority... maybe */
        pthread_attr_setscope(&attr,PTHREAD_SCOPE_PROCESS);
        thread_policy=SCHED_OTHER;
        pthread_attr_setschedpolicy(&attr,thread_policy);

        if (pthread_create(&lcd_thread, &attr, lcd_draw_thread, lcdlist) != 0) {
            g15daemon_log(LOG_ERR,"Unable to create display thread.  Exiting");
            goto exitnow;
        }
        g15daemon_log(LOG_INFO,"%s loaded\n",PACKAGE_STRING);
        
        snprintf(location,1024,"%s/%s\0",DATADIR,"g15daemon/splash/g15logo2.wbmp");
        g15daemon_load_wbmp(lcdlist->tail->lcd,location);
        uf_write_buf_to_g15(lcdlist->tail->lcd);
	
        snprintf(location,1024,"%s/%s\0",DATADIR,"g15daemon/plugins");

        g15_open_all_plugins(lcdlist,location);

        new_action.sa_handler = g15daemon_sighandler;
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
    	sigaction(SIGQUIT, &new_action, NULL);
        
        do {
            sleep(1);
  
        } while( leaving == 0);

        g15daemon_log(LOG_INFO,"Leaving by request");

        pthread_join(lcd_thread,NULL);
        pthread_join(keyboard_thread,NULL);

#ifdef LIBG15_VERSION
#if LIBG15_VERSION >= 1100
        exitLibG15(); 
#endif
#endif
        ll_lcdlist_destroy(&lcdlist);

exitnow:
        /* return to root privilages for the final countdown */
        seteuid(0);
    setegid(0);
    closelog();
    unlink("/var/run/g15daemon.pid");
    }
    return 0;
}
