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
  
  (c) 2006-2007 Mike Lampard, Philip Lawatsch, Antonio Bartolini, and others
  
  This daemon listens on localhost port 15550 for client connections,
  and arbitrates LCD display.  Allows for multiple simultaneous clients.
  Client screens can be cycled through by pressing the 'L1' key.
  
  simple analyser xmms plugin for g15daemon
*/

/* Some useful costants */
#define WIDTH 256
#define PLUGIN_VERSION "2.5.8"
#define PLUGIN_NAME    "G15daemon Visualization Plugin"
#define INFERIOR_SPACE 7 /* space between bars and position bar */


/* Time factor of the band dinamics. 3 means that the coefficient of the
   last value is half of the current one's. (see source) */
#define tau 4.5
/* Factor used for the diffusion. 4 means that half of the height is
   added to the neighbouring bars */
#define dif 3 

#define ROWLEN 35

/* height of a char */
#define CHAR_HEIGH 9

/* font size for title */
#define FONT_SIZE 8

/* separator of line */
#define SEPARATOR "-"

/* BEGIN CONFIG VARIABLES */ 
/* for default values see below */

/* Float. Linearity of the amplitude scale (0.5 for linear, keep in [0.1, 0.49]) */
static float linearity;

/* Integer. Amplification of the scale. Can be negative. +-30 is a reasonable range */
static int amplification;

/* Integer. limit (in px) of max length of bars avoid overlap (internal use) */
static unsigned int limit;  

/* Integer. Number of Bars - Must be a divisor of 256! allowed: 2 4 8 16 32 64 128(no space between bars) */   
static unsigned int num_bars;

/* Boolean. Variable to disable keybindings Default: Disabled */
static unsigned int enable_keybindings;

/* Integer. Visualization  type */
static unsigned int vis_type;

/* Boolean. Peak visualization enable */
static unsigned int enable_peak;

/* Boolean. Detached peak from bars */
static unsigned int detached_peak;

/* Boolean. Enable Analog Mode */
static unsigned int analog_mode;

/* Boolean. Enable Line Mode */
static unsigned int line_mode;

/* Integer. Step for leds in Analog Mode. Min value:  2 */
static unsigned int analog_step;

/* Boolean. Show Title */
static unsigned int show_title;

/* Boolean. Show bar */
static unsigned int show_pbar;

/* Boolean. Show time in progress bar */
static unsigned int show_time;

/* Integer. Number of row of title */
static unsigned int rownum;

/* Boolean. Title overlay */
static unsigned int title_overlay;


/* END CONFIG VARIABLES */

/* Set here defaults values */
static unsigned int def_vis_type = 0;
static unsigned int def_num_bars = 32;
static float def_linearity = 0.37;
static int def_amplification = 0;
static unsigned int def_limit = G15_LCD_HEIGHT - INFERIOR_SPACE - CHAR_HEIGH;
static unsigned int def_enable_peak = TRUE;
static unsigned int def_detached_peak = TRUE;
static unsigned int def_analog_mode = FALSE;
static unsigned int def_line_mode = FALSE;
static unsigned int def_analog_step = 2;
static unsigned int def_enable_keybindings = FALSE;
static unsigned int def_show_title = TRUE;
static unsigned int def_show_pbar = TRUE;
static unsigned int def_show_time = TRUE;
static unsigned int def_rownum = 1;
static unsigned int def_title_overlay = FALSE;


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
static gint g15analyser_disable(gpointer data);

static void g15analyser_conf_ok(GtkWidget *w, gpointer data);
static void g15analyser_conf_apply(void);
static void g15analyser_conf_cancel(void);
static void g15analyser_conf_reset(void);

static g15canvas *canvas;
static g15font   *font;

static unsigned int playing=0, paused=0;
static int g15screen_fd = -1;

static pthread_mutex_t g15buf_mutex;

static Display *dpy;
static Window root_win;

static int mmedia_timeout_handle;
static int g15keys_timeout_handle;
static int g15disp_timeout_handle;

/* scrollin text stuff */
static int text_start = 60;
static int text_start2 = -1;

static int lastvolume;
static int volume;

/* gdk stuff */
static GtkWidget *configure_win = NULL;
static GtkWidget *vbox, *hbox;
static GtkWidget *bbox, *ok, *cancel, *apply, *defaults;
static GtkWidget *t_options_bars_radio, *t_options_scope_radio;
static GtkWidget *t_options_effect_no, *t_options_effect_line, *t_options_effect_peak, *t_options_effect_analog;
static GtkWidget *t_options_vistype;
static GtkWidget *t_options_bars, *t_options_bars_effects;
static GtkWidget *g_options_frame ,*g_options_enable_keybindings, *g_options_enable_dpeak;
static GtkWidget *g_options_show_title, *g_options_show_pbar, *g_options_show_time, *g_options_title_overlay;
static GtkWidget *g_options_frame_bars;
static GtkWidget *g_options_frame_bars_effects;
static GtkWidget *scale_bars, *scale_lin, *scale_ampli, *scale_step, *scale_rownum;
static GtkObject *adj_bars, *adj_lin, *adj_ampli, *adj_step, *adj_rownum;

static gint tmp_bars=-1, tmp_step=-1, tmp_ampli=-1000, tmp_rownum=-1; 
static gfloat tmp_lin=-1;

VisPlugin g15analyser_vp = {
#ifdef OLD_PLUGIN
  
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
  
#else
  
  .description =    PLUGIN_NAME " " PLUGIN_VERSION,
  .num_pcm_chs_wanted = 1,
  .num_freq_chs_wanted = 1,
  .init =           g15analyser_init,           /* init           */
  .cleanup =        g15analyser_cleanup,        /* cleanup        */
  .about =          g15analyser_about,          /* about          */
  .configure =      g15analyser_conf,           /* configure      */
  .playback_start = g15analyser_playback_start, /* playback_start */
  .playback_stop =  g15analyser_playback_stop,  /* playback_stop  */
  .render_pcm =     g15analyser_render_pcm,     /* render_pcm     */
  .render_freq =    g15analyser_render_freq     /* render_freq    */
  
#endif
};
