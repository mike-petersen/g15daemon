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
#define G15DAEMON_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include "g15daemon.h"
#include <libg15.h>

/* set a pixel in a libg15 buffer */
void g15daemon_setpixel(lcd_t *lcd, unsigned int x, unsigned int y, unsigned int val)
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


void g15daemon_line (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int colour) {

    int d, sx, sy, dx, dy;
    unsigned int ax, ay;

    x1 = x1 - 1;
    y1 = y1 - 1;
    x2 = x2 - 1;
    y2 = y2 - 1;

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
    g15daemon_setpixel (lcd, x1, y1, colour);

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
            g15daemon_setpixel (lcd, x1, y1, colour);
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
            g15daemon_setpixel (lcd, x1, y1, colour);
        }
    }
}


void g15daemon_rectangle (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, int filled, unsigned int colour) {

    int y;

    if (x1 != x2 && y1 != y2)
    {
        if (!filled)
        {
            g15daemon_line (lcd, x1, y1, x2, y1, colour);
            g15daemon_line (lcd, x1, y1, x1, y2, colour);
            g15daemon_line (lcd, x1, y2, x2, y2, colour);
            g15daemon_line (lcd, x2, y1, x2, y2, colour);
        }
        else
        {
            for (y = y1; y <= y2; y++)
            {
                g15daemon_line(lcd,x1,y,x2,y,colour);
            }
        }
    }
}


void g15daemon_draw_bignum (lcd_t * lcd, unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, unsigned int colour, int num) {
    x1 += 2;
    x2 -= 2;

    switch(num){
        case 45: 
            g15daemon_rectangle (lcd, x1, y1+((y2/2)-2), x2, y1+((y2/2)+2), 1, BLACK);
            break;
        case 46:
            g15daemon_rectangle (lcd, x2-5, y2-5, x2, y2 , 1, BLACK);
            break;
        case 48:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1 +5, y1 +5, x2 -5, y2 - 6, 1, WHITE);
            break;
        case 49: 
            g15daemon_rectangle (lcd, x2-5, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1, y1, x2 -5, y2, 1, WHITE);
            break;
        case 50:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1, y1+5, x2 -5, y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1+5, y1+((y2/2)+3), x2 , y2-6, 1, WHITE);
            break;
        case 51:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1, y1+5, x2 -5, y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 52:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1, y1+((y2/2)+3), x2 -5, y2, 1, WHITE);
            g15daemon_rectangle (lcd, x1+5, y1, x2-5 , y1+((y2/2)-3), 1, WHITE);
            break;
        case 53:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1+5, y1+5, x2 , y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 54:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1+5, y1+5, x2 , y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1+5, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 55:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1, y1+5, x2 -5, y2, 1, WHITE);
            break;
        case 56:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1+5, y1+5, x2-5 , y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1+5, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 57:
            g15daemon_rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            g15daemon_rectangle (lcd, x1+5, y1+5, x2-5 , y1+((y2/2)-3), 1, WHITE);
            g15daemon_rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2, 1, WHITE);
            break;
        case 58: 
            g15daemon_rectangle (lcd, x2-5, y1+5, x2, y1+10 , 1, BLACK);
            g15daemon_rectangle (lcd, x2-5, y2-10, x2, y2-5 , 1, BLACK);
            break;

    }
}

