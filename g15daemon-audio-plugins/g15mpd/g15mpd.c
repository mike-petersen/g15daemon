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
#include <poll.h>
#include <arpa/inet.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>
#include <poll.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
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
int have_xtest=0;
static int voltimeout=0;
static int own_keyboard=0;

pthread_mutex_t daemon_mutex;
pthread_mutex_t lockit;

static int menulevel=0;
#define MENU_MODE1 0
#define MENU_MODE2 1
#define MAX_MENU_MODES 2
/* playlist mode takes over all keys on the keyboard - allowing searches/playlist scroll via volume ctrl etc. 
all non-valid keys are sent elsewhere via the xtest extension */	
static int playlist_mode=0;
int playlist_selection=0;
int item_selected=0;

struct track_info {
    char artist[100];
    char title[100];
    int total;
    int elapsed;
    int volume;
    int repeat;
    int random;
    int playstate;
    int totalsongs_in_playlist;
    int currentsong;
} track_info;

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
    float g15v;
    sscanf(ver,"%f",&g15v);

    fds.fd = g15screen_fd;
    fds.events = POLLIN;
    
    while(!leaving){
       int foo=0;
       int current_fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo);
       static int last_fg_check = 0;
       if(playlist_mode && last_fg_check != current_fg_check){
         if(own_keyboard){
           if(current_fg_check==0){
               own_keyboard=0;
               XUngrabKeyboard(dpy,CurrentTime);
               XFlush(dpy);
            }
         }else if(current_fg_check && !own_keyboard) {
            own_keyboard=1;
            XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
         }
         last_fg_check = current_fg_check;   
       }
    

        /* g15daemon series 1.2 need key request packets */
       pthread_mutex_lock(&daemon_mutex);
       if((g15v*10)<=18) {
            keystate = g15_send_cmd (g15screen_fd, G15DAEMON_GET_KEYSTATE, foo);
       } else {
            if ((poll(&fds, 1, 5)) > 0)
                read (g15screen_fd, &keystate, sizeof (keystate));
        }
        pthread_mutex_unlock(&daemon_mutex);

        if (keystate)
        {
            switch (keystate)
            {
                case G15_KEY_L1:
                    exit(1); // FIXME quick hack to exit
                    break;
                case G15_KEY_L2:
                    menulevel++;
                    if(menulevel>=MAX_MENU_MODES)
                        menulevel=0;
                    break;
                case G15_KEY_L3:
                    if(!own_keyboard){ //activate keyboard grab mode
                        own_keyboard=playlist_mode=1;
                        XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                        mpd_Song *song = mpd_playlist_get_current_song(obj);
                        if(song)
                            if(song->pos)
                                track_info.currentsong=song->pos;
                    }else{ //de-activate
                        own_keyboard=playlist_mode=0;
                        XUngrabKeyboard(dpy,CurrentTime);
                    }
                    break;
                case G15_KEY_L4:
                    if(menulevel==MENU_MODE1){
                        mpd_player_set_random(obj,mpd_player_get_random(obj)^1);
                    }
                    if(menulevel==MENU_MODE2){
                        volume=mpd_status_get_volume(obj);
                        if(volume>0)
                            volume-=10;
                        mpd_status_set_volume (obj,volume);
                    }
                    break;
                case G15_KEY_L5:
                    if(menulevel==MENU_MODE1){
                        mpd_player_set_repeat(obj, mpd_player_get_repeat(obj)^1);
                    }
                    if(menulevel==MENU_MODE2){
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
        usleep(100*1000);
    }
    return NULL;
}

void xkey_handler(XEvent *event){
    
    int keycode = event->xkey.keycode;
    int code_audio_play = XKeysymToKeycode(dpy,XF86XK_AudioPlay);
    int code_audio_stop = XKeysymToKeycode(dpy, XF86XK_AudioStop);
    int code_audio_next = XKeysymToKeycode(dpy, XF86XK_AudioNext);
    int code_audio_prev = XKeysymToKeycode(dpy, XF86XK_AudioPrev);
    int code_audio_raisevol = XKeysymToKeycode(dpy, XF86XK_AudioRaiseVolume);
    int code_audio_lowervol = XKeysymToKeycode(dpy, XF86XK_AudioLowerVolume);

    if(keycode == code_audio_play) {
        if(playing && !playlist_mode) {
            if (paused)  {
                mpd_player_play(obj);
                paused = 0;
            } else {
                mpd_player_pause(obj);
                paused = 1;
            }
        } else {
            mpd_player_play(obj);
            playing = 1;
        }
        if(playlist_mode){
            mpd_player_play_id(obj, item_selected);
        }
        return;
    }

    if(keycode == code_audio_stop) {
        mpd_player_stop(obj);
        playing = 0;
        return;
    }
    
    if(keycode == code_audio_next) {
        mpd_player_next(obj);
        return;
    }

    if(keycode == code_audio_prev) {
        mpd_player_prev(obj);
        return;
    }

    if(keycode == code_audio_raisevol){
        playlist_selection = 1;
        return;
    }
    if(keycode == code_audio_lowervol){
        playlist_selection = -1;
        return;
    }

    /* now the default stuff */
    if(own_keyboard) {
        menulevel=MENU_MODE1;
        XUngrabKeyboard(dpy,CurrentTime);
        XFlush(dpy);
    }

    if(have_xtest) { // send the keypress elsewhere 
        if(event->type==KeyPress){
            XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
        }
        XFlush(dpy);
    }

    if(own_keyboard && have_xtest) { // we only regrab if the XTEST extension is available.
        XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    }
    XFlush(dpy);

}

static void* xevent_thread()
{
    XEvent event;
    long event_mask = KeyPressMask|FocusChangeMask|SubstructureNotifyMask;

    XSelectInput(dpy, root_win, event_mask);

    while(!leaving){
        if(XCheckMaskEvent(dpy, event_mask, &event)){
            switch(event.type) {
                case KeyPress: {
                    xkey_handler(&event);
                    break;
                }
                case KeyRelease: {
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
                        if(own_keyboard && have_xtest) { // we only regrab if the XTEST extension is available.
                            XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                        }
                        XFlush(dpy);
                        break;
                    }
                default:
                    printf("Unhandled event (%i) received\n",event.type);
            }
        }
        if(voltimeout)
            voltimeout--;

        usleep(1000);
    }
    return NULL;
}

/* take the track_info structure and render it */
void *g15display_thread(){
    long chksum=0;
    static long last_chksum;
    int i;
    unsigned char time_elapsed[41];
    unsigned char time_total[41];
    static int current = 0;
    int changed =0;
    while(!leaving){
        if(playlist_mode){
    
            int y=0;
            int offset=2;
            changed = 0;
            
            if(track_info.currentsong>-1){
                current=track_info.currentsong;
                track_info.currentsong = -1;
                changed = 1;
            }

            if(playlist_selection>0){
                if(current+1<mpd_playlist_get_playlist_length(obj)){
                    current+=playlist_selection;
                    changed = 1;
                }
                playlist_selection=0;
            }
            if(playlist_selection<0){
                if(current) {
                    current--;
                    changed = 1;
                }
                playlist_selection=0;
            }



            if(current-offset<0)
                offset-=current;
            
            if(changed){
                g15r_pixelBox (canvas, 0, 0, 159, 42, G15_COLOR_WHITE, 1, 1);
            for(i=current-offset;i<current+6;i++){
                char title[100];
                mpd_Song *song;

                song = mpd_playlist_get_song_from_pos(obj, i);
                if(song) {
                    if(song->title!=NULL)
                        strncpy(title,song->title,99);
                    else
                        strncpy(title,"",99);
                    if(song->artist!=NULL){
                        strncat(title," - ",99);
                        strncat(title,song->artist,99);
                    }
                }
                /* sanitise the display */
                if(i==mpd_playlist_get_playlist_length(obj))
                    strncpy(title,"End of PlayList",99);
                if(i>mpd_playlist_get_playlist_length(obj))
                    break;
                if(i<0)
                    strncpy(title,"",99);

                if(y==offset){
                    g15r_pixelBox (canvas, 0, 7*offset, 159 , 7*(offset+1), G15_COLOR_BLACK, 1, 1);
                    canvas->mode_xor=1;
                    if(song)
                        if(song->id)
                            item_selected=song->id;
                }
                g15r_renderString (canvas, (unsigned char *)title, y, G15_TEXT_MED, 1, 1);
                canvas->mode_xor=0;
                y++;
            }
            }
        }else{
            /* track info */
            g15r_pixelBox (canvas, 0, 0, 159, 42, G15_COLOR_WHITE, 1, 1);
            g15r_renderString (canvas, (unsigned char *)track_info.artist, 0, G15_TEXT_LARGE, 80-(strlen(track_info.artist)*8)/2, 2);
            g15r_renderString (canvas, (unsigned char *)track_info.title, 0, G15_TEXT_MED, 80-(strlen(track_info.title)*5)/2, 12);

            /* elapsed time */
            memset(time_elapsed,0,41);
            memset(time_total,0,41);
            snprintf((char*)time_elapsed,40,"%02i:%02i",track_info.elapsed/60, track_info.elapsed%60);
            snprintf((char*)time_total,40,"%02i:%02i",track_info.total/60, track_info.total%60);
            if(track_info.elapsed>0&&track_info.total>0)
                g15r_drawBar (canvas, 10, 22, 149, 30, G15_COLOR_BLACK, track_info.elapsed, track_info.total, 1);
            canvas->mode_xor=1;
            g15r_renderString (canvas,(unsigned char*)time_elapsed,0,G15_TEXT_MED,12,23);
            g15r_renderString (canvas,(unsigned char*)time_total,0,G15_TEXT_MED,124,23);
            canvas->mode_xor=0;

            switch(track_info.playstate)
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

            g15r_pixelBox (canvas, 1, 34, 158 , 41, G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, 10, 34, 27, 42, G15_COLOR_BLACK, 1, 1);
            canvas->mode_xor=1;
            g15r_renderString (canvas, (unsigned char *)"mode", 0, G15_TEXT_SMALL, 11, 36);
            canvas->mode_xor=0;

            if(menulevel==MENU_MODE1){
                if(track_info.random){
                    g15r_drawLine (canvas, 43, 42, 158, 42,G15_COLOR_WHITE);
                    canvas->mode_xor=1;
                    g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 1);
                }else{
                    g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 0);
                }
                g15r_renderString (canvas, (unsigned char *)"Rndm", 0, G15_TEXT_SMALL, 108, 36);
                canvas->mode_xor=0;

                if(track_info.repeat){
                    canvas->mode_xor=1;
                    g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 1);
                }else {
                    g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 0);
                }

                g15r_renderString (canvas, (unsigned char *)"Rpt", 0, G15_TEXT_SMALL, 136, 36);
                canvas->mode_xor=0;

                        // 2nd box from left - if you want it...
                if(playlist_mode==0){
                    g15r_pixelBox (canvas, 34, 34, 70 , 42, G15_COLOR_BLACK, 1, 0);
                    g15r_renderString (canvas, (unsigned char *)"Playlist", 0, G15_TEXT_SMALL, 36, 36);
                }else{
                    canvas->mode_xor=1;
                    g15r_pixelBox (canvas, 34, 34, 70 , 42, G15_COLOR_BLACK, 1, 1);
                    g15r_renderString (canvas, (unsigned char *)"Playlist", 0, G15_TEXT_SMALL, 36, 36);    
                    canvas->mode_xor=0;
                }
            }

            if(menulevel==MENU_MODE2){
                g15r_pixelBox (canvas, 104, 34, 125 , 42, G15_COLOR_BLACK, 1, 0);
                g15r_renderString (canvas, (unsigned char *)"Vol-", 0, G15_TEXT_SMALL, 108, 36);

                g15r_pixelBox (canvas, 130, 34, 149 , 42, G15_COLOR_BLACK, 1, 0);
                g15r_renderString (canvas, (unsigned char *)"Vol+", 0, G15_TEXT_SMALL, 132, 36);

	        // 2nd box from left - if you want it...
        	//g15r_pixelBox (canvas, 34, 34, 54 , 42, G15_COLOR_BLACK, 1, 0);
        	//g15r_renderString (canvas, (unsigned char *)"test", 0, G15_TEXT_SMALL, 36, 36);
            }

            if(voltimeout){
                g15r_drawBar (canvas,10, 22, 149, 30, G15_COLOR_BLACK, track_info.volume, 100, 1);
                canvas->mode_xor=1;
                g15r_renderString (canvas, (unsigned char *)"Volume", 0, G15_TEXT_LARGE, 59, 23);
                canvas->mode_xor=0;
            }
        }

        /* do a quicky checksum - only send frame if different */
        chksum=0;
        for(i=0;i<G15_BUFFER_LEN;i++){
            chksum+=canvas->buffer[i]*i;
        }
        pthread_mutex_lock(&daemon_mutex);
        if(last_chksum!=chksum) {
            while(g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN)<0 && !leaving) {
                perror("lost connection, tryng again\n");
                usleep(10000);
                /* connection error occurred - try to reconnect to the daemon */
                g15screen_fd=new_g15_screen(G15_G15RBUF);
            }
        }
        pthread_mutex_unlock(&daemon_mutex);
        last_chksum=chksum;

        usleep(75*1000);
        if(playlist_mode)
            usleep(75*1000);

    }
    return NULL;
}

void status_changed(MpdObj *mi, ChangedStatusType what)
{
    pthread_mutex_lock(&lockit);

    mpd_Song *song = mpd_playlist_get_current_song(mi);
    if(song) {
        if(song->artist!=NULL)
            strncpy(track_info.artist,song->artist,99);
        if(song->title!=NULL)
            strncpy(track_info.title,song->title,99);
    }

    if(what&MPD_CST_CROSSFADE){
        // printf(GREEN"X-Fade:"RESET" %i sec.\n",mpd_status_get_crossfade(mi));
    }
    
    if(what&MPD_CST_PLAYLIST)
    {
        // printf(GREEN"Playlist changed"RESET"\n");
        track_info.totalsongs_in_playlist = mpd_playlist_get_playlist_length(mi);
        
    }

    if(what&MPD_CST_ELAPSED_TIME && !voltimeout){
        track_info.elapsed = mpd_status_get_elapsed_song_time(mi);
        track_info.total = mpd_status_get_total_song_time(mi);
    }

    if(what&MPD_CST_VOLUME){
        voltimeout=500;
        track_info.volume = mpd_status_get_volume(mi);
    }
    
    if(what&MPD_CST_STATE) {
        track_info.playstate = mpd_player_get_state(mi);
    }
    
    track_info.repeat = mpd_player_get_repeat(obj);
    track_info.random = mpd_player_get_random(obj);
    
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
    pthread_t g15display;
    int xtest_major_version = 0;
    int xtest_minor_version = 0;
    int dummy;

    int iport = 6600;
    char *hostname = getenv("MPD_HOST");
    char *port = getenv("MPD_PORT");
    char *password = getenv("MPD_PASSWORD");
    
    pthread_mutex_init(&lockit,NULL);
    pthread_mutex_init(&daemon_mutex,NULL);

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

    have_xtest = XTestQueryExtension(dpy, &dummy, &dummy, &xtest_major_version, &xtest_minor_version);
    if(have_xtest == False || xtest_major_version < 2 || (xtest_major_version <= 2 && xtest_minor_version < 2))
    {
        printf("XTEST extension not supported");
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

        pthread_create(&Xkeys, &attr, xevent_thread, NULL);
        pthread_create(&Lkeys, &attr, Lkeys_thread, NULL);
        pthread_create(&g15display, &attr, g15display_thread, NULL);

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
    pthread_join(Lkeys,NULL);
    pthread_join(g15display,NULL);
    XUngrabKey(dpy, XF86XK_AudioPrev, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioNext, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioPlay, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioStop, AnyModifier, root_win);
    if(own_keyboard)
        XUngrabKeyboard(dpy,CurrentTime);
    pthread_mutex_destroy(&lockit);
    
    return 1;
}
