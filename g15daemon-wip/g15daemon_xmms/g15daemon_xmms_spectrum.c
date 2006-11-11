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
*/

/* xmms plugin for the daemon, based on finespectrum plugin available on sourceforge */
#include <stdlib.h>
#include <unistd.h>
#include <g15daemon_client.h>
#include <math.h>
#include <pthread.h>
#include <xmms/plugin.h>
#include <xmms/util.h>
#include <xmms/xmmsctrl.h>
#include <string.h>
#include <glib.h>

#include "font.h"

#define WIDTH 256

#define WHITE 0
#define BLACK 1

/* Linearity of the amplitude scale (0.5 for linear, keep in [0.1, 0.9]) */
//#define linearity 0.33
#define linearity 0.33

#define NUM_BANDS 16        

/* Time factor of the band dinamics. 3 means that the coefficient of the
last value is half of the current one's. (see source) */
#define tau 3

/* Factor used for the diffusion. 4 means that half of the height is
added to the neighbouring bars */
#define dif 3

static gint16 bar_heights[WIDTH];

static gdouble scale, x00, y00;

static void g15analyser_init(void);
static void g15analyser_cleanup(void);
static void g15analyser_playback_start(void);
static void g15analyser_render_freq(gint16 data[2][256]);

static unsigned char lcdbuf[1048];
static unsigned int leaving=0;
pthread_t g15send_thread_hd;
static int g15screen_fd = -1;

pthread_mutex_t g15buf_mutex;

VisPlugin g15analyser_vp = {
    NULL,
    NULL,
    0,
    "G15daemon Spectrum Analyzer 0.2",
    0,
    1,
    g15analyser_init, /* init */
    g15analyser_cleanup, /* cleanup */
    NULL, /* about */
    NULL, /* configure */
    NULL, /* disable_plugin */
    g15analyser_playback_start, /* playback_start */
    g15analyser_playback_start, /* playback_stop */
    NULL, /* render_pcm */
    g15analyser_render_freq  /* render_freq */
};


VisPlugin *get_vplugin_info(void) {
    return &g15analyser_vp;
}

/* set a pixel in a libg15 buffer */
void setpixel(unsigned char *buf, unsigned int x, unsigned int y, unsigned char value)
{
    unsigned int curr_row = y;
    unsigned int curr_col = x;
        
    unsigned int pixel_offset = curr_row * 160 + curr_col;
    unsigned int byte_offset = pixel_offset / 8;
    unsigned int bit_offset = 7-(pixel_offset % 8);
    if(value)                    
       buf[byte_offset] = buf[byte_offset] | 1 << bit_offset;
    else
       buf[byte_offset] = buf[byte_offset]  &  ~(1 << bit_offset);
       
}
                                            

void renderCharacterMediumGFX(unsigned char *buffer, int col, int row, char character,unsigned int inverse, unsigned int destructive)
{
    int helper = character * 7 * 5; // for our font which is 6x4
    int top_left_pixel_x = col; // 1 pixel spacing
    int top_left_pixel_y = row; // once again 1 pixel spacing
    int x, y;

    for (y=0;y<7;++y)
    {
        for (x=0;x<5;++x)
        {
            char font_entry = fontdata_7x5[helper + y * 5 + x];
            if (font_entry)
                setpixel(buffer,top_left_pixel_x+x,top_left_pixel_y+y,inverse == BLACK ? WHITE : BLACK);
            else
                if(destructive)
                    setpixel(buffer,top_left_pixel_x+x,top_left_pixel_y+y,inverse == BLACK ? BLACK : WHITE);
        }
    }
}
                                            

void draw_str (unsigned char *buf, char *str, int x, int y, int colour,  unsigned int destructive)
{
    char *p = str;
    int width=5;

    while (*p)
    {
            renderCharacterMediumGFX(buf, x,y, *p,colour^1,destructive);
        p++;
        x += width;
    }
}

// Draw a line from (x1,y1) to (x2,y2) in a given colour (colour)
// No restrictions are placed on these input values. (so x1>x2 is no problem)
// (Bressenham line algorithm uses only integer arithmetic)
void line (unsigned char *buf, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int colour)
{
    int  d, sx, sy, dx, dy;
    unsigned int ax, ay;

    dx = x2 - x1;
    ax = abs (dx) << 1;
    if (dx < 0)
        sx = -1;
    else
        sx = 1;

    dy = y2 - y1;
    ay = abs (dy) << 1;
    if (dy < 0)
        sy = -1;
    else
        sy = 1;

    /* set the pixel */
    setpixel (buf, x1, y1, colour);

    if (ax > ay)
    {
        d = ay - (ax >> 1);
        while (x1 != x2)
        {
            if (d >= 0)
            {
                y1 += sy;
                d -= ax;
            }
            x1 += sx;
            d += ay;
            setpixel (buf, x1, y1, colour);
        }
    }
    else
    {
        d = ax - (ay >> 1);
        while (y1 != y2)
        {
            if (d >= 0)
            {
                x1 += sx;
                d -= ay;
            }
            y1 += sy;
            d += ax;
            setpixel (buf, x1, y1, colour);
        }
    }
}

void rectangle (unsigned char *buf, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int filled, unsigned int colour)
{
    unsigned int y;

    if (x1 != x2 && y1 != y2)
    {
        if (!filled)
        {
            line (buf, x1, y1, x2, y1, colour);
            line (buf, x1, y1, x1, y2, colour);
            line (buf, x1, y2, x2, y2, colour);
            line (buf, x2, y1, x2, y2, colour);
        }
        else
        {
            for (y = y1; y <= y2; y++ )
            {
                line (buf, x1, y, x2, y, colour);
            }
        }
    }
}

// given a maximum value, and a value between 0 and that maximum value, calculate and draw a bar showing that percentage
void draw_bar (unsigned char *buf, int x1, int y1, int x2, int y2, int colour, int num, int max, int type)
{
    float len, length;
    if (num > max)
        num = max;

    if(type==2)
        y1+=2;y2-=2;x1+=2;x2-=2;

    len = ((float) max / (float) num);
    length = (x2 - x1) / len;

    if(type==1){
        rectangle (buf, x1, y1-type, x2, y2+type, 1, colour ^1 );
        rectangle (buf, x1, y1-type, x2, y2+type, 0, colour);
    } else if(type==2){
        rectangle (buf, x1-2, y1-type, x2+2, y2+type, 1, colour ^1 );
        rectangle (buf, x1-2, y1-type, x2+2, y2+type, 0, colour);
    }else if(type==3) {
        line (buf, x1, y1-type, x1, y2+type, colour);
        line (buf, x2, y1-type, x2, y2+type, colour);  
        line (buf, x1, y1+((y2-y1)/2),x2, y1+((y2-y1)/2), colour);
    }
    rectangle (buf, x1, y1 , (int) ceil(x1 + length) , y2 , 1, colour );
}
                                                                                                                    
void *g15send_thread() {
    int i;
    int playlist_pos;
    char *title;
    char *artist;
    char *song;
                                            
    while(!leaving){
       pthread_mutex_lock(&g15buf_mutex);
       
        memset(lcdbuf,0,1048);

        for(i = 0; i < NUM_BANDS; i++)
        {               
          rectangle(lcdbuf,i*10,40-bar_heights[i],(i*10)+8,35,1,BLACK);
        }

        playlist_pos = xmms_remote_get_playlist_pos(0);
        title = xmms_remote_get_playlist_title(0, playlist_pos);
        if(strlen(title)>32) {
          artist = strtok(title,"-");
          song = strtok(NULL,"-");
          if(strlen(song)>32)
            song[32]='\0';
          draw_str (lcdbuf, song+1, 165-(strlen(song)*5), 0, BLACK,  1);
          if(strlen(artist)>32)
            artist[32]='\0';
          if(artist[strlen(artist)-1]==' ')
           artist[strlen(artist)-1]='\0';
          draw_str (lcdbuf, artist, 160-(strlen(artist)*5), 8, BLACK,  1);
        } else
            draw_str (lcdbuf, title, 160-(strlen(title)*5), 0, BLACK,  1);
        
        draw_bar (lcdbuf, -2, 40, 160, 43, BLACK, xmms_remote_get_output_time(0)/1000, xmms_remote_get_playlist_time(0,playlist_pos)/1000, 1);

        g15_send(g15screen_fd,lcdbuf,1048);
        pthread_mutex_unlock(&g15buf_mutex);
        
       xmms_usleep(25000);
    }

    return NULL;
}


static void g15analyser_init(void) {

    pthread_attr_t attr;
    memset(lcdbuf,0,1048);
    memset(&attr,0,sizeof(pthread_attr_t));
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    
    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        return;
    }
    scale = G15_HEIGHT / ( log((1 - linearity) / linearity) *2 );
    x00 = linearity*linearity*32768.0/(2 * linearity - 1);
    y00 = -log(-x00) * scale;
    
    leaving = 0;
    
    pthread_mutex_init(&g15buf_mutex, NULL);        
    pthread_create(&g15send_thread_hd, &attr, g15send_thread, 0);
}

static void g15analyser_cleanup(void) {
    
    leaving=1;
    
    if(g15screen_fd)
        close(g15screen_fd);
}

static void g15analyser_playback_start(void) {
    
    memset(lcdbuf,0,1048);
}

static void g15analyser_render_freq(gint16 data[2][256]) {
    
    gint i;
    gdouble y;
    int j;
    int xscale[] = {0, 1, 2, 3, 5, 7, 10, 14, 20, 28, 40, 54, 74,101, 137, 187, 255};

    if(!g15screen_fd)
        return;
    
    pthread_mutex_lock(&g15buf_mutex);

    for(i = 0; i < NUM_BANDS; i++)
    {
      for(j=xscale[i], y=0; j < xscale[i+1]; j++)
      {
	  if(data[0][j] > y)
	    y = data[0][j];
      }

      if(y)
      {         
          y = (gint)(log(y) * (16/log(256)));
          if(y > 32) y = 32;
      }

      bar_heights[i] = y;
    }
    pthread_mutex_unlock(&g15buf_mutex);

    return;
}

