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
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <glib.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>

#include <xmms/plugin.h>
#include <xmms/util.h>
#include <xmms/xmmsctrl.h>
#include <xmms/configfile.h>

#include <libg15.h>
#include <libg15render.h>
#include <g15daemon_client.h>

#include <X11/Xlib.h>
#include <X11/XF86keysym.h>


/* Some useful costants */
#define WIDTH 256
#define PLUGIN_VERSION "2.5.0"
#define PLUGIN_NAME    "G15daemon Visualization Plugin"
#define INFERIOR_SPACE 7 /* space between bars and position bar */
#define SUPERIOR_SPACE 8 /* space between bars and top of lcd   */

/* Time factor of the band dinamics. 3 means that the coefficient of the
   last value is half of the current one's. (see source) */
#define tau 4.5



/* BEGIN CONFIG VARIABLES */

/* Linearity of the amplitude scale (0.5 for linear, keep in [0.1, 0.9]) */
static float linearity=0.33;

/* Amplification of the scale. Cannot be negative. 0,0.5 is a reasonable range */
static int amplification=0;

/* Factor used for the diffusion. 4 means that half of the height is
   added to the neighbouring bars */
static float dif=3;    

/* limit (in px) of max length of bars avoid overlap */
static unsigned int limit = G15_LCD_HEIGHT - INFERIOR_SPACE - SUPERIOR_SPACE;  

/* Number of Bars - Must be a divisor of 256! allowed: 1 (useless) 2 4 8 16 32 64 128(no space between bars) */   
static unsigned int num_bars=32;

/* Variable to disable keybindings Default: Disabled */
static unsigned int enable_keybindings=FALSE;

/* Visualization  type */
static unsigned int vis_type=0;

/* Peak visualization enable */
static unsigned int enable_peak=TRUE;

/* Detached peak from bars */
static unsigned int detached_peak=TRUE;

/* Enable Analog Mode */
static unsigned int analog_mode=FALSE;

/* Step for leds in Analog Mode. Min value:  2 */
static unsigned int analog_step=2;

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
static void g15analyser_conf(void);
static void g15analyser_about(void);

static void g15analyser_conf_ok(GtkWidget *w, gpointer data);
static void g15analyser_conf_apply(void);
static void g15analyser_conf_cancel(void);

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

/* gdk stuff */
static GtkWidget *configure_win = NULL;
static GtkWidget *vbox;
static GtkWidget *bbox, *ok, *cancel, *apply;
static GtkWidget *t_options_bars_radio, *t_options_scope_radio;
static GtkWidget *t_options_effect_no, *t_options_effect_peak, *t_options_effect_analog;
static GtkWidget *t_options_vistype;
static GtkWidget *t_options_bars, *t_options_bars_effects;
static GtkWidget *g_options_frame ,*g_options_enable_keybindings, *g_options_enable_dpeak;
static GtkWidget *g_options_frame_bars;
static GtkWidget *g_options_frame_bars_effects;

static int tmp_bars=-1, tmp_step=-1, tmp_ampli=-1000; 
static float tmp_lin=-1;


VisPlugin g15analyser_vp = {
  NULL,
  NULL,
  0,
  PLUGIN_NAME " " PLUGIN_VERSION,
  1,
  1,
  g15analyser_init,           /* init           */
  g15analyser_cleanup,        /* cleanup        */
  g15analyser_about,          /* about          */
  g15analyser_conf,           /* configure      */
  NULL,                       /* disable_plugin */
  g15analyser_playback_start, /* playback_start */
  g15analyser_playback_stop,  /* playback_stop  */
  g15analyser_render_pcm,     /* render_pcm     */
  g15analyser_render_freq     /* render_freq    */
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
  pthread_mutex_lock (&g15buf_mutex);
  if (cfg)
    {
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "visualisation_type", (int*)&vis_type);
      xmms_cfg_read_float(cfg, "G15Daemon Spectrum", "linearity", (float*)&linearity);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "amplification", (int*)&amplification);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "bars_limit", (int*)&limit);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "num_bars", (int*)&num_bars);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "enable_peak", (int*)&enable_peak);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "detached_peak", (int*)&detached_peak);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "analog_mode", (int*)&analog_mode);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "analog_step", (int*)&analog_step);
      xmms_cfg_read_int(cfg, "G15Daemon Spectrum", "enable_keybindings", (int*)&enable_keybindings);
      
      xmms_cfg_free(cfg);
      
    }
  pthread_mutex_unlock (&g15buf_mutex);
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
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "amplification", amplification);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "bars_limit", limit);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "num_bars", num_bars);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "enable_peak", enable_peak);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "detached_peak", detached_peak);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "analog_mode", analog_mode);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "analog_step", analog_step);
      xmms_cfg_write_int(cfg, "G15Daemon Spectrum", "enable_keybindings", enable_keybindings);
      xmms_cfg_write_file(cfg, filename);
      xmms_cfg_free(cfg);
    }
  g_free(filename);
}

void g15analyser_conf_apply(void){
  pthread_mutex_lock (&g15buf_mutex);
  if (GTK_TOGGLE_BUTTON(t_options_bars_radio)->active)
    vis_type = 0;
  else
    vis_type = 1;
  if ( tmp_lin != -1 )
    linearity = tmp_lin;
  if ( tmp_ampli != -1000 )
    amplification = tmp_ampli;
  if ( tmp_bars != -1 )
    num_bars =  pow(2,tmp_bars);
  if (tmp_step != -1 )
    analog_step = tmp_step;
  if (GTK_TOGGLE_BUTTON(t_options_effect_no)->active){
    enable_peak = 0;
    analog_mode = 0;
  } else {
    enable_peak = GTK_TOGGLE_BUTTON(t_options_effect_peak)->active;
    analog_mode = GTK_TOGGLE_BUTTON(t_options_effect_analog)->active;
  }
  detached_peak = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_options_enable_dpeak));
  enable_keybindings =  gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_options_enable_keybindings));
  pthread_mutex_unlock (&g15buf_mutex);
  return;
}


static void g15analyzer_adj_changed(GtkWidget *w, int *a)
{
  *a=(int) GTK_ADJUSTMENT(w)->value;
}

static void g15analyzer_adj_changed_float(GtkWidget *w, float *a)
{
  *a=(float) GTK_ADJUSTMENT(w)->value;
}

void g15analyser_conf_ok(GtkWidget *w, gpointer data){
  g15analyser_conf_apply();
  g15spectrum_write_config();
  gtk_widget_destroy(configure_win);
  return;
}

void g15analyser_conf_cancel(){
  g15spectrum_read_config();
  gtk_widget_destroy(configure_win);
  return;
}

void g15analyser_conf(void){
  GtkWidget *label,*label_bars, *label_lin, *label_ampli, *label_step;
  GtkWidget *scale_bars, *scale_lin, *scale_ampli, *scale_step;
  GtkObject *adj_bars, *adj_lin, *adj_ampli, *adj_step;
  int bar_value = (log(num_bars)/log(2));
  printf("%d",bar_value);
  if(configure_win)
    return;
  configure_win = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_container_set_border_width(GTK_CONTAINER(configure_win), 10);
  gtk_window_set_title(GTK_WINDOW(configure_win), PLUGIN_NAME " configuration");
  gtk_window_set_policy(GTK_WINDOW(configure_win), FALSE, FALSE, FALSE);
  gtk_window_set_position(GTK_WINDOW(configure_win), GTK_WIN_POS_MOUSE);
  gtk_signal_connect(GTK_OBJECT(configure_win), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &configure_win);
  
  vbox = gtk_vbox_new(FALSE, 5);
  
  /* general config */
  
  g_options_frame = gtk_frame_new("General:");
  gtk_container_set_border_width(GTK_CONTAINER(g_options_frame), 5);
  t_options_vistype = gtk_vbox_new(FALSE, 5);
  label = gtk_label_new("Visualizion Type:");
  gtk_misc_set_alignment(GTK_MISC (label), 0, 0);
  gtk_misc_set_padding(GTK_MISC (label),5,0);
  gtk_box_pack_start(GTK_BOX(t_options_vistype), label, FALSE, FALSE, 0);
  gtk_widget_show(label);
  t_options_bars_radio = gtk_radio_button_new_with_label(NULL, "Spectrum Bars");
  t_options_scope_radio = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(t_options_bars_radio)), "Scope");
  /* Bars radio */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t_options_bars_radio), vis_type == 0);
  gtk_box_pack_start(GTK_BOX(t_options_vistype), t_options_bars_radio, FALSE, FALSE, 0);
  gtk_widget_show(t_options_bars_radio);
  /* Spectrum radio */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t_options_scope_radio), vis_type == 1);
  gtk_box_pack_start(GTK_BOX(t_options_vistype), t_options_scope_radio, FALSE, FALSE, 0);
  gtk_widget_show(t_options_scope_radio);
  /* create keybindings button */
  g_options_enable_keybindings = gtk_check_button_new_with_label("Enable Keybindings (need to restart XMMS)");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_options_enable_keybindings), enable_keybindings);
  /* put check button in g_options_vbox */
  gtk_box_pack_start(GTK_BOX(t_options_vistype), g_options_enable_keybindings, FALSE, FALSE, 0);
  gtk_widget_show(g_options_enable_keybindings);
  gtk_container_add(GTK_CONTAINER(g_options_frame), t_options_vistype); 
  gtk_widget_show(t_options_vistype);
  /* draw frame */
  gtk_box_pack_start(GTK_BOX(vbox), g_options_frame, TRUE, TRUE, 0);
  gtk_widget_show(g_options_frame);
  
  
  /* bars config */
  
  g_options_frame_bars = gtk_frame_new("Spectrum bars options:");
  gtk_container_set_border_width(GTK_CONTAINER(g_options_frame), 5);
  t_options_bars = gtk_vbox_new(FALSE, 5);
  
  label_bars=gtk_label_new("Num Bars:");
  gtk_misc_set_alignment(GTK_MISC (label_bars), 0, 0);
  gtk_misc_set_padding(GTK_MISC (label_bars),5,0);
  gtk_box_pack_start(GTK_BOX(t_options_bars), label_bars, TRUE, TRUE, 4);
  gtk_widget_show(label_bars);
  adj_bars=gtk_adjustment_new(bar_value, 0, 7, 1, 1, 0);
  scale_bars=gtk_hscale_new(GTK_ADJUSTMENT(adj_bars));
  gtk_scale_set_draw_value(GTK_SCALE(scale_bars), FALSE);
  gtk_widget_show(scale_bars);
  gtk_box_pack_start(GTK_BOX(t_options_bars), scale_bars, TRUE, TRUE, 4);
  gtk_signal_connect(GTK_OBJECT(adj_bars), "value-changed", GTK_SIGNAL_FUNC(g15analyzer_adj_changed), &tmp_bars);
  
  label_lin=gtk_label_new("Amplification linearity:");
  gtk_misc_set_alignment(GTK_MISC (label_lin), 0, 5);
  gtk_misc_set_padding(GTK_MISC (label_lin),5,0);
  gtk_box_pack_start(GTK_BOX(t_options_bars), label_lin, TRUE, TRUE, 4);
  gtk_widget_show(label_lin);
  adj_lin=gtk_adjustment_new(linearity, 0.10 , 0.49 , 0.1, 0.1, 0);
  scale_lin=gtk_hscale_new(GTK_ADJUSTMENT(adj_lin));
  gtk_scale_set_draw_value(GTK_SCALE(scale_lin), FALSE);
  gtk_widget_show(scale_lin);
  gtk_box_pack_start(GTK_BOX(t_options_bars), scale_lin, TRUE, TRUE, 4);
  gtk_signal_connect(GTK_OBJECT(adj_lin), "value-changed", GTK_SIGNAL_FUNC(g15analyzer_adj_changed_float), &tmp_lin);
  
  label_ampli=gtk_label_new("Amplification:");
  gtk_misc_set_alignment(GTK_MISC (label_ampli), 0, 5);
  gtk_misc_set_padding(GTK_MISC (label_ampli),5,0);
  gtk_box_pack_start(GTK_BOX(t_options_bars), label_ampli, TRUE, TRUE, 4);
  gtk_widget_show(label_ampli);
  adj_ampli=gtk_adjustment_new(amplification, -30, 30, 1, 1, 0);
  scale_ampli=gtk_hscale_new(GTK_ADJUSTMENT(adj_ampli));
  gtk_scale_set_draw_value(GTK_SCALE(scale_ampli), FALSE);
  gtk_widget_show(scale_ampli);
  gtk_box_pack_start(GTK_BOX(t_options_bars), scale_ampli, TRUE, TRUE, 4);
  gtk_signal_connect(GTK_OBJECT(adj_ampli), "value-changed", GTK_SIGNAL_FUNC(g15analyzer_adj_changed), &tmp_ampli);
  
  /* draw frame */
  gtk_container_add(GTK_CONTAINER(g_options_frame_bars), t_options_bars); 
  gtk_widget_show(t_options_bars);
  gtk_box_pack_start(GTK_BOX(vbox), g_options_frame_bars, TRUE, TRUE, 0);
  gtk_widget_show(g_options_frame_bars);
  
  
  /* bars effects */
  g_options_frame_bars_effects = gtk_frame_new("Spectrum bars effects:");
  gtk_container_set_border_width(GTK_CONTAINER(g_options_frame_bars_effects), 5);
  t_options_bars_effects = gtk_vbox_new(FALSE, 5);
  /* radio effect type */
  t_options_effect_no = gtk_radio_button_new_with_label(NULL, "None");
  t_options_effect_peak = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(t_options_effect_no)), "Peaks");
  t_options_effect_analog = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(t_options_effect_peak)), "Analog");
  /* no effect radio */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t_options_effect_no), (enable_peak == FALSE && analog_mode == FALSE));
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), t_options_effect_no, FALSE, FALSE, 0);
  gtk_widget_show(t_options_effect_no);
  /* peak effect radio */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t_options_effect_peak), (enable_peak == TRUE && analog_mode == FALSE));
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), t_options_effect_peak, FALSE, FALSE, 0);
  gtk_widget_show(t_options_effect_peak);
  /* analog effect radio */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t_options_effect_analog), (enable_peak == FALSE && analog_mode == TRUE));
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), t_options_effect_analog, FALSE, FALSE, 0);
  gtk_widget_show(t_options_effect_analog);
  
  /* peak detached button */ 
  g_options_enable_dpeak = gtk_check_button_new_with_label("Detach peaks from bars");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_options_enable_dpeak), detached_peak);
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), g_options_enable_dpeak, FALSE, FALSE, 0);
  gtk_widget_show(g_options_enable_dpeak);
  
  /* analog step bar*/
  label_step=gtk_label_new("Analog mode step:");
  gtk_misc_set_alignment(GTK_MISC (label_step), 0, 5);
  gtk_misc_set_padding(GTK_MISC (label_step),5,0);
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), label_step, TRUE, TRUE, 4);
  gtk_widget_show(label_step);
  adj_step=gtk_adjustment_new(analog_step, 2, 9, 1, 1, 0);
  scale_step=gtk_hscale_new(GTK_ADJUSTMENT(adj_step));
  gtk_scale_set_draw_value(GTK_SCALE(scale_step), FALSE);
  gtk_widget_show(scale_step);
  gtk_box_pack_start(GTK_BOX(t_options_bars_effects), scale_step, TRUE, TRUE, 4);
  gtk_signal_connect(GTK_OBJECT(adj_step), "value-changed", GTK_SIGNAL_FUNC(g15analyzer_adj_changed), &tmp_step);
  
  /* draw frame */
  gtk_container_add(GTK_CONTAINER(g_options_frame_bars_effects), t_options_bars_effects); 
  gtk_widget_show(t_options_bars_effects);
  gtk_box_pack_start(GTK_BOX(vbox), g_options_frame_bars_effects, TRUE, TRUE, 0);
  gtk_widget_show(g_options_frame_bars_effects);
  
  
  /* buttons */
  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);   
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
  gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);
  
  ok = gtk_button_new_with_label(" Ok ");
  gtk_signal_connect(GTK_OBJECT(ok), "clicked", GTK_SIGNAL_FUNC(g15analyser_conf_ok), NULL);
  GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
  gtk_widget_show(ok);
  
  apply = gtk_button_new_with_label(" Apply ");
  gtk_signal_connect(GTK_OBJECT(apply), "clicked", GTK_SIGNAL_FUNC(g15analyser_conf_apply), NULL);
  GTK_WIDGET_SET_FLAGS(apply, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(bbox), apply, TRUE, TRUE, 0);
  gtk_widget_show(apply);
  
  cancel = gtk_button_new_with_label("Cancel");
  gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked", GTK_SIGNAL_FUNC(g15analyser_conf_cancel), GTK_OBJECT(configure_win));
  GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
  gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);
  
  gtk_container_add(GTK_CONTAINER(configure_win), vbox);
  /* Show all*/
  gtk_widget_show(cancel);
  gtk_widget_show(bbox);
  gtk_widget_show(vbox);
  gtk_widget_show(configure_win);
  return;
}

void g15analyser_about(void){
  GtkWidget *dialog, *button, *label;
  
  dialog = gtk_dialog_new();
  
  gtk_widget_set_usize (dialog, 400, 300);
  gtk_window_set_title(GTK_WINDOW(dialog), "about " PLUGIN_NAME);
  gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, FALSE);
  gtk_container_border_width(GTK_CONTAINER(dialog), 5);
  
  
  label = gtk_label_new (PLUGIN_NAME"\n\
v. " PLUGIN_VERSION "\n\
\n\
by Mike Lampard <mlampard@users.sf.net>\n\
   Anthony J. Mirabella <aneurysm9>\n\
   Antonio Bartolini <robynhub@users.sf.net>\n\
   and others...\n\
\n\
get the newest version from:\n\
http://g15daemon.sf.net/\n\
");
  
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label, FALSE, FALSE, 0);
  gtk_widget_show(label);
  
  button = gtk_button_new_with_label(" Ok ");
  gtk_signal_connect_object(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(dialog));
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), button, FALSE, FALSE, 0);
  gtk_widget_show(button);
  
  gtk_widget_show(dialog);
  gtk_widget_grab_focus(button);
  
  return;
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
  int i,j;
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
		/* if enable peak*/
		if (enable_peak && ! analog_mode){
		  /* check for new peak */
		  if(y1>=bar_heights_peak[i]) {          
		    bar_heights_peak[i] = y1;
		  } else {
		    /* decrement old peak */
		    bar_heights_peak[i]--;              
		  }
		}
		if (y1 > limit)
		  y1 = limit;
		y1 =  G15_LCD_HEIGHT - INFERIOR_SPACE - y1;
		
		/* if Analog Mode */
		if (analog_mode){
		  int end =  (y1 % analog_step == 0 ? y1 : y1 - (y1 % analog_step)); /* Approx to multiple */
		  for(j = G15_LCD_HEIGHT - SUPERIOR_SPACE ; j > end ; j-= analog_step){
		    g15r_pixelBox (canvas, i, j - analog_step + 2 ,i+bar_width-2, j , G15_COLOR_BLACK, 1, 1); 
		  }		  
		} else
		  g15r_pixelBox (canvas, i, y1 , i + bar_width - 2, 36, G15_COLOR_BLACK, 1, 1);
		
		/* if enable peak*/
		if (enable_peak && ! analog_mode){        
		  int peak1, peak2;
		  /* superior limit */
		  if (bar_heights_peak[i] < limit){     
		    peak1 = G15_LCD_HEIGHT - SUPERIOR_SPACE - bar_heights_peak[i] - detached_peak;
		    peak2 = peak1;
		    /* inferior limit */
		    if ( peak2 < 34 )                    
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
  g15spectrum_read_config();

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
	      scope_data[i] = data[0][i] / scale;  /* FIXME: Use both channels? */
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

      /* dynamically calculate the scale (maybe changed in config) */
      scale = (G15_LCD_HEIGHT - INFERIOR_SPACE - SUPERIOR_SPACE + amplification) / ( log((1 - linearity) / linearity) *2 );  
      x00 = linearity*linearity*32768.0/(2 * linearity - 1);
      y00 = -log(-x00) * scale;

      for (i = 0; i < WIDTH; i++) {
	y = (gdouble)data[0][i] * (i + 1); /* Compensating the energy */  /* FIXME: Use both channels? */
	y = ( log(y - x00) * scale + y00 ); /* Logarithmic amplitude */
	
	y = ( (dif-2)*y + /* FIXME: conditionals should be rolled out of the loop */
	      (i==0       ? y : bar_heights[i-1]) +
	      (i==WIDTH-1 ? y : bar_heights[i+1])) / dif; /* Add some diffusion */
	y = ((tau-1)*bar_heights[i] + y) / tau; /* Add some dynamics */
	bar_heights[i] = (gint16)y; /* amplification non mi CAMBIA!!! */
      }
      
    }
  
  pthread_mutex_unlock(&g15buf_mutex);
  
  return;
  
}

