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
    
    This is a simple frontend for the Media Player Daemon (MPD)
*/

#include <libmpd/libmpd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>
#include <poll.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>

extern int debug_level;
int g15screen_fd, retval;
g15canvas *canvas;

static Display *dpy;
static Window root_win;

static int playing;
static int paused;

MpdObj *obj = NULL;

int leaving = 0;
static int voltimeout=0;

static int menulevel=0;
#define MAX_MENU_MODES 2

void error_callback(MpdObj *mi,int errorid, char *msg, void *userdata)
{
} 

void *Lkeys_thread() {
    int keystate = 0;
    int volume;
    struct pollfd fds;
    char ver[5];
    int foo;
    strncpy(ver,G15DAEMON_VERSION,3);
    float g15version;
    sscanf(ver,"%f",&g15version);

    fds.fd = g15screen_fd;
    fds.events = POLLIN;
    
    while(!leaving){
        /* g15daemon series 1.2 need key request packets */
        if(g15version<1.9)
          keystate = g15_send_cmd (g15screen_fd, G15DAEMON_GET_KEYSTATE, foo);
        else        
          if ((poll(&fds, 1, 5)) > 0)
            read (g15screen_fd, &keystate, sizeof (keystate));
        if (keystate)
        {
            switch (keystate)
            {
                case G15_KEY_L1:
                    break;
                case G15_KEY_L2:
                    menulevel++;
                    if(menulevel>=MAX_MENU_MODES)
                        menulevel=0;
                    printf("L2 pressed (entering mode %i)\n",menulevel);
                    break;
                case G15_KEY_L3:
                    printf("L3 pressed... nothing to do yet\n");
                    break;
                case G15_KEY_L4:
                    if(menulevel==0){
                        mpd_player_set_random(obj,mpd_player_get_random(obj)^1);
                    }
                    if(menulevel==1){
                        volume=mpd_status_get_volume(obj);
                        if(volume>0)
                            volume-=10;
                        mpd_status_set_volume (obj,volume);
                    }
                    break;
                case G15_KEY_L5:
                    if(menulevel==0){
                        mpd_player_set_repeat(obj, mpd_player_get_repeat(obj)^1);
                    }
                    if(menulevel==1){
                        volume=mpd_status_get_volume(obj);
                        if(volume<100)
                            volume+=10;
                        mpd_status_set_volume (obj,volume);
                    }
                    break;
                default:
                    break;
            }
            keystate = 0;
        }
        usleep(25000);
    }
    return NULL;
}

static void* poll_mmediakeys()
{
    long mask = KeyPressMask;
    XEvent event;
    while(!leaving){
        while (XCheckMaskEvent(dpy, mask, &event)){
            if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioPlay)) {
                if(playing) {
                    if (paused)  {
                        mpd_player_play(obj);
                        paused = 0;
                    } else {
                        mpd_player_pause(obj);
                        paused = 1;
                    }
                } else
                    mpd_player_play(obj);
                    playing = 1;
            }

            if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioStop)){
                mpd_player_stop(obj);
                playing = 0;
            }

            if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioNext))
                mpd_player_next(obj);

            if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioPrev))
                mpd_player_prev(obj);
        }
        usleep(100*1000);
        if(voltimeout)
            voltimeout--;
    }
    return NULL;
}

pthread_mutex_t lockit;

void status_changed(MpdObj *mi, ChangedStatusType what)
{
    long chksum=0;
    static long last_chksum;
    int i;
    static char artist[100];
    static char title[100];

    pthread_mutex_lock(&lockit);
    mpd_Song *song = mpd_playlist_get_current_song(mi);
    if(song) {
       g15r_pixelBox (canvas, 0, 0, 159, 20, G15_COLOR_WHITE, 1, 1);
       
       if(song->artist!=NULL)
          strncpy(artist,song->artist,99);
       if(song->title!=NULL)
          strncpy(title,song->title,99);
    }

    g15r_renderString (canvas, (unsigned char *)artist, 0, G15_TEXT_LARGE, 80-(strlen(artist)*8)/2, 2);
    g15r_renderString (canvas, (unsigned char *)title, 0, G15_TEXT_MED, 80-(strlen(title)*5)/2, 12);

    if(what&MPD_CST_CROSSFADE){
	// printf(GREEN"X-Fade:"RESET" %i sec.\n",mpd_status_get_crossfade(mi));
    }
    
    if(what&MPD_CST_PLAYLIST)
    {
	// printf(GREEN"Playlist changed"RESET"\n");
    }

    if(what&MPD_CST_ELAPSED_TIME && !voltimeout){
        unsigned char time_elapsed[41];
        unsigned char time_total[41];
        int elapsed = mpd_status_get_elapsed_song_time(mi);
        int total = mpd_status_get_total_song_time(mi);

        memset(time_elapsed,0,41);
        memset(time_total,0,41);
        snprintf((char*)time_elapsed,40,"%02i:%02i",elapsed/60, elapsed%60);
        snprintf((char*)time_total,40,"%02i:%02i",total/60, total%60);

        if(elapsed>0&&total>0)
          g15r_drawBar (canvas, 10, 22, 149, 30, G15_COLOR_BLACK, elapsed, total, 1);
        
        canvas->mode_xor=1;
        g15r_renderString (canvas,(unsigned char*)time_elapsed,0,G15_TEXT_MED,12,23);
        g15r_renderString (canvas,(unsigned char*)time_total,0,G15_TEXT_MED,124,23);
        canvas->mode_xor=0;
    }

    if(what&MPD_CST_VOLUME||voltimeout>0){
        static int volume;
        if(what&MPD_CST_VOLUME){
            voltimeout=5;
            volume = mpd_status_get_volume(mi);
        }
        if(voltimeout<0)
            voltimeout=0;
        
        g15r_drawBar (canvas,10, 22, 149, 30, G15_COLOR_BLACK, volume, 100, 1);
        canvas->mode_xor=1;
        g15r_renderString (canvas, (unsigned char *)"Volume", 0, G15_TEXT_LARGE, 59, 23);
        canvas->mode_xor=0;
    }

    if(what&MPD_CST_STATE)
    {
        switch(mpd_player_get_state(mi))
        {
            case MPD_PLAYER_PLAY:
                playing=1;
                paused=0;
                break;
            case MPD_PLAYER_PAUSE:
                g15r_pixelBox (canvas, 10, 22, 149, 30, G15_COLOR_WHITE, 1, 1);
                g15r_renderString (canvas, (unsigned char *)"Playback Paused", 0, G15_TEXT_LARGE, 22, 23);
                paused=1;
                break;
            case MPD_PLAYER_STOP:
                g15r_pixelBox (canvas, 10, 22, 149, 30, G15_COLOR_WHITE, 1, 1);
                g15r_renderString (canvas, (unsigned char *)"Playback Stopped", 0, G15_TEXT_LARGE, 18, 23);
                playing=0;
                paused=0;
                break;
            default:
                break;
        }
    }

    g15r_pixelBox (canvas, 1, 34, 158 , 41, G15_COLOR_WHITE, 1, 1);
    g15r_pixelBox (canvas, 10, 34, 27, 42, G15_COLOR_BLACK, 1, 1);
    canvas->mode_xor=1;
    g15r_renderString (canvas, (unsigned char *)"mode", 0, G15_TEXT_SMALL, 11, 36);
    canvas->mode_xor=0;
    
    if(menulevel==0){
        if(mpd_player_get_random(mi)){
        g15r_drawLine (canvas, 43, 42, 158, 42,G15_COLOR_WHITE);
            canvas->mode_xor=1;
            g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 1);
        }else
            g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 0);
            
            g15r_renderString (canvas, (unsigned char *)"Rndm", 0, G15_TEXT_SMALL, 108, 36);
            if(mpd_player_get_random(mi))
                canvas->mode_xor=0;
                        
            if(mpd_player_get_repeat(mi)){
                canvas->mode_xor=1;
                g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 1);
            }else
                g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 0);
            
                g15r_renderString (canvas, (unsigned char *)"Rpt", 0, G15_TEXT_SMALL, 136, 36);
                if(mpd_player_get_repeat(mi))
                    canvas->mode_xor=0;
	// 2nd box from left - if you want it...
        //g15r_pixelBox (canvas, 34, 34, 54 , 42, G15_COLOR_BLACK, 1, 0);
        //g15r_renderString (canvas, (unsigned char *)"test", 0, G15_TEXT_SMALL, 36, 36);
    }
    
    if(menulevel==1){
        g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 0);
        g15r_renderString (canvas, (unsigned char *)"Vol-", 0, G15_TEXT_SMALL, 108, 36);
    
        g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 0);
        g15r_renderString (canvas, (unsigned char *)"Vol+", 0, G15_TEXT_SMALL, 132, 36);
	
        // 2nd box from left - if you want it...
        //g15r_pixelBox (canvas, 34, 34, 54 , 42, G15_COLOR_BLACK, 1, 0);
        //g15r_renderString (canvas, (unsigned char *)"test", 0, G15_TEXT_SMALL, 36, 36);
    }
    
    /* do a quicky checksum - only send frame if different */
    chksum=0;
    for(i=0;i<G15_BUFFER_LEN;i++){
        chksum+=canvas->buffer[i]*i;
    }
    if(last_chksum!=chksum) {
        while(g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN)<0 && !leaving) {
            perror("lost connection, tryng again\n");
            usleep(10000);
            /* connection error occurred - try to reconnect to the daemon */
            g15screen_fd=new_g15_screen(G15_G15RBUF);
        }
    }
    last_chksum=chksum;
    pthread_mutex_unlock(&lockit);
    usleep(100*1000);
}


int myx_error_handler(Display *dpy, XErrorEvent *err){
    printf("error (%i) occured - ignoring\n",err->error_code);
    return 0;
}

int main(int argc, char **argv)
{
    int fdstdin = 0;
    pthread_t Xkeys;
    pthread_t Lkeys;
    int iport = 6600;
    char *hostname = getenv("MPD_HOST");
    char *port = getenv("MPD_PORT");
    char *password = getenv("MPD_PASSWORD");
    pthread_mutex_init(&lockit,NULL);
    /* Make the input non blocking */

    /* set correct hostname */	
    if(!hostname) {
        hostname = "localhost";
    }
    if(port){
        iport = atoi(port);
    }

    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon\n");
        return 1;
    }

    canvas = (g15canvas *) malloc (sizeof (g15canvas));
    if (canvas != NULL) {
        memset(canvas->buffer, 0, G15_BUFFER_LEN);
        canvas->mode_cache = 0;
        canvas->mode_reverse = 0;
        canvas->mode_xor = 0;
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

    /* completely ignore errors and carry on */
    XSetErrorHandler(myx_error_handler);
    XFlush(dpy);

    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioPlay), AnyModifier, root_win,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioStop), AnyModifier, root_win,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioPrev), AnyModifier, root_win,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioNext), AnyModifier, root_win,
             False, GrabModeAsync, GrabModeAsync);

    /* Create mpd object */
    obj = mpd_new(hostname, iport,password); 
    /* Connect signals */
    mpd_signal_connect_error(obj,(ErrorCallback)error_callback, NULL);
    mpd_signal_connect_status_changed(obj,(StatusChangedCallback)status_changed, NULL);
    /* Set timeout */
    mpd_set_connection_timeout(obj, 10);

    if(!mpd_connect(obj))
    {
        char buffer[20];
        pthread_attr_t attr;

        mpd_send_password(obj);
        memset(buffer, '\0', 20);
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr,32*1024); /* set stack to 64k - dont need 8Mb !! */

        pthread_create(&Xkeys, &attr, poll_mmediakeys, NULL);
        pthread_create(&Lkeys, &attr, Lkeys_thread, NULL);

        do{
            mpd_status_update(obj);
        }while(!usleep(10000) &&  !leaving);
    }
    mpd_free(obj);
    close(fdstdin);

    if(canvas!=NULL)
        free(canvas);

    close(g15screen_fd);
    leaving = 1;
    pthread_join(Xkeys,NULL);
    XUngrabKey(dpy, XF86XK_AudioPrev, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioNext, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioPlay, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioStop, AnyModifier, root_win);
    
    pthread_mutex_destroy(&lockit);
    
    return 1;
}
