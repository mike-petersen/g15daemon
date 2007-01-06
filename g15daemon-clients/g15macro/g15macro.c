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

        (c) 2006 Mike Lampard 

        $Revision$ -  $Date$ $Author$

        This daemon listens on localhost port 15550 for client connections,
        and arbitrates LCD display.  Allows for multiple simultaneous clients.
        Client screens can be cycled through by pressing the 'L1' key.

        This is a macro recorder and playback utility for the G15 and g15daemon.
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

#include <pthread.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/XF86keysym.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>

#define  XK_MISCELLANY 
#define XK_LATIN1
#define XK_LATIN2
#include <X11/keysymdef.h>
        
int g15screen_fd;
int config_fd = 0;
g15canvas *canvas;

static Display *dpy;
static Window root_win;

int leaving = 0;
int display_timeout=500;

unsigned char recstring[1024];

static int mled_state = G15_LED_M1;
static int mkey_state = 0;
static int recording = 0;

typedef struct keypress_s {
    unsigned long keycode;
    unsigned long time_ms;
    unsigned char pressed;
    unsigned int mouse_x;
    unsigned int mouse_y;
    unsigned int buttons;
}keypress_t;

typedef struct keysequence_s {
    keypress_t recorded_keypress[128];
    unsigned int record_steps;
} keysequence_t;

typedef struct gkeys_s{
    unsigned int recorded;
    keysequence_t keysequence;
}gkeys_t;

typedef struct mstates_s {
    gkeys_t gkeys[18];
}mstates_t; 

mstates_t *mstates[3];

struct current_recording {
    keypress_t recorded_keypress[128];
}current_recording;

unsigned int rec_index=0;
const char *gkeystring[] = { "G1","G2","G3","G4","G5","G6","G7","G8","G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","Unknown" };

const int gkeycodes[] = { 177,152,190,208,129,130,231,209,210,136,220,143,246,251,137,138,133,183 };

const int mmedia_codes[] = {164, 162, 144, 153, 174, 176};

const long mmedia_defaults[] = {
    XF86XK_AudioStop,
    XF86XK_AudioPlay,
    XF86XK_AudioPrev,
    XF86XK_AudioNext,
    XF86XK_AudioLowerVolume,
    XF86XK_AudioRaiseVolume
};
/* because this is an X11 client, we can work around the kernel limitations on key numbers */
const long gkeydefaults[] = {
    /* M1 palette */
    XF86XK_Launch4,
    XF86XK_Launch5,
    XF86XK_Launch6,
    XF86XK_Launch7,
    XF86XK_Launch8,
    XF86XK_Launch9,
    XF86XK_LaunchA,
    XF86XK_LaunchB, 
    XF86XK_LaunchC,  
    XF86XK_LaunchD, 
    XF86XK_LaunchE, 
    XF86XK_LaunchF, 
    XF86XK_iTouch, 
    XF86XK_Calculater, 
    XF86XK_Support, 
    XF86XK_Word, 
    XF86XK_Messenger, 
    XF86XK_WebCam,
    /* M2 palette */
    XK_F13,
    XK_F14,
    XK_F15,
    XK_F16,
    XK_F17,
    XK_F18,
    XK_F19,
    XK_F20,
    XK_F21,
    XK_F22,
    XK_F23,
    XK_F24,
    XK_F25,
    XK_F26,
    XK_F27,
    XK_F28,
    XK_F29,
    XK_F30,
    /* M3 palette */
    XK_Tcedilla,
    XK_racute,
    XK_abreve,
    XK_lacute,
    XK_cacute,
    XK_ccaron,
    XK_eogonek,
    XK_ecaron,
    XK_dcaron,
    XK_dstroke,
    XK_nacute,
    XK_ncaron,
    XK_odoubleacute,
    XK_udoubleacute,
    XK_rcaron,
    XK_uring,
    XK_scaron,
    XK_abovedot
};

int map_gkey(keystate){
    int retval = -1;
    switch(keystate){
        case G15_KEY_G1:  retval = 0;   break;
        case G15_KEY_G2:  retval = 1;   break;
        case G15_KEY_G3:  retval = 2;   break;
        case G15_KEY_G4:  retval = 3;   break;
        case G15_KEY_G5:  retval = 4;   break;
        case G15_KEY_G6:  retval = 5;   break;
        case G15_KEY_G7:  retval = 6;   break;
        case G15_KEY_G8:  retval = 7;   break;
        case G15_KEY_G9:  retval = 8;   break;
        case G15_KEY_G10: retval = 9;   break;
        case G15_KEY_G11: retval = 10;   break;
        case G15_KEY_G12: retval = 11;   break;
        case G15_KEY_G13: retval = 12;   break;
        case G15_KEY_G14: retval = 13;   break;
        case G15_KEY_G15: retval = 14;   break;
        case G15_KEY_G16: retval = 15;   break;
        case G15_KEY_G17: retval = 16;   break;
        case G15_KEY_G18: retval = 17;   break;
    }
    return retval;
}


void record_complete(unsigned long keystate)
{
    char tmpstr[1024];
    int gkey = map_gkey(keystate);

    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,mled_state);

    if(!rec_index) // nothing recorded - delete prior recording
        memset(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress,0,sizeof(keysequence_t));
    else
        memcpy(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress, &current_recording, sizeof(keysequence_t));

    mstates[mkey_state]->gkeys[gkey].keysequence.record_steps=rec_index;

    memset(canvas->buffer,0,G15_BUFFER_LEN);
    if(rec_index){
        strcpy(tmpstr,"For key ");
        strcat(tmpstr,gkeystring[map_gkey(keystate)]);
        g15r_renderString (canvas, (unsigned char *)"Recording", 0, G15_TEXT_LARGE, 80-((strlen("Recording")/2)*8), 4);
        g15r_renderString (canvas, (unsigned char *)"Complete", 0, G15_TEXT_LARGE, 80-((strlen("Complete")/2)*8), 18);

    }else{
        strcpy(tmpstr,"From Key ");
        strcat(tmpstr,gkeystring[map_gkey(keystate)]);
        g15r_renderString (canvas, (unsigned char *)"Macro", 0, G15_TEXT_LARGE, 80-((strlen("Macro")/2)*8), 4);
        g15r_renderString (canvas, (unsigned char *)"Deleted", 0, G15_TEXT_LARGE, 80-((strlen("Deleted")/2)*8), 18);
    }
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_LARGE, 80-((strlen(tmpstr)/2)*8), 32);
    
    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

    memset(recstring,0,strlen((char*)recstring));
    rec_index = 0;
}

void macro_playback(unsigned long keystate)
{
    int i = 0;
    int gkey = map_gkey(keystate);
    if(gkey<0)
      return;
    /* if no macro has been recorded for this key, send the g15daemon default keycode */
    if(mstates[mkey_state]->gkeys[gkey].keysequence.record_steps==0){
        int mkey_offset=0;
        switch(mkey_state){
          case 0:
            mkey_offset = 0;
            break;
          case 1:
            mkey_offset = 18;
            break;
          case 2:
            mkey_offset = 36;
            break;
          default:
            mkey_offset=0;
        }
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, gkeydefaults[gkey+mkey_offset]),True, CurrentTime);
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, gkeydefaults[gkey+mkey_offset]),False, CurrentTime);
        return;
    }
    for(i=0;i<mstates[mkey_state]->gkeys[gkey].keysequence.record_steps;i++){
        XTestFakeKeyEvent(dpy, mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].keycode, 
                          mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].pressed, CurrentTime);
    }
}

void change_keymap(int offset){
    int i=0,j=0;
    for(i=offset;i<offset+18;i++,j++)
    {
      KeySym newmap[1];
      newmap[0]=gkeydefaults[i];
      XChangeKeyboardMapping (dpy, gkeycodes[j], 1, newmap, 1);
    }
    XFlush(dpy);
}

/* ensure that the multimedia keys are configured */
void configure_mmediakeys(){

   KeySym newmap[1];
   int i=0;
   for(i=0;i<6;i++){
     newmap[0]=mmedia_defaults[i];
     XChangeKeyboardMapping (dpy, mmedia_codes[i], 1, newmap, 1);
   }
   XFlush(dpy);
}

void *Lkeys_thread() {
    int keystate = 0;
    struct pollfd fds;
    char ver[5];
    int foo = 0;
    strncpy(ver,G15DAEMON_VERSION,3);
    float g15v;
    sscanf(ver,"%f",&g15v);

    while(!leaving){

        /* g15daemon series 1.2 need key request packets */
        if((g15v*10)<=18) {
            keystate = g15_send_cmd (g15screen_fd, G15DAEMON_GET_KEYSTATE, foo);
        } else {
            fds.fd = g15screen_fd;
            fds.events = POLLIN;
            fds.revents=0;
            if ((poll(&fds, 1, 500)) > 0)
                read (g15screen_fd, &keystate, sizeof (keystate));
        }

        if (keystate)
        {
            switch (keystate)
            {
                case G15_KEY_L5:{
                    int fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo);
                    if(fg_check)
                      leaving = 1;
                    break;
                }
                case G15_KEY_MR:
                    if(0==g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo)){
                        usleep(1000);
                        g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, foo);         
                    }
                    usleep(1000);
                    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS, G15_LED_MR | mled_state);
                    g15r_initCanvas (canvas);
                    g15r_renderString (canvas, (unsigned char *)"Recording", 0, G15_TEXT_LARGE, 80-((strlen("Recording")/2)*8), 1);
                    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

                    recording = 1;
                    XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                    memset(&current_recording,0,sizeof(current_recording));
                    break;
                case G15_KEY_M1:
                    mkey_state = 0;
                    mled_state = G15_LED_M1;
                    recording = 0;
                    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,G15_LED_M1);
                    change_keymap(0);
                    break;
                case G15_KEY_M2:
                    mkey_state = 1;
                    mled_state = G15_LED_M2;
                    recording = 0;
                    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,G15_LED_M2);
                    change_keymap(18);
                    break;
                case G15_KEY_M3:
                    mkey_state = 2;
                    mled_state = G15_LED_M3;
                    recording = 0;
                    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,G15_LED_M3);
                    change_keymap(36);
                    break;
                default:
                    if(keystate >=G15_KEY_G1 && keystate <=G15_KEY_G18){
                        if(recording==1){
                            record_complete(keystate);
                            recording = 0;
                            XUngrabKeyboard(dpy,CurrentTime);
                            XFlush(dpy);
                        } else {
                            macro_playback(keystate);
                        }
                    }
                    break;
            }
            keystate = 0;
        }
    }
    return NULL;
}

unsigned int g15daemon_gettime_ms(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec*1000+tv.tv_usec/1000);
}

void xkey_handler(XEvent *event) {

    static unsigned long lasttime;
    unsigned char keytext[256];
    unsigned int keycode = event->xkey.keycode;
    int press = True;

    if(event->type==KeyRelease){ // we only do keyreleases for some keys
        switch (XKeycodeToKeysym(dpy, keycode, 0)) {
            case XK_Shift_L:
            case XK_Shift_R:
            case XK_Control_L:
            case XK_Control_R:
            case XK_Caps_Lock:
            case XK_Shift_Lock:
            case XK_Meta_L:
            case XK_Meta_R:
            case XK_Alt_L:
            case XK_Alt_R:
            case XK_Super_L:
            case XK_Super_R:
            case XK_Hyper_L:
            case XK_Hyper_R:
                break;
            default: 
                return;
        }

        press = False;
    }    
    if(recording){
        current_recording.recorded_keypress[rec_index].keycode = keycode;
        current_recording.recorded_keypress[rec_index].pressed = press;
        if(rec_index==0)
            current_recording.recorded_keypress[rec_index].time_ms=0;
        else
            current_recording.recorded_keypress[rec_index].time_ms=g15daemon_gettime_ms() - lasttime;
        rec_index++;

        /* now the default stuff */
        XUngrabKeyboard(dpy,CurrentTime);
        XTestFakeKeyEvent(dpy, keycode, press, CurrentTime);
        XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(dpy);

        strcpy((char*)keytext,XKeysymToString(XKeycodeToKeysym(dpy, keycode, 0)));
        if(0==strcmp((char*)keytext,"space"))
            strcpy((char*)keytext," ");
        strcat((char*)recstring,(char*)keytext);
        g15r_renderString (canvas, (unsigned char *)recstring, 0, G15_TEXT_MED, 80-((strlen((char*)recstring)/2)*5), 22);
        g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

    }else
        rec_index=0;

        lasttime = g15daemon_gettime_ms();
}

static void* xevent_thread()
{
    XEvent event;
    long event_mask = KeyPressMask|KeyReleaseMask|FocusChangeMask|SubstructureNotifyMask;

    XSelectInput(dpy, root_win, event_mask);

    while(!leaving){
        if(XCheckMaskEvent(dpy, event_mask, &event)){
            switch(event.type) {
                case KeyPress:
                case KeyRelease: {
                    xkey_handler(&event);
                    break;
                }
                case FocusIn: 
                case FocusOut:
                case EnterNotify:
                case LeaveNotify:
                case MapNotify:
                case UnmapNotify:
                case MapRequest:
                case ConfigureNotify:
                case CreateNotify:
                case DestroyNotify:
                    break;
                case ReparentNotify: {
                        if(recording)
                            XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                        XFlush(dpy);
                        break;
                    }
                default:
                    printf("Unhandled event (%i) received\n",event.type);
            }
        }
        usleep(1000);
    }
    return NULL;
}

int myx_error_handler(Display *dpy, XErrorEvent *err){
    return 0;
}

void g15macro_sighandler(int sig) {
    switch(sig){
         case SIGINT:
         case SIGQUIT:
              leaving = 1;
               break;
         case SIGPIPE:
              leaving = 1;
               break;
    }
}

int main(int argc, char **argv)
{
    pthread_t Xkeys;
    pthread_t Lkeys;
    int xtest_major_version = 0;
    int xtest_minor_version = 0;
    struct sigaction new_action;
    int dummy;
    char configpath[1024];
    char splashpath[1024];

    int have_xtest = 0;

    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon\n");
        return 1;
    }

    strncpy(configpath,getenv("HOME"),1024);
    strncat(configpath,"/.g15macro",1024-strlen(configpath));
    mkdir(configpath,777);
    strncat(configpath,"/g15macro-data",1024-strlen(configpath));

    config_fd = open(configpath,O_RDONLY|O_SYNC);

    mstates[0] = malloc(sizeof(mstates_t));
    mstates[1] = (mstates_t*)malloc(sizeof(mstates_t));
    mstates[2] = (mstates_t*)malloc(sizeof(mstates_t));

    if(config_fd) {
      read(config_fd,mstates[0],sizeof(mstates_t));
      read(config_fd,mstates[1],sizeof(mstates_t));
      read(config_fd,mstates[2],sizeof(mstates_t));
      close(config_fd);
    }else {
      memset(mstates[0],0,sizeof(mstates));
      memset(mstates[1],0,sizeof(mstates));
      memset(mstates[2],0,sizeof(mstates));
    }
    g15_send_cmd (g15screen_fd,G15DAEMON_KEY_HANDLER, dummy);
    usleep(1000);
    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,mled_state);
    usleep(1000);
    canvas = (g15canvas *) malloc (sizeof (g15canvas));
    if (canvas != NULL) {
        g15r_initCanvas(canvas);
    } else {
        printf("Unable to initialise the libg15render canvas\nExiting\n");
        return 1;
    }

    dpy = XOpenDisplay(getenv("DISPLAY"));
    if (!dpy) {
        printf("Can't open display\n");
        return 1;
    }

    root_win = DefaultRootWindow(dpy);
    if (!root_win) {
        printf("Cant find root window\n");
        return 1;
    }

    have_xtest = XTestQueryExtension(dpy, &dummy, &dummy, &xtest_major_version, &xtest_minor_version);
    if(have_xtest == False || xtest_major_version < 2 || (xtest_major_version <= 2 && xtest_minor_version < 2))
    {
        printf("XTEST extension not supported\n\nExiting\n");
    }

    /* completely ignore errors and carry on */
    XSetErrorHandler(myx_error_handler);
    configure_mmediakeys();
    change_keymap(0);
    XFlush(dpy);
    
    new_action.sa_handler = g15macro_sighandler;
    new_action.sa_flags = 0;
    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGQUIT, &new_action, NULL);
    
    snprintf((char*)splashpath,1024,"%s/%s",DATADIR,"g15macro/splash/g15macro.wbmp");
    g15r_loadWbmpSplash(canvas, splashpath);
    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
    usleep(1000);
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr,32*1024); /* set stack to 64k - dont need 8Mb !! */

    pthread_create(&Xkeys, &attr, xevent_thread, NULL);
    pthread_create(&Lkeys, &attr, Lkeys_thread, NULL);
    do{
        display_timeout--;
        if(display_timeout<0) display_timeout=-1;
        if(recording)
            display_timeout=500;
        if(display_timeout<=0){
            int fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, dummy);
            if (fg_check==1) { // foreground 
                    g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, dummy);
            	    g15r_loadWbmpSplash(canvas, splashpath);
            }

           usleep(500*1000);
        }
    }while(!usleep(1000) &&  !leaving);
    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,0);
    usleep(1000);
    if(recording){
        recording = 0;
        XUngrabKeyboard(dpy,CurrentTime);
    }
    config_fd = open(configpath,O_CREAT|O_WRONLY|O_SYNC,0600);
    write(config_fd,mstates[0],sizeof(mstates_t));
    write(config_fd,mstates[1],sizeof(mstates_t));
    write(config_fd,mstates[2],sizeof(mstates_t));

    close(config_fd);
    pthread_join(Xkeys,NULL);
    pthread_join(Lkeys,NULL);
    /* revert the keymap to g15daemon default on exit */
    change_keymap(0);

    XCloseDisplay(dpy);    
    close(g15screen_fd);
    return 1;
}
