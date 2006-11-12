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

#include <stdio.h>
#include <stdlib.h>
#include "g15daemon.h"
#include <libg15.h>
#include <libg15render.h>

/* set a pixel in a libg15 buffer */
void setpixel(lcd_t *lcd, unsigned int x, unsigned int y, unsigned int val)
{
    unsigned int curr_row = y;
    unsigned int curr_col = x;

    unsigned int pixel_offset = curr_row * LCD_WIDTH + curr_col;
    unsigned int byte_offset = pixel_offset / 8;
    unsigned int bit_offset = 7-(pixel_offset % 8);

    if (val)
        lcd->buf[byte_offset] = lcd->buf[byte_offset] | 1 << bit_offset;
    else
        lcd->buf[byte_offset] = lcd->buf[byte_offset]  &  ~(1 << bit_offset);
}

void draw_bignum (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int colour, int num) {
    x1 += 2;
    x2 -= 2;

    g15canvas *canvas = (g15canvas *) malloc (sizeof (g15canvas));
    g15r_initCanvas (canvas);
    memcpy (canvas->buffer, lcd->buf, G15_BUFFER_LEN);

    switch(num){
        case 45: 
            g15r_pixelBox (canvas, x1, y1+((y2/2)-2), x2, y1+((y2/2)+2), G15_COLOR_BLACK, 1, 1);
            break;
        case 46:
            g15r_pixelBox (canvas, x2-5, y2-5, x2, y2 , G15_COLOR_BLACK, 1, 1);
            break;
        case 48:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1 +5, y1 +5, x2 -5, y2 - 6, G15_COLOR_WHITE, 1, 1);
            break;
        case 49: 
            g15r_pixelBox (canvas, x2-5, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1, y1, x2 -5, y2, G15_COLOR_WHITE, 1, 1);
            break;
        case 50:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1, y1+5, x2 -5, y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+((y2/2)+3), x2 , y2-6, G15_COLOR_WHITE, 1, 1);
            break;
        case 51:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1, y1+5, x2 -5, y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1, y1+((y2/2)+3), x2-5 , y2-6, G15_COLOR_WHITE, 1, 1);
            break;
        case 52:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1, y1+((y2/2)+3), x2 -5, y2, G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1, x2-5 , y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            break;
        case 53:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+5, x2 , y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1, y1+((y2/2)+3), x2-5 , y2-6, G15_COLOR_WHITE, 1, 1);
            break;
        case 54:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+5, x2 , y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+((y2/2)+3), x2-5 , y2-6, G15_COLOR_WHITE, 1, 1);
            break;
        case 55:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1, y1+5, x2 -5, y2, G15_COLOR_WHITE, 1, 1);
            break;
        case 56:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+5, x2-5 , y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+((y2/2)+3), x2-5 , y2-6, G15_COLOR_WHITE, 1, 1);
            break;
        case 57:
            g15r_pixelBox (canvas, x1, y1, x2, y2 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x1+5, y1+5, x2-5 , y1+((y2/2)-3), G15_COLOR_WHITE, 1, 1);
            g15r_pixelBox (canvas, x1, y1+((y2/2)+3), x2-5 , y2, G15_COLOR_WHITE, 1, 1);
            break;
        case 58: 
            g15r_pixelBox (canvas, x2-5, y1+5, x2, y1+10 , G15_COLOR_BLACK, 1, 1);
            g15r_pixelBox (canvas, x2-5, y2-10, x2, y2-5 , G15_COLOR_BLACK, 1, 1);
            break;
    }
    memcpy (lcd->buf, canvas->buffer, G15_BUFFER_LEN);
}

