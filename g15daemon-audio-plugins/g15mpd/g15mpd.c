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

        (c) 2006-2007 Mike Lampard 

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

#include <linux/input.h>

extern int debug_level;
int g15screen_fd, retval;
g15canvas *canvas;
int current_fg_check=0;
static int playing;
static int paused=0;
static int quickscroll = 0;

MpdObj *obj = NULL;

int leaving = 0;
static int voltimeout=0;
static int own_keyboard=0;

pthread_mutex_t daemon_mutex;
pthread_mutex_t lockit;

int mmedia_fd; 

static int menulevel=0;
#define MENU_MODE1 0
#define MENU_MODE2 1
#define MAX_MENU_MODES 2
/* playlist mode takes over all keys on the keyboard - allowing searches/playlist scroll via volume ctrl etc. 
all non-valid keys are sent elsewhere via the xtest extension */	
static int playlist_mode=0;
int playlist_selection=0;
int item_selected=0;
int volume_adjust=0;
int mute=0;
int muted_volume=0;

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

    strncpy(ver,G15DAEMON_VERSION,3);
    float g15v;
    sscanf(ver,"%f",&g15v);

    fds.fd = g15screen_fd;
    fds.events = POLLIN;

    while(!leaving){
        int foo=0;
        current_fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo);
        static int last_fg_check = 0;
        if(playlist_mode && last_fg_check != current_fg_check){
            if(own_keyboard){
                if(current_fg_check==0){
                    own_keyboard=0;
                }
            }else if(current_fg_check && !own_keyboard) {
                own_keyboard=1;
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
                    if(!own_keyboard){
                        own_keyboard=playlist_mode=1;
                        mpd_Song *song = mpd_playlist_get_current_song(obj);
                        if(song)
                            if(song->pos)
                                track_info.currentsong=song->pos;
                    }else{ //de-activate
                        own_keyboard=playlist_mode=0;
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


void *event_key_thread() {

    struct input_event *event;
    event=malloc(sizeof(struct input_event));
    struct pollfd fds;

    int retval;    
    while(!leaving) {

        event->value=0;
        event->code=0;
        fds.fd=mmedia_fd;
        fds.events=POLLIN;

        if(poll(&fds,1,500)<1)
            continue;

        retval=read(mmedia_fd,event,sizeof(struct input_event));

        if(event->value==0||current_fg_check==0) 
            continue;

        int keycode = event->code;
        int code_audio_play = KEY_PLAYPAUSE;
        int code_audio_stop = KEY_STOPCD;
        int code_audio_next = KEY_NEXTSONG;
        int code_audio_prev = KEY_PREVIOUSSONG;
        int code_audio_raisevol = KEY_VOLUMEUP;
        int code_audio_lowervol = KEY_VOLUMEDOWN;
        int code_audio_mute = KEY_MUTE;

        /*     printf("keycode = %d\n", keycode); */

        if(keycode == code_audio_play) {
            if(playing && !playlist_mode) {
                if (paused==1)  {
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
        }

        if(keycode == code_audio_stop) {
            mpd_player_stop(obj);
            playing = 0;
            continue;
        }

        if(keycode == code_audio_next) {
          if(playlist_mode)
            playlist_selection+=1;
          else
            mpd_player_next(obj);
          continue;
        }

        if(keycode == code_audio_prev) {
          if(playlist_mode)
            playlist_selection-=1;
          else
            mpd_player_prev(obj);
          continue;
        }

        if(keycode == code_audio_raisevol){
          if(playlist_mode && quickscroll)
            playlist_selection+=1;
          else {
            pthread_mutex_lock(&daemon_mutex);
            volume_adjust+=1;
            pthread_mutex_unlock(&daemon_mutex);
            continue;
          }
        }
        if(keycode == code_audio_lowervol){
          if(playlist_mode && quickscroll)
            playlist_selection-=1;
          else {
            pthread_mutex_lock(&daemon_mutex);
            volume_adjust-=1;
            pthread_mutex_unlock(&daemon_mutex);
            continue;
          }
        }

        if(keycode == code_audio_mute){
            pthread_mutex_lock(&daemon_mutex);
            mute = 1;
            pthread_mutex_unlock(&daemon_mutex);
            continue;
        }

        /* now the default stuff */
        if(own_keyboard) {
            menulevel=MENU_MODE1;
            continue;
        }
    }
    free(event);
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
            if (track_info.total != 0) {
                snprintf((char*)time_elapsed,40,"%02i:%02i",track_info.elapsed/60, track_info.elapsed%60);
                snprintf((char*)time_total,40,"%02i:%02i",track_info.total/60, track_info.total%60);
            }
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

            if(muted_volume != 0){
                g15r_renderString (canvas, (unsigned char *)"MUTE", 0, G15_TEXT_LARGE, 11, 2);
            }
            else if(voltimeout){
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
        if (song->artist!=NULL) {
            strncpy(track_info.artist,song->artist,99);
        } else {
            track_info.artist[0] = 0;
        }
        if (song->title!=NULL) {
            strncpy(track_info.title,song->title,99);
        } else {
            track_info.title[0] = 0;
        }
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
    usleep(10*1000);
}



int main(int argc, char **argv)
{
    int fdstdin = 0;
    pthread_t Lkeys;
    pthread_t g15display;
    pthread_t EKeys;
    int volume;
    int volume_new;
    char devname[256] = "Unknown";
    int iport = 6600;
    char *hostname = getenv("MPD_HOST");
    char *port = getenv("MPD_PORT");
    char *password = getenv("MPD_PASSWORD");
    int eventdev;
    char evdev_name[128];
    pthread_mutex_init(&lockit,NULL);
    pthread_mutex_init(&daemon_mutex,NULL);
    int i;
      
    for (i=0;i<argc;i++) {
    char argument[20];
    memset(argument,0,20);
    strncpy(argument,argv[i],19);
    if (!strncmp(argument, "-q",2) || !strncmp(argument, "--quickscroll",13)) {
        quickscroll=1;
    }
    if (!strncmp(argument, "-h",2) || !strncmp(argument, "--help",6)) {
        printf("  %s version %s\n  (c)2006-2007 Mike Lampard\n\n",argv[0],VERSION);
        printf("%s -q or --quickscroll	Use volume control to scroll through the playlist\n",argv[0]);
        printf("%s -v or --version	Show program version\n",argv[0]);
        printf("%s -h or --help		This help text\n\n",argv[0]);
        exit(0);
    }
    if (!strncmp(argument, "-v",2) || !strncmp(argument, "--version",9)) {
      printf("%s version %s\n",argv[0],VERSION);
      exit(0);     
    }    
  }

    
    for(eventdev=0;eventdev<127;eventdev++) {
        snprintf(evdev_name,127,"/dev/input/event%i",eventdev);
        if ((mmedia_fd = open(evdev_name, O_NONBLOCK|O_RDONLY)) < 0) {
            printf("error opening interface %i",eventdev);
        }
        ioctl(mmedia_fd, EVIOCGNAME(sizeof(devname)), devname);
        if(0==strncmp(devname,"Logitech Logitech Gaming Keyboard",256)){
            printf("Found device: \"%s\" on %s ", devname,evdev_name);
            break;
        }else
            close(mmedia_fd);
    }
    if (mmedia_fd) { // we assume that the next event device is the multimedia keys
        close(mmedia_fd);
        snprintf(evdev_name,127,"/dev/input/event%i",++eventdev);
        printf("and %s\n",evdev_name);
        if ((mmedia_fd = open(evdev_name, O_NONBLOCK|O_RDONLY)) < 0) {
            printf("error opening interface %i",eventdev);
        }
    }else {
        printf("Unable to find Keyboard via EVENT interface... is /dev/input/event[0-9] readable??\n");
    }

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

        pthread_create(&Lkeys, &attr, Lkeys_thread, NULL);
        pthread_create(&EKeys, &attr,event_key_thread, NULL);
        pthread_create(&g15display, &attr, g15display_thread, NULL);

        do{
            pthread_mutex_lock(&daemon_mutex);
            if(mute){
                volume_adjust = 0;
                mute = 0;
                if (muted_volume == 0) {
                    //printf("mute\n");
                    muted_volume = mpd_status_get_volume(obj);
                    mpd_status_set_volume (obj,0);
                } else {
                    //printf("unmute\n");
                    if (mpd_status_get_volume(obj) == 0) { /* if no other client has set volume up */
                        mpd_status_set_volume (obj,muted_volume);
                    }
                    muted_volume = 0;
                }
            }
            if(volume_adjust != 0){
                if (muted_volume != 0) {
                    volume=muted_volume;
                } else {
                    volume=mpd_status_get_volume(obj);
                }
                volume_new = volume + volume_adjust;
                volume_adjust = 0;
                if(volume_new < 0)
                    volume_new = 0;
                if(volume_new > 100)
                    volume_new = 100;
                if(volume != volume_new || muted_volume){
                    //printf("volume %d -> %d\n", volume, volume_new);
                    mpd_status_set_volume (obj,volume_new);
                }
                voltimeout=500;
                muted_volume=0;
            }
            mpd_status_update(obj);
            pthread_mutex_unlock(&daemon_mutex);

        }while(!usleep(5000) &&  !leaving);
    }
    mpd_free(obj);
    close(fdstdin);

    if(canvas!=NULL)
        free(canvas);

    close(g15screen_fd);
    close(mmedia_fd);
    leaving = 1;
    pthread_join(Lkeys,NULL);
    pthread_join(g15display,NULL);

    pthread_mutex_destroy(&lockit);

    return 1;
}
