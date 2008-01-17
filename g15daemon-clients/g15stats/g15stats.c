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

(c) 2008 Mike Lampard 

$Revision$ -  $Date$ $Author$

This daemon listens on localhost port 15550 for client connections,
and arbitrates LCD display.  Allows for multiple simultaneous clients.
Client screens can be cycled through by pressing the 'L1' key.

This is a cpu and memory stats client
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libg15.h>
#include <ctype.h>
#include <g15daemon_client.h>
#include <libg15render.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <glibtop.h>
#include <glibtop/cpu.h>
#include <glibtop/mem.h>
#include <glibtop/sysdeps.h> 
#include <glibtop/netload.h> 
#include <glibtop/swap.h> 
#include <glibtop/loadavg.h>
#include <glibtop/uptime.h>

int leaving=0;
int g15screen_fd;
int cycle = 0;


void drawBar_reversed (g15canvas * canvas, int x1, int y1, int x2, int y2, int color,
                       int num, int max, int type)
{
    float len, length;
    if (max <= 0 || num <= 0)
        return;
    if (num > max)
        num = max;

    if (type == 2)
    {
        y1 += 2;
        y2 -= 2;
        x1 += 2;
        x2 -= 2;
    }

    len = ((float) max / (float) num);
    length = (x2 - x1) / len;

    if (type == 1)
    {
        g15r_pixelBox (canvas, x1, y1 - type, x2, y2 + type, color ^ 1, 1, 1);
        g15r_pixelBox (canvas, x1, y1 - type, x2, y2 + type, color, 1, 0);
    }
    else if (type == 2)
    {
        g15r_pixelBox (canvas, x1 - 2, y1 - type, x2 + 2, y2 + type, color ^ 1,
                       1, 1);
        g15r_pixelBox (canvas, x1 - 2, y1 - type, x2 + 2, y2 + type, color, 1,
                       0);
    }
    else if (type == 3)
    {
        g15r_drawLine (canvas, x1, y1 - type, x1, y2 + type, color);
        g15r_drawLine (canvas, x2, y1 - type, x2, y2 + type, color);
        g15r_drawLine (canvas, x1, y1 + ((y2 - y1) / 2), x2,
                       y1 + ((y2 - y1) / 2), color);
    }
    g15r_pixelBox (canvas, (int) ceil (x2-length), y1, x2, y2, color, 1, 1);
    if(type == 5) {
        int x;
        for(x=x2-2;x>(x2-length);x-=2)
            g15r_drawLine (canvas, x, y1, x, y2, color^1);
    }
}


int daemonise(int nochdir, int noclose) {
    pid_t pid;

    if(nochdir<1)
        chdir("/");
    pid = fork();

    switch(pid){
        case -1:
            printf("Unable to daemonise!\n");
            return -1;
            break;
            case 0: {
                umask(0);
                if(setsid()==-1) {
                    perror("setsid");
                    return -1;
                }
                if(noclose<1) {
                    freopen( "/dev/null", "r", stdin);
                    freopen( "/dev/null", "w", stdout);
                    freopen( "/dev/null", "w", stderr);
                }
                break;
            }
        default:
            _exit(0);
    }
    return 0;
}

void draw_mem_screen(g15canvas *canvas) {
    char tmpstr[1024];

    glibtop_mem mem;

    glibtop_get_mem(&mem);

    g15r_clearScreen (canvas, G15_COLOR_WHITE);
//  g15r_pixelBox (canvas, 1, 0, 159, 42, 1, 1, 0);

//  g15r_pixelBox (canvas, 1, 0, 159, 21, 1, 1, 0);
//  g15r_pixelBox (canvas, 1, 21, 159, 42, 1, 1, 0);
    g15r_drawLine (canvas, 2, 20, 158, 20, 1);
    g15r_drawLine (canvas, 2, 21, 158, 21, 1);
    g15r_drawLine (canvas, 2, 22, 158, 22, 1);

    g15r_drawBar(canvas,2,9,158,21,1,mem.user,mem.total,4);
    drawBar_reversed(canvas,2,21,158,33,1,mem.cached+mem.buffer,mem.total,4);

    canvas->mode_xor = 1;
    sprintf(tmpstr,"Memory Total %uMb",(unsigned int)mem.total/(1024*1000));
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_MED, 80-(strlen(tmpstr)*5)/2, 2);
    sprintf(tmpstr,"User %uMb",(unsigned int)mem.user/(1024*1000));
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_MED, 10, 13);

    sprintf(tmpstr,"Cache %uMb",(unsigned int)(mem.cached+mem.buffer)/(1024*1000));
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_MED, 148-(strlen(tmpstr)*5), 25);
    sprintf(tmpstr,"Memory Free %uMb",(unsigned int)mem.free/(1024*1000));
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_MED, 80-(strlen(tmpstr)*5)/2, 35);

    canvas->mode_xor = 0; 
}

void draw_swap_screen(g15canvas *canvas) {
    char tmpstr[1024];

    glibtop_swap swap;

    glibtop_get_swap(&swap);

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    sprintf(tmpstr,"Swap Used %ukb of %ukb",(unsigned int)swap.used/1024,(unsigned int)swap.total/1024);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 80-(strlen(tmpstr)*5)/2, 2);
    g15r_drawBar(canvas,3,12,157,19,1,(unsigned int)swap.used/(1024),(unsigned int)swap.total/1024,1);

    sprintf(tmpstr,"Free Swap: %ukb",(unsigned int)swap.free/1024);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 80-(strlen(tmpstr)*5)/2, 23);

    sprintf(tmpstr,"Paged in: %u, Paged out: %u",(unsigned int)swap.pagein,(unsigned int)swap.pageout);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 80-(strlen(tmpstr)*5)/2, 35);
}

void draw_cpu_screen(g15canvas *canvas) {
    glibtop_cpu cpu;
    glibtop_loadavg loadavg;
    glibtop_uptime uptime;

    int total,user,nice,sys,idle;
    int b_total,b_user,b_nice,b_sys,b_idle,b_irq,b_iowait;
    static int last_total,last_user,last_nice,last_sys,last_idle,last_iowait,last_irq;
    char tmpstr[1024];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_cpu(&cpu);
    glibtop_get_loadavg(&loadavg);
    glibtop_get_uptime(&uptime);

    total = ((unsigned long) cpu.total) ? ((double) cpu.total) : 1.0;
    user  = ((unsigned long) cpu.user)  ? ((double) cpu.user)  : 1.0;
    nice  = ((unsigned long) cpu.nice)  ? ((double) cpu.nice)  : 1.0;
    sys   = ((unsigned long) cpu.sys)   ? ((double) cpu.sys)   : 1.0;
    idle  = ((unsigned long) cpu.idle)  ? ((double) cpu.idle)  : 1.0;

    b_total = total - last_total;
    b_user  = user  - last_user;
    b_nice  = nice  - last_nice;
    b_sys   = sys   - last_sys;
    b_idle  = idle  - last_idle;
    b_irq   = cpu.irq - last_irq;
    b_iowait= cpu.iowait - last_iowait;

    last_total = total;
    last_user = user;
    last_nice = nice;
    last_sys = sys;
    last_idle = idle;
    last_irq = cpu.irq;
    last_iowait = cpu.iowait;
    
    sprintf(tmpstr,"Usr %2.f%%",((float)b_user/(float)b_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 2);
    sprintf(tmpstr,"Sys %2.f%%",((float)b_sys/(float)b_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 14);
    sprintf(tmpstr,"Nce %2.f%%",((float)b_nice/(float)b_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 26);

    g15r_drawBar(canvas,43,1,150,10,1,b_user+1,b_total,4);
    g15r_drawBar(canvas,43,12,150,21,1,b_sys+1,b_total,4);
    g15r_drawBar(canvas,43,23,150,32,1,b_nice+1,b_total,4);
    drawBar_reversed(canvas,43,1,150,32,1,b_idle+1,b_total,5);

    g15r_drawLine (canvas, 40, 1, 40, 32, 1);
    g15r_drawLine (canvas, 41, 1, 41, 32, 1);

    g15r_renderString (canvas, (unsigned char*)"I", 0, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"d", 1, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"l", 2, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"e", 3, G15_TEXT_MED, 152, 4);

//  sprintf(tmpstr,"IOWait %u, Interrupts/s %u",b_iowait, b_irq);
    float minutes = uptime.uptime/60;
    float hours = minutes/60;
    float days = 0.0;

    if(hours>24)
        days=(int)(hours/24);
    if(days)
        hours=(int)hours-(days*24);

    sprintf(tmpstr,"LoadAVG %.2f %.2f %.2f | Uptime %.fd%.fh",loadavg.loadavg[0],loadavg.loadavg[1],loadavg.loadavg[2],days,hours);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);

}

void keyboard_watch(void) {
    unsigned int keystate;

    while(!leaving) {
        recv(g15screen_fd,&keystate,4,0);

        if(keystate & G15_KEY_L1) {
            leaving=1;
        }
        else if(keystate & G15_KEY_L2) {
            cycle=0;
        }
        else if(keystate & G15_KEY_L3) {
            cycle=1;
        }
        else if(keystate & G15_KEY_L4) {
            cycle=2;
        }
        else if(keystate & G15_KEY_L5) {
        }
        usleep(100*900);
    }

    return;
}

int main(int argc, char *argv[]){

    g15canvas *canvas;
    pthread_t keys_thread;
    int i;
    int go_daemon=0;

    for (i=0;i<argc;i++) {
        if(0==strncmp(argv[i],"-d",2)||0==strncmp(argv[i],"--daemon",8)) {
            go_daemon=1;
        }
        if(0==strncmp(argv[i],"-h",2)||0==strncmp(argv[i],"--help",6)) {
            printf("%s (c) 2008 Mike Lampard\n",PACKAGE_NAME);
            printf("\nOptions:\n");
            printf("--daemon (-d) run in background\n");
            printf("--help (-h) this help text.\n");
            return 0;
        }
    }        
    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon\n");
        return -1;
    }

    canvas = (g15canvas *) malloc (sizeof (g15canvas));
    if(go_daemon==1) 
        daemonise(0,0);

    if(canvas != NULL)
        g15r_initCanvas(canvas);
    else
        return -1;

    pthread_create(&keys_thread,NULL,(void*)keyboard_watch,NULL);
    glibtop_init();

    while(leaving==0) {

        switch(cycle) {
            case 0:
                draw_cpu_screen(canvas);
                break;
            case 1:   
                draw_mem_screen(canvas);
                break;
            case 2:
                draw_swap_screen(canvas);
                break;
            default:
                printf("cycle reched %i\n",cycle);
        }

        canvas->mode_xor = 0;

        g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

        sleep(1);
    }
    glibtop_close();

    close(g15screen_fd);
    free(canvas);
    return 0;  
}

