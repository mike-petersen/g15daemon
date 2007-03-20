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
  
  This daemon listens on localhost port 15550 for client connections,
  and arbitrates LCD display.  Allows for multiple simultaneous clients.
  Client screens can be cycled through by pressing the 'L1' key.
  
  simple analyser xmms plugin for g15daemon
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <g15daemon_client.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <glib.h>
#include <xmms/plugin.h>
#include <xmms/util.h>
#include <xmms/xmmsctrl.h>
#include <xmms/configfile.h>
#include <libg15.h>
#include <libg15render.h>
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>

#define WIDTH 256  

/* BEGIN CONFIG VARIABLES */

/* Linearity of the amplitude scale (0.5 for linear, keep in [0.1, 0.9]) */
static float linearity=0.37;

/* Time factor of the band dinamics. 3 means that the coefficient of the
   last value is half of the current one's. (see source) */
static float tau=4.5;

/* Factor used for the diffusion. 4 means that half of the height is
   added to the neighbouring bars */
static float dif=3;    

/* limit (in px) of max length of bars avoid overlap */
static unsigned int limit=28;  

/*must be a divisor of 256! allowed: 1 (useless) 2 4 8 16 32 64 128(no space between bars) */   
static unsigned int num_bars=32;

/* Variable to disable keybindings Default: Disabled */
static unsigned int enable_keybindings=FALSE;

/* Visualition  type */
static unsigned int vis_type=0;

/* Peak visualition enable */
static unsigned int enable_peak=TRUE;

/* Detached peak from bars */
static unsigned int detached_peak=TRUE;

/* END CONFIG VARIABLES */

static gint16 bar_heights[WIDTH];
static gint16 bar_heights_peak[WIDTH];
static gint16 scope_data[G15_LCD_WIDTH];
static gdouble scale, x00, y00;

static void g15analyser_init(void);
static void g15analyser_cleanup(void);
static void g15analyser_playback_start(void);
static void g15analyser_playback_stop(void);
static void g15analyser_render_pcm(gint16 data[2][512]);
static void g15analyser_render_freq(gint16 data[2][256]);

g15canvas *canvas;

static unsigned int playing=0, paused=0;
static int g15screen_fd = -1;

pthread_mutex_t g15buf_mutex;

static Display *dpy;
static Window root_win;

static int mmedia_timeout_handle;
static int g15keys_timeout_handle;
static int g15disp_timeout_handle;

static int lastvolume;
static int volume;

VisPlugin g15analyser_vp = {
  NULL,
  NULL,
  0,
  "G15daemon Spectrum Analyzer 0.4",
  1,
  1,
  g15analyser_init, /* init */
  g15analyser_cleanup, /* cleanup */
  NULL, /* TODO about */
  NULL, /* TODO configure */
  NULL, /* disable_plugin */
  g15analyser_playback_start, /* playback_start */
  g15analyser_playback_stop, /* playback_stop */
  g15analyser_render_pcm, /* render_pcm */
  g15analyser_render_freq  /* render_freq */
};



VisPlugin *get_vplugin_info(void) {
  return &g15analyser_vp;
}

void g15spectrum_read_config(void)
{
  ConfigFile *cfg;
  gchar *filename;
  
  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  cfg = xmms_cfg_open_file(filename);
  
  if (cfg)
    {
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "visualisation_type", (int*)&vis_type);
      xmms_cfg_read_float(cfg, "G15Daemon Spectrum", "linearity", (float*)&linearity);
      xmms_cfg_read_float(cfg, "G15Daemon Spectrum", "tau", (float*)&tau);
      xmms_cfg_read_float(cfg, "G15Daemon Spectrum", "dif", (float*)&dif);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "bars_limit", (int*)&limit);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "num_bars", (int*)&num_bars);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "enable_peak", (int*)&enable_peak);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "detached_peak", (int*)&detached_peak);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "enable_keybindings", (int*)&enable_keybindings);
      
      xmms_cfg_free(cfg);
    }
  g_free(filename);
}

void g15spectrum_write_config(void)
{
  ConfigFile *cfg;
  gchar *filename;
  
  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  cfg = xmms_cfg_open_file(filename);
  
  if (cfg)
    {
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "visualisation_type", vis_type);
      xmms_cfg_write_float(cfg, "G15Daemon Spectrum", "linearity", linearity);
      xmms_cfg_write_float(cfg, "G15Daemon Spectrum", "tau", tau);
      xmms_cfg_write_float(cfg, "G15Daemon Spectrum", "dif", dif);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "bars_limit", limit);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "num_bars", num_bars);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "enable_peak", enable_peak);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "detached_peak", detached_peak);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "enable_keybindings", enable_keybindings);
      xmms_cfg_write_file(cfg, filename);
      xmms_cfg_free(cfg);
    }
  g_free(filename);
}
static int poll_g15keys() {
  int keystate = 0;
  struct pollfd fds;
  
  fds.fd = g15screen_fd;
  fds.events = POLLIN;
  
  if ((poll(&fds, 1, 5)) > 0);
  read (g15screen_fd, &keystate, sizeof (keystate));
  
  if (keystate) {
    pthread_mutex_lock (&g15buf_mutex);
    switch (keystate)
      {
      case G15_KEY_L1:
	vis_type = 1 - vis_type;
	g15spectrum_write_config(); // save as default next time
	break;
      default:
	break;
      }
    keystate = 0;
    pthread_mutex_unlock (&g15buf_mutex);
  }
  return TRUE;
}

static int poll_mmediakeys()
{
  long mask = KeyPressMask;
  XEvent event;
  
  while (XCheckMaskEvent(dpy, mask, &event)){
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioPlay)) {
      if(playing) {
	if (paused)  {
	  xmms_remote_play(g15analyser_vp.xmms_session);
	  paused = 0;
	} else {
	  xmms_remote_pause(g15analyser_vp.xmms_session);
	  paused = 1;
	}
      } else
	xmms_remote_play(g15analyser_vp.xmms_session);
    }
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioStop))
      xmms_remote_stop(g15analyser_vp.xmms_session);
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioLowerVolume)){
      volume = xmms_remote_get_main_volume(g15analyser_vp.xmms_session);
      if(volume<1)
	volume=1;
      xmms_remote_set_main_volume(g15analyser_vp.xmms_session, --volume);
    }
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioRaiseVolume)){
      volume = xmms_remote_get_main_volume(g15analyser_vp.xmms_session);
      if(volume>99)
	volume=99;
      xmms_remote_set_main_volume(g15analyser_vp.xmms_session, ++volume);
    }
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioMute)){
      if(xmms_remote_get_main_volume(g15analyser_vp.xmms_session)!=0){
	lastvolume = xmms_remote_get_main_volume(g15analyser_vp.xmms_session);
	volume = 0;
      }
      else {
	volume = lastvolume;
      }
      
      xmms_remote_set_main_volume(g15analyser_vp.xmms_session, volume);
    }
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioNext))
      if (playing)
	xmms_remote_playlist_next(g15analyser_vp.xmms_session);
    
    if(event.xkey.keycode==XKeysymToKeycode(dpy, XF86XK_AudioPrev))
      if (playing)
	xmms_remote_playlist_prev(g15analyser_vp.xmms_session);
    
  }
  return TRUE;
}

static int g15send_func() {
  int i;
  int playlist_pos;
  char *title;
  char *artist;
  char *song;
  char *strtok_ptr;
  static int vol_timeout=0;
  long chksum=0;
  static long last_chksum;
  
  pthread_mutex_lock (&g15buf_mutex);
  g15r_clearScreen (canvas, G15_COLOR_WHITE);
  
  if (xmms_remote_get_playlist_length(g15analyser_vp.xmms_session) > 0)
    {
      playlist_pos = xmms_remote_get_playlist_pos(g15analyser_vp.xmms_session);
      
      title = xmms_remote_get_playlist_title(g15analyser_vp.xmms_session, playlist_pos);
      if(title!=NULL){ //amarok doesnt support providing title info via xmms interface :(
	if(strlen(title)>32) {
	  artist = strtok_r(title,"-",&strtok_ptr);
	  song = strtok_r(NULL,"-",&strtok_ptr);
	  if(strlen(song)>32)
	    song[32]='\0';
	  g15r_renderString (canvas, (unsigned char *)song+1, 0, G15_TEXT_MED, 165-(strlen(song)*5), 0);
	  if(strlen(artist)>32)
	    artist[32]='\0';
	  if(artist[strlen(artist)-1]==' ')
	    artist[strlen(artist)-1]='\0';
	  g15r_renderString (canvas, (unsigned char *)artist, 0, G15_TEXT_MED, 160-(strlen(artist)*5), 8);
	} else
	  g15r_renderString (canvas, (unsigned char *)title, 0, G15_TEXT_MED, 160-(strlen(title)*5), 0);
      }
      g15r_drawBar (canvas, 0, 39, 159, 41, G15_COLOR_BLACK, xmms_remote_get_output_time(g15analyser_vp.xmms_session)/1000, xmms_remote_get_playlist_time(g15analyser_vp.xmms_session,playlist_pos)/1000, 1);
      
      if (playing)
	{
	  if (vis_type == 0)
	    {
	      int bar_width =  G15_LCD_WIDTH / num_bars;
	      
	      for(i = 0; i < G15_LCD_WIDTH; i+=bar_width){
		int y1 = bar_heights[i];
		if (enable_peak){                       /* if enable peak*/
		  if(y1>=bar_heights_peak[i]) {          /* check for new peak */
		    bar_heights_peak[i] = y1;
		  } else {
		    bar_heights_peak[i]--;              /* decrement old peak */
		  }
		}
		if (y1 > limit)
		  y1 = limit;
		y1 =  G15_LCD_HEIGHT - 7 - y1;
		g15r_pixelBox (canvas, i, y1 , i + bar_width - 2, 36, G15_COLOR_BLACK, 1, 1);
		
		if (enable_peak){                        /* if enable peak*/
		  int peak1, peak2;
		  if (bar_heights_peak[i] < limit){      /* superior limit */
		    peak1 = G15_LCD_HEIGHT - 8 - bar_heights_peak[i] - detached_peak;
		    peak2 = peak1;
		    if ( peak2 < 34 )                    /* inferior limit */
		      g15r_pixelBox (canvas, i, peak1  , i + bar_width - 2, peak2, G15_COLOR_BLACK, 1, 1);
		  }
		}
	      }
	    }
	  else
	    {
	      int y1, y2=(25 - scope_data[0]);
	      for (i = 0; i < 160; i++)
		{
		  y1 = y2;
		  y2 = (25 - scope_data[i]);
		  g15r_drawLine (canvas, i, y1, i + 1, y2, G15_COLOR_BLACK);
		}
	    }
	}
      else 
	g15r_renderString (canvas, (unsigned char *)"Playback Stopped", 0, G15_TEXT_LARGE, 16, 16);
      
    }
  else
    g15r_renderString (canvas, (unsigned char *)"Playlist Empty", 0, G15_TEXT_LARGE, 24, 16);
  
  if(lastvolume!=xmms_remote_get_main_volume(g15analyser_vp.xmms_session) || vol_timeout>=0) {
    if(lastvolume!=xmms_remote_get_main_volume(g15analyser_vp.xmms_session))
      vol_timeout=10;
    else
      vol_timeout--;
    
    lastvolume = xmms_remote_get_main_volume(g15analyser_vp.xmms_session);
    if (lastvolume >= 0)	
      g15r_drawBar (canvas, 10, 15, 149, 28, G15_COLOR_BLACK, lastvolume, 100, 1);
    canvas->mode_xor=1;
    g15r_renderString (canvas, (unsigned char *)"Volume", 0, G15_TEXT_LARGE, 59, 18);
    canvas->mode_xor=0;
  }
  /* do a quicky checksum - only send frame if different */
  for(i=0;i<G15_BUFFER_LEN;i++){
    chksum+=canvas->buffer[i]*i;
  }
  if(last_chksum!=chksum) {
    if(g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN)<0) {
      perror("lost connection, tryng again\n");
      /* connection error occurred - try to reconnect to the daemon */
      g15screen_fd=new_g15_screen(G15_G15RBUF);
    }
  }
  last_chksum=chksum;
  
  pthread_mutex_unlock(&g15buf_mutex);
  return TRUE;
}

int myx_error_handler(Display *dpy, XErrorEvent *err){
  
  printf("error (%i) occured - ignoring\n",err->error_code);
  return 0;
}


static void g15analyser_init(void) {
  
  pthread_mutex_init(&g15buf_mutex, NULL);        
  pthread_mutex_lock(&g15buf_mutex);
  if (enable_keybindings) {
    dpy = XOpenDisplay(getenv("DISPLAY"));
    if (!dpy) {
      printf("Can't open display\n");
      return;
    }
    root_win = DefaultRootWindow(dpy);
    if (!root_win) {
      printf("Cant find root window\n");
      return;
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
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioLowerVolume),Mod2Mask , root_win,
	     False, GrabModeAsync, GrabModeAsync);                                     
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioRaiseVolume), Mod2Mask, root_win,
	     False, GrabModeAsync, GrabModeAsync);                                     
    XGrabKey(dpy,XKeysymToKeycode(dpy, XF86XK_AudioMute), Mod2Mask, root_win,
	     False, GrabModeAsync, GrabModeAsync);                                     
  }
  
  if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
    return;
  }
  canvas = (g15canvas *) malloc (sizeof (g15canvas));
  if (canvas != NULL)
    {
      memset(canvas->buffer, 0, G15_BUFFER_LEN);
      canvas->mode_cache = 0;
      canvas->mode_reverse = 0;
      canvas->mode_xor = 0;
    }
  g15spectrum_read_config();
  
  scale = G15_HEIGHT / ( log((1 - linearity) / linearity) *2 );
  x00 = linearity*linearity*32768.0/(2 * linearity - 1);
  y00 = -log(-x00) * scale;
  
  pthread_mutex_unlock(&g15buf_mutex);
  /* increase lcd drive voltage/contrast for this client */
  g15_send_cmd(g15screen_fd, G15DAEMON_CONTRAST,2);
  
  pthread_attr_t attr;
  memset(&attr,0,sizeof(pthread_attr_t));
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  if (enable_keybindings){
    mmedia_timeout_handle = g_timeout_add(100, poll_mmediakeys, NULL);
    g15keys_timeout_handle = g_timeout_add(100, poll_g15keys, NULL);
  }
  g15disp_timeout_handle = g_timeout_add(75, g15send_func, NULL);
  
}

static void g15analyser_cleanup(void) {
  
  pthread_mutex_lock (&g15buf_mutex);
  if (canvas != NULL)
    free(canvas);
  if(g15screen_fd)
    close(g15screen_fd);
  pthread_mutex_unlock (&g15buf_mutex);
  if (enable_keybindings){
    XUngrabKey(dpy, XF86XK_AudioPrev, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioNext, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioPlay, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioStop, AnyModifier, root_win);
    XUngrabKey(dpy, XF86XK_AudioLowerVolume, Mod2Mask, root_win);
    XUngrabKey(dpy, XF86XK_AudioRaiseVolume, Mod2Mask, root_win);
    XUngrabKey(dpy, XF86XK_AudioMute, AnyModifier, root_win);
    
    gtk_timeout_remove(mmedia_timeout_handle);
    gtk_timeout_remove(g15keys_timeout_handle);
  }
  gtk_timeout_remove(g15disp_timeout_handle);
  if (enable_keybindings)
    XCloseDisplay(dpy);
  g15spectrum_write_config(); // save as default next time
  
  return;
}

static void g15analyser_playback_start(void) {
  
  pthread_mutex_lock (&g15buf_mutex);
  playing = 1;
  paused = 0;
  pthread_mutex_unlock (&g15buf_mutex);
  return;
  
}

static void g15analyser_playback_stop(void) {
  
  pthread_mutex_lock (&g15buf_mutex);
  playing = 0;
  paused = 0;
  pthread_mutex_unlock (&g15buf_mutex);
  return;
  
}

static void g15analyser_render_pcm(gint16 data[2][512]) {
  pthread_mutex_lock (&g15buf_mutex);
  
  if (playing)
    {
      gint i;
      gint max;
      gint scale = 128;
      
      do
	{
	  max = 0;
	  for (i = 0; i < G15_LCD_WIDTH; i++)
	    {
	      scope_data[i] = data[0][i] / scale;
	      if (abs(scope_data[i]) > abs(max))
		max = scope_data[i];
	    }
	  scale += 128;
	} while (abs(max) > 10);
    }
  
  pthread_mutex_unlock (&g15buf_mutex);
}

static void g15analyser_render_freq(gint16 data[2][256]) {
  
  pthread_mutex_lock(&g15buf_mutex);
  
  if (playing)
    {
      gint i;
      gdouble y;
      /* code from version 0.1 */
      for (i = 0; i < WIDTH; i++) {
	y = (gdouble)data[0][i] * (i + 1); /* Compensating the energy */
	y = ( log(y - x00) * scale + y00 ); /* Logarithmic amplitude */
	
	y = ( (dif-2)*y + /* FIXME: conditionals should be rolled out of the loop */
	      (i==0       ? y : bar_heights[i-1]) +
	      (i==WIDTH-1 ? y : bar_heights[i+1])) / dif; /* Add some diffusion */
	y = ((tau-1)*bar_heights[i] + y) / tau; /* Add some dynamics */
	bar_heights[i] = (gint16)y;
      }
      
    }
  
  pthread_mutex_unlock(&g15buf_mutex);
  
  return;
  
}

