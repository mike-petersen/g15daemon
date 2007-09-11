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

        (c) 2007 Mike Lampard 

        $Revision$ -  $Date$ $Author$

        The daemon listens on localhost port 15550 for client connections,
        and arbitrates LCD display.  Allows for multiple simultaneous clients.
        Client screens can be cycled through by pressing the 'L1' key.

        This is a tiny g15daemon client which monitors DPMS activity and switches the G15 backlight off 
*/

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
#include <g15daemon_client.h>
#include <libg15.h>
#include <config.h>
#include <X11/Xlib.h>

#include <X11/Xproto.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/dpmsstr.h>
#include <X11/extensions/scrnsaver.h>


int leaving = 0;

extern Bool DPMSQueryExtension (Display *, int *event_ret, int *err_ret);
extern Status DPMSGetVersion (Display *, int *major_ret, int *minor_ret);
extern Bool DPMSCapable (Display *);
extern Status DPMSInfo (Display *, CARD16 *power_level, BOOL *state);


int toggle_g15lights(int g15screen_fd, int lightstatus, int fg_check) 
{
  int dummy=0;

  if(fg_check!=0 && lightstatus == False) {
     g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, dummy);
  }

  if(fg_check==0 && lightstatus == False)
    g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, dummy);

  if(lightstatus) {
     usleep(500);
     g15_send_cmd (g15screen_fd, G15DAEMON_BACKLIGHT, G15_BRIGHTNESS_MEDIUM);
     usleep(500);
     g15_send_cmd (g15screen_fd, G15DAEMON_KB_BACKLIGHT, G15_BRIGHTNESS_MEDIUM);
  } else {
     usleep(500);
     g15_send_cmd (g15screen_fd, G15DAEMON_BACKLIGHT, G15_BRIGHTNESS_DARK);
     usleep(500);
     g15_send_cmd (g15screen_fd, G15DAEMON_KB_BACKLIGHT, G15_BRIGHTNESS_DARK);
     
  }   
  return 0;
}

void fork_child(char *cmd)
{
  pid_t pid;
  if(cmd==NULL)
    return;
  if( (pid=fork()) == -1)
    return;
  if(pid)
    return;
  else {
    execlp(cmd,cmd,NULL);
    exit(0);
  }
}

void sighandler(int sig) 
{
  switch (sig) {
     case SIGINT:
     case SIGQUIT:
        leaving = 1;
        break;
     case SIGPIPE:
        break;
  }
}  
  
int main(int argc, char **argv) {
  Display *dpy;
  int g15screen_fd;
  int event;
  int error;
  BOOL enabled = True;
  CARD16 power = 0;
  int dummy = 0, lights = True, change = 0;
  int i;
  struct sigaction new_action;
  char * enable_cmd = NULL;
  char * disable_cmd = NULL;
  int dpms_timeout = 0;
  
  for (i=0;i<argc;i++) {
    char argument[20];
    memset(argument,0,20);
    strncpy(argument,argv[i],19);
    if (!strncmp(argument, "-a",2) || !strncmp(argument, "--activate",10)) {
      if(argv[i+1]!=NULL){
        sscanf(argv[i+1],"%i",&dpms_timeout);
        i++;
      }else{
        printf("%s --activate requires an argument <minutes to activation>\n",argv[0]);
        exit(1);
      }
    }
    if (!strncmp(argument, "-e",2) || !strncmp(argument, "--cmd-enable",12)) {
      if(argv[i+1]!=NULL){
        enable_cmd = malloc(strlen(argv[i+1])+1);
        memcpy(enable_cmd, argv[i+1], strlen(argv[i+1]));
        i++;
      }
    }
    if (!strncmp(argument, "-d",2) || !strncmp(argument, "--cmd-disable",13)) {
      if(argv[i+1]!=NULL){
        disable_cmd = malloc(strlen(argv[i+1])+1);
        memcpy(disable_cmd, argv[i+1], strlen(argv[i+1]));
        i++;
      }
    }
    if (!strncmp(argument, "-v",2) || !strncmp(argument, "--version",9)) {
      printf("%s version %s\n",argv[0],VERSION);
      exit(0);     
    }    
    if (!strncmp(argument, "-h",2) || !strncmp(argument, "--help",6)) {
      printf("  %s version %s\n  (c)2007 Mike Lampard\n\n",argv[0],VERSION);
      printf(" -a or --activate <minutes>        - cause DPMS to be activated in <minutes> if no activity.\n");
      printf("                                     By default, %s will simply monitor DPMS status, and\n",argv[0]);
      printf("                                     requires a screensaver to activate. \n");
      printf("                                     In this mode, no screensver is needed.\n");
      printf("                                     %s will shut the monitor down on its own after <minutes>\n",argv[0]);
      printf("                                     with no keyboard or mouse activity.\n");
      printf(" -e or --cmd-enable <cmd to run>   - Run program <cmd> when DPMS is activated.\n\n");
      printf(" -d or --cmd-disable <cmd to run>  - Run program <cmd> when DPMS is de-activated.\n\n");
      printf(" -v or --version                   - Show program version\n\n");
      printf(" -h or --help                      - This page\n\n");
      exit(0);
    }
  }
  dpy = XOpenDisplay(getenv("DISPLAY"));
  if (!dpy) {
    printf("Unable to open display %s - exiting\n",getenv("DISPLAY"));
    return 1;
  }
  
  do {
      if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon - retrying\n");
        sleep(2);
      }
  }while(g15screen_fd<0);

  if (!DPMSQueryExtension (dpy, &event, &error)) {
     printf ("XDPMS extension not supported.\n");
     return 1;
  }

  if (!DPMSCapable(dpy)) {
     printf("DPMS is not enabled... exiting\n");
     return 1; 
  }
  
  if (dpms_timeout>0) {
    DPMSEnable(dpy);
    DPMSSetTimeouts(dpy,dpms_timeout*60,dpms_timeout*60,0);
  }

  new_action.sa_handler = sighandler;
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGQUIT, &new_action, NULL);

  while(!leaving) {    
   int fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, dummy);
   
   if (! DPMSInfo (dpy, &power, &enabled)) {
     printf ("unable to get DPMS state.\n");
     return 1;
   }

   switch(power) {
    case DPMSModeOn: {
      if (lights == False) {
          change = 1;
          fork_child(enable_cmd);
      }
      lights = True;
      break;
    }
    case DPMSModeStandby:
    case DPMSModeSuspend:
    case DPMSModeOff: {
      if(lights == True){
          change = 1;
          fork_child(disable_cmd);
      }
      lights = False;
      break;
    }
   }
   
   if (fg_check==1 && lights == True && change == 0) { // foreground 
       g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, dummy);
   }

   if(change) {
    toggle_g15lights(g15screen_fd,lights,fg_check);
    change=0;
   }
   sleep (1);
  }

  close(g15screen_fd);
  if(enable_cmd!=NULL)
    free(enable_cmd);
  if(disable_cmd!=NULL)
    free(disable_cmd);
  XCloseDisplay(dpy);
  return 0;
}
