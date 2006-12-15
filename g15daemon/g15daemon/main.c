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
#include <libdaemon/daemon.h>
#include <pwd.h>

#include <config.h>
#include <libg15.h>
#include "g15daemon.h"
#include "logo.h"

#ifndef LIBG15_VERSION
#define LIBG15_VERSION 1000
#endif

/* all threads will exit if leaving >0 */
int leaving = 0;

unsigned int current_key_state;
unsigned int cycle_key;
unsigned int client_handles_keys = 0;
struct lcd_t *keyhandler = NULL;

extern unsigned int connected_clients;

static void *keyboard_watch_thread(void *lcdlist){
    
    lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
    
    static unsigned int lastkeypresses = 0;
    unsigned int keypresses = 0;
    int retval = 0;
    current_key_state = 0;
    
    while (!leaving) {
        
        pthread_mutex_lock(&g15lib_mutex);
          retval = getPressedKeys(&keypresses, 40);
        pthread_mutex_unlock(&g15lib_mutex);
        
        if(retval == G15_NO_ERROR){
            if(keypresses != lastkeypresses){
                current_key_state = keypresses;
                g15_process_keys(displaylist, keypresses,lastkeypresses);
                if(client_handles_keys)
                    send_keystate(keyhandler);
                lastkeypresses = keypresses;
            }
        }
        pthread_msleep(10);
    }
    return NULL;
}

static void *lcd_draw_thread(void *lcdlist){

    lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
    static long int lastlcd = 1;
    
    lcd_t *displaying = displaylist->tail->lcd;
    memset(displaying->buf,0,1024);
    
    writePixmapToLCD(logo_data);
    pthread_sleep(2);
    
    while (!leaving) {
        pthread_mutex_lock(&lcdlist_mutex);
        
        displaying = displaylist->current->lcd;
        
        if(displaylist->tail == displaylist->current){
            lcdclock(displaying);
            displaying->mkey_state = 0;
        }
        
        if(displaying->ident != lastlcd){
           write_buf_to_g15(displaying);
           lastlcd = displaying->ident;
        }
        
        if(displaying->state_changed ){
            setLCDContrast(displaying->contrast_state);
            if(connected_clients)
                displaying->mkey_state = displaying->mkey_state | G15_LED_MR;
            setLEDs(displaying->mkey_state);
            setLCDBrightness(displaying->backlight_state);
            displaying->state_changed = 0;
        }
            
        pthread_mutex_unlock(&lcdlist_mutex);
        
        pthread_msleep(100);
    }
    return NULL;
}

/* this thread only listens for new connections. 
 * sockserver_accept will spawn a new thread for each connected client
 */
static void *lcdserver_thread(void *lcdlist){

    lcdlist_t *displaylist = (lcdlist_t*) lcdlist ;
    int g15_socket=-1;
    
    if((g15_socket = init_sockserver())<0){
        daemon_log(LOG_ERR,"Unable to initialise the server at port %i",LISTEN_PORT);
        return NULL;
    }
    
    if (fcntl(g15_socket, F_SETFL, O_NONBLOCK) <0 ) {
        daemon_log(LOG_ERR,"Unable to set socket to nonblocking");
    }

    while ( !leaving ) {
        g15_clientconnect(&displaylist,g15_socket);
    }
    
    close(g15_socket);
    return NULL;
}


int main (int argc, char *argv[])
{
    pid_t daemonpid;
    int retval;
    int i;
    int g15daemon_debug = 0;
        
    pthread_t keyboard_thread;
    pthread_t lcd_thread;
    pthread_t server_thread;

    daemon_pid_file_ident = 
            daemon_log_ident = 
            daemon_ident_from_argv0(argv[0]);

    
    for (i=0;i<argc;i++) {
        char daemonargs[20];
        memset(daemonargs,0,20);
        strncpy(daemonargs,argv[i],19);
        if (!strncmp(daemonargs, "-k",2) || !strncmp(daemonargs, "--kill",6)) {
#ifdef DAEMON_PID_FILE_KILL_WAIT_AVAILABLE 
            if ((retval = daemon_pid_file_kill_wait(SIGINT, 15)) != 0)
#else
                if ((retval = daemon_pid_file_kill(SIGINT)) != 0)
#endif
                    daemon_log(LOG_WARNING, "Failed to kill daemon");
            return retval < 0 ? 1 : 0;
        }
        if (!strncmp(daemonargs, "-v",2) || !strncmp(daemonargs, "--version",9)) {
            float lg15ver = LIBG15_VERSION;
            printf("G15Daemon version %s - %s\n",VERSION,daemon_pid_file_is_running() >= 0 ?"Loaded & Running":"Not Running");
            printf("compiled with libg15 version %.3f\n\n",lg15ver/1000);
            exit(0);
        }    
        
        if (!strncmp(daemonargs, "-h",2) || !strncmp(daemonargs, "--help",6)) {
            printf("G15Daemon version %s - %s\n",VERSION,daemon_pid_file_is_running() >= 0 ?"Loaded & Running":"Not Running");
            printf("%s -h (--help) or -k (--kill) or -s (--switch) or -d (--debug) or -v (--version)\n\n -k will kill a previous incarnation,\n -h shows this help\n -s changes the screen-switch key from MR to L1\n -d debug mode - stay in foreground and output all debug messages to STDERR\n -v show version\n",argv[0]);
            exit(0);
        }

        if (!strncmp(daemonargs, "-s",2) || !strncmp(daemonargs, "--switch",8)) {
            cycle_key = G15_KEY_L1;
        }else{
            cycle_key = G15_KEY_MR;
        }

        if (!strncmp(daemonargs, "-d",2) || !strncmp(daemonargs, "--debug",7)) {
            g15daemon_debug = 1;
        }
    }

    if ((daemonpid = daemon_pid_file_is_running()) >= 0) {
        printf("%s is already running.  Use \'%s -k\' to kill the daemon before running again.\nExiting now\n",argv[0],argv[0]);
        return 1;
    }

    daemon_retval_init();

    if(!g15daemon_debug) {
      if((daemonpid = daemon_fork()) < 0){
          daemon_retval_done();
          return 1;
      }
    }
      
    if (daemonpid && !g15daemon_debug){
        retval=0;
        char * g15_errors[] = {	"No Error",
                                "Unable to write to PID file",
                                "Unable to initialise keyboard",
                                "Unable to configure the linux kernel UINPUT driver",
                                "Unable to register signal handler",
                                "Unable to create new keyboard thread", 
                                "Unable to create new display thread",
                                "Unable to create server thread",
                                NULL };
          if((retval = daemon_retval_wait(20)) !=0) {
            if(retval)
                daemon_log(LOG_ERR,"An Error Occurred - %i : ( %s ) received",retval, g15_errors[retval]);
              else
                 daemon_log(LOG_ERR,"A library error occurred.  Please file a bug report stating the g15daemon version, your kernel version, libdaemon version and your distribution name.");
              return 255;
          }
    
        return retval;
    
    }else{ /* daemonised now */

        int fd;
        fd_set fds;
        lcdlist_t *lcdlist;
        pthread_attr_t attr;
        struct passwd *nobody;
        
        nobody = getpwnam("nobody");
            
        if(daemon_pid_file_create() !=0){
            daemon_log(LOG_ERR,"Unable to create PID File! Exiting");
            daemon_retval_send(1);   
            goto exitnow;
        }

        /* init stuff here..  */
        retval = initLibG15();
        if(retval != G15_NO_ERROR){
            daemon_log(LOG_ERR,"Unable to find G15 keyboard or the keyboard is already handled. Exiting");
            daemon_retval_send(2);
            goto exitnow;

        setLCDContrast(1); 
        setLEDs(0);
        
        }
#ifdef HAVE_LINUX_UINPUT_H
        retval = g15_init_uinput();
#else
        daemon_log(LOG_WARNING,"Compiled without Uinput support, extra keys will not be available");
#endif
        if(retval !=0){
            daemon_log(LOG_ERR,"Unable to setup the UINPUT device. Exiting");
            daemon_retval_send(3);
            goto exitnow;
        }
    
        if(daemon_signal_init(SIGINT,SIGQUIT,SIGHUP,SIGPIPE,0) <0){
            daemon_log(LOG_ERR,"Unable to register signal handler. Exiting");
            daemon_retval_send(4);
            goto exitnow;
        }

        /* all other processes/threads should be seteuid nobody */
        if(nobody!=NULL) {
            seteuid(nobody->pw_uid);
            setegid(nobody->pw_gid);
        }
                                                    
        /* initialise the linked list */
        lcdlist = lcdlist_init();
        pthread_mutex_init(&g15lib_mutex, NULL);
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr,512*1024); /* set stack to 512k - dont need 8Mb !! */

        if (pthread_create(&keyboard_thread, &attr, keyboard_watch_thread, lcdlist) != 0) {
            daemon_log(LOG_ERR,"Unable to create keyboard listener thread.  Exiting");
            daemon_retval_send(5);
            goto exitnow;
        }
        pthread_attr_setstacksize(&attr,128*1024); 

        if (pthread_create(&lcd_thread, &attr, lcd_draw_thread, lcdlist) != 0) {
            daemon_log(LOG_ERR,"Unable to create display thread.  Exiting");
            daemon_retval_send(6);
            goto exitnow;
        }

        if (pthread_create(&server_thread, &attr, lcdserver_thread, lcdlist) != 0) {
            daemon_log(LOG_ERR,"Unable to create lcd-client server thread.  Exiting");
            daemon_retval_send(7);
            goto exitnow;
        }
        daemon_retval_send(0);
        daemon_log(LOG_INFO,"%s loaded\n",PACKAGE_STRING);
        FD_ZERO(&fds);
        FD_SET(fd=daemon_signal_fd(),&fds);
    
        do {
            fd_set myfds = fds;
            if(select(FD_SETSIZE,&myfds,0,0,0) <0){
                if(errno == EINTR) continue;
                break;
            }
        
            if(FD_ISSET(fd,&fds)){
                int sig;
                sig = daemon_signal_next();
                switch(sig){
                    case SIGINT:
                    case SIGQUIT:
                        leaving = 1;
                        daemon_log(LOG_INFO,"Leaving by request");
                        break;
                    case SIGPIPE:
                        break;
                }
            }
        } while ( leaving == 0 );
        
        daemon_signal_done();
        pthread_join(server_thread,NULL);
        pthread_join(lcd_thread,NULL);
        pthread_join(keyboard_thread,NULL);
#ifdef HAVE_LINUX_UINPUT_H
        g15_exit_uinput();
#endif
#ifdef LIBG15_VERSION
#if LIBG15_VERSION >= 1100
        exitLibG15(); 
#endif
#endif
        lcdlist_destroy(&lcdlist);
    }

exitnow:
    /* return to root privilages for the final countdown */
    seteuid(0);
    setegid(0);
                
    daemon_retval_done();
    daemon_pid_file_remove();
    return 0;
}
