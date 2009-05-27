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

(c) 2008-2009 Mike Lampard
(c) 2009 Piotr Czarnecki

$Revision$ -  $Date$ $Author$

This daemon listens on localhost port 15550 for client connections,
and arbitrates LCD display.  Allows for multiple simultaneous clients.
Client screens can be cycled through by pressing the 'L1' key.

This is a simple stats client showing graphs for CPU, MEM & Swap usage, Network traffic and Battery life.
*/
#define _GNU_SOURCE 1

#include <config.h>
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
#include <glibtop/sysinfo.h>
#include "g15stats.h"

int g15screen_fd;

int cycle = 0;
/** Holds the mode type variable of the application running */
int mode = 0;
/** Holds the sub mode type variable of the application running */
int submode = 0;

int have_nic=0;

unsigned int net_hist[MAX_NET_HIST][2];
int net_rr_index=0;

unsigned long net_max_in=0;
unsigned long net_max_out=0;

unsigned long previous_in;
unsigned long previous_out;

_Bool net_scale_absolute=0;

pthread_cond_t wake_now = PTHREAD_COND_INITIALIZER;

unsigned long maxi(unsigned long a, unsigned long b) {
  if(a>b)
    return a;
  return b;
}

char * show_bytes(unsigned long bytes) {
    static char tmpstr[128];
    if(bytes>1024*1024) {
      bytes = bytes / (1024*1024);
      sprintf(tmpstr,"%liMb",bytes);
    }
    else if(bytes<1024*1024 && bytes > 1024) {
      bytes = bytes / 1024;
      sprintf(tmpstr,"%likb",bytes);
    }
    else if(bytes<1024) {
      sprintf(tmpstr,"%lib",bytes);
    }
    return tmpstr;
}

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

void print_sys_load_info(g15canvas *canvas) {
    char tmpstr[MAX_LINES];
    glibtop_loadavg loadavg;
    glibtop_uptime uptime;

    glibtop_get_loadavg(&loadavg);
    glibtop_get_uptime(&uptime);
    
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

void print_mem_info(g15canvas *canvas) {
    char tmpstr[MAX_LINES];
    glibtop_mem mem;
    glibtop_get_mem(&mem);

    sprintf(tmpstr,"Memory Used %dMb | Memory Total %dMb",(unsigned int)((mem.buffer+mem.cached+mem.user)/(1024*1024)),(unsigned int)(mem.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);
}

void print_swap_info(g15canvas *canvas) {
    char tmpstr[MAX_LINES];
    glibtop_swap swap;
    glibtop_get_swap(&swap);

    sprintf(tmpstr,"Swap Used %dMb | Swap Avail. %dMb",(unsigned int)(swap.used/(1024*1024)),(unsigned int)(swap.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);
}

void print_net_info(g15canvas *canvas) {
    char tmpstr[MAX_LINES];
    sprintf(tmpstr,"Peak IN %s/s|",show_bytes(net_max_in));
    strcat(tmpstr,"Peak OUT ");
    strcat(tmpstr,show_bytes(net_max_out));
    strcat(tmpstr,"/s");
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);
}

void print_time_info(g15canvas *canvas){
    char tmpstr[MAX_LINES];
    time_t now;
    time(&now);

    sprintf(tmpstr,"%s",ctime(&now));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);
}

void draw_mem_screen(g15canvas *canvas) {
    glibtop_mem mem;
    char tmpstr[MAX_LINES];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_mem(&mem);

    sprintf(tmpstr,"Usr %2.f%%",((float)mem.user/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 2);
    sprintf(tmpstr,"Buf %2.f%%",((float)mem.buffer/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 14);
    sprintf(tmpstr,"Che %2.f%%",((float)mem.cached/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 26);

    g15r_drawBar(canvas,BAR_START,1,BAR_END,10,G15_COLOR_BLACK,(mem.user/1024),(mem.total/1024),4);
    g15r_drawBar(canvas,BAR_START,12,BAR_END,21,G15_COLOR_BLACK,(mem.buffer/1024),(mem.total/1024),4);
    g15r_drawBar(canvas,BAR_START,23,BAR_END,32,G15_COLOR_BLACK,(mem.cached/1024),(mem.total/1024),4);
    drawBar_reversed(canvas,BAR_START,1,BAR_END,32,G15_COLOR_BLACK,(mem.free/1024),(mem.total/1024),5);

    g15r_drawLine (canvas, VL_LEFT, 1, VL_LEFT, 32, G15_COLOR_BLACK);
    g15r_drawLine (canvas, VL_LEFT+1, 1, VL_LEFT+1, 32, G15_COLOR_BLACK);

    g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"R", 1, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 2, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 3, G15_TEXT_MED, TEXT_RIGHT, 4);

    sprintf(tmpstr,"Memory Used %dMb | Memory Total %dMb",(unsigned int)((mem.buffer+mem.cached+mem.user)/(1024*1024)),(unsigned int)(mem.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);

}

void draw_swap_screen(g15canvas *canvas) {
    glibtop_swap swap;
    char tmpstr[MAX_LINES];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_swap(&swap);

    g15r_renderString (canvas, (unsigned char*)"Swap", 0, G15_TEXT_MED, TEXT_LEFT, 9);
    sprintf(tmpstr,"Used %i%%",(unsigned int)(((float)swap.used/(float)swap.total)*100));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 19);

    g15r_drawBar(canvas,BAR_START,1,BAR_END,32,G15_COLOR_BLACK,(swap.used/1024),swap.total/1024,4);

    drawBar_reversed(canvas,BAR_START,1,BAR_END,32,G15_COLOR_BLACK,(swap.total/1024)-(swap.used/1024),swap.total/1024,5);

    g15r_drawLine (canvas, VL_LEFT, 1, VL_LEFT, 32, G15_COLOR_BLACK);
    g15r_drawLine (canvas, VL_LEFT+1, 1, VL_LEFT+1, 32, G15_COLOR_BLACK);

    g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"R", 1, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 2, G15_TEXT_MED, TEXT_RIGHT, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 3, G15_TEXT_MED, TEXT_RIGHT, 4);

    print_swap_info(canvas);
}

/* draw cpu screen.  if drawgraph = 0 then no graph is drawn */
void draw_cpu_screen_unicore_logic(g15canvas *canvas, glibtop_cpu cpu, int drawgraph, int printlabels, int cpuandmemory) {
    int total,user,nice,sys,idle;
    int b_total,b_user,b_nice,b_sys,b_idle,b_irq,b_iowait;
    static int last_total,last_user,last_nice,last_sys,last_idle,last_iowait,last_irq;
    char tmpstr[MAX_LINES];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

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

    last_total  = total;
    last_user   = user;
    last_nice   = nice;
    last_sys    = sys;
    last_idle   = idle;
    last_irq    = cpu.irq;
    last_iowait = cpu.iowait;

    if(printlabels) {
        sprintf(tmpstr,"Usr %2.f%%",((float)b_user/(float)b_total)*100);
        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 2);
        sprintf(tmpstr,"Sys %2.f%%",((float)b_sys/(float)b_total)*100);
        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 14);
        sprintf(tmpstr,"Nce %2.f%%",((float)b_nice/(float)b_total)*100);
        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 26);
    }
    if(drawgraph) {
        g15r_drawBar(canvas,BAR_START,1,BAR_END,10,G15_COLOR_BLACK,b_user+1,b_total,4);
        g15r_drawBar(canvas,BAR_START,12,BAR_END,21,G15_COLOR_BLACK,b_sys+1,b_total,4);
        g15r_drawBar(canvas,BAR_START,23,BAR_END,32,G15_COLOR_BLACK,b_nice+1,b_total,4);
        drawBar_reversed(canvas,BAR_START,1,BAR_END,32,G15_COLOR_BLACK,b_idle+1,b_total,5);

        g15r_drawLine (canvas, VL_LEFT, 1, VL_LEFT, 32, G15_COLOR_BLACK);
        g15r_drawLine (canvas, VL_LEFT+1, 1, VL_LEFT+1, 32, G15_COLOR_BLACK);
    }
    if (cpuandmemory) {
        g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, TEXT_RIGHT, 3);
        g15r_renderString (canvas, (unsigned char*)"R", 1, G15_TEXT_MED, TEXT_RIGHT, 3);
        g15r_renderString (canvas, (unsigned char*)"E", 2, G15_TEXT_MED, TEXT_RIGHT, 3);
        g15r_renderString (canvas, (unsigned char*)"E", 3, G15_TEXT_MED, TEXT_RIGHT, 3);

        sprintf(tmpstr,"CPU %3.f%%",((float)(b_total-b_idle)/(float)b_total)*100);
        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 5);
    } else {
        g15r_renderString (canvas, (unsigned char*)"I", 0, G15_TEXT_MED, TEXT_RIGHT, 4);
        g15r_renderString (canvas, (unsigned char*)"d", 1, G15_TEXT_MED, TEXT_RIGHT, 4);
        g15r_renderString (canvas, (unsigned char*)"l", 2, G15_TEXT_MED, TEXT_RIGHT, 4);
        g15r_renderString (canvas, (unsigned char*)"e", 3, G15_TEXT_MED, TEXT_RIGHT, 4);
    }
}

void draw_cpu_screen_unicore(g15canvas *canvas, int drawgraph, int printlabels) {
    glibtop_cpu cpu;
    glibtop_get_cpu(&cpu);

    draw_cpu_screen_unicore_logic(canvas, cpu, drawgraph, printlabels, 0);
}

void draw_cpu_screen_multicore(g15canvas *canvas, int unicore) {
    glibtop_cpu cpu;
    char tmpstr[MAX_LINES];
    const glibtop_sysinfo *cpuinfo;
    int core,ncpu,ncpumax;
    int divider = 0;
        
    int total,user,nice,sys,idle;
    int b_total,b_user,b_nice,b_sys,b_idle,b_irq,b_iowait;
    static int last_total[GLIBTOP_NCPU],last_user[GLIBTOP_NCPU],last_nice[GLIBTOP_NCPU],
            last_sys[GLIBTOP_NCPU],last_idle[GLIBTOP_NCPU],last_iowait[GLIBTOP_NCPU],last_irq[GLIBTOP_NCPU];

    cpuinfo = glibtop_get_sysinfo();

    if(cpuinfo->ncpu == 0) 
        ncpu = 1;
    else
        ncpu = cpuinfo->ncpu;

    ncpumax = ncpu;

    glibtop_get_cpu(&cpu);

    if (ncpu==1 || unicore) {
        draw_cpu_screen_unicore_logic(canvas, cpu, 1, 1, 0);
        return;
    } else if (((ncpu > 5) && (mode != MODE_CPU_SUMARY)) 
            || (mode == MODE_CPU_USR_SYS_NCE_1)
            || (mode == MODE_CPU_USR_SYS_NCE_2)) {
        draw_cpu_screen_unicore_logic(canvas, cpu, 0, 1, 0);
    } else if (mode != MODE_CPU_SUMARY){
        draw_cpu_screen_unicore_logic(canvas, cpu, 0, 0, 0);
    } else {
        draw_cpu_screen_unicore_logic(canvas, cpu, 0, 0, 1);
    }

    int y1=0, y2=32, currentValue, mem_used=1, mem_total=1;

    int spacer = 1;
    int height = 8;
    if (mode) {
        switch (mode) {
            case    MODE_CPU_TOTAL :
                if(ncpu > 11){
                    spacer = 0;
                }
                height = 37;
                break;
            case    MODE_CPU_USR_SYS_NCE_2 :
                if(ncpu > 4){
                    spacer = 0;
                }
                height = 12;

                ncpumax = height;
                break;
            case    MODE_CPU_SUMARY :
                if(ncpu>6){
                    spacer = 0;
                }
                height = 16;

                ncpumax = height;
                glibtop_mem mem;
                glibtop_get_mem(&mem);
                mem_used  = ((mem.total - mem.free) / 1024);
                mem_total = (mem.total / 1024);

                sprintf(tmpstr,"MEM %3.f%%",((float)(mem_used)/(float)mem_total)*100);
                g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 24);
                break;
        }

        if (ncpumax < ncpu) {
            height = (height - ((ncpumax - 1) * spacer)) / (ncpumax);
        } else {
            height = (height - ((ncpu - 1) * spacer)) / (ncpu);
        }

        if(height < 1){
            height = 1;
        }
    }

    for(core=0;core<ncpu && core<ncpumax;core++) {
        total = ((unsigned long) cpu.xcpu_total[core]) ? ((double) cpu.xcpu_total[core]) : 1.0;
        user  = ((unsigned long) cpu.xcpu_user[core])  ? ((double) cpu.xcpu_user[core])  : 1.0;
        nice  = ((unsigned long) cpu.xcpu_nice[core])  ? ((double) cpu.xcpu_nice[core])  : 1.0;
        sys   = ((unsigned long) cpu.xcpu_sys[core])   ? ((double) cpu.xcpu_sys[core])   : 1.0;
        idle  = ((unsigned long) cpu.xcpu_idle[core])  ? ((double) cpu.xcpu_idle[core])  : 1.0;

        b_total     = total - last_total[core];
        b_user      = user  - last_user[core];
        b_nice      = nice  - last_nice[core];
        b_sys       = sys   - last_sys[core];
        b_idle      = idle  - last_idle[core];
        b_irq       = cpu.xcpu_irq[core]    - last_irq[core];
        b_iowait    = cpu.xcpu_iowait[core] - last_iowait[core];

        last_total[core]	= total;
        last_user[core] 	= user;
        last_nice[core] 	= nice;
        last_sys[core] 		= sys;
        last_idle[core] 	= idle;
        last_irq[core] 		= cpu.xcpu_irq[core];
        last_iowait[core] 	= cpu.xcpu_iowait[core];

        if (mode) {
            y1 = (core * height) + (core * spacer);
            y2 = y1 + height-1;
            currentValue = b_total-b_idle;
        }

        switch (mode) {
            case    MODE_CPU_USR_SYS_NCE_1   :
                divider = 9/ncpu;
                g15r_drawBar(canvas,BAR_START,(divider*core),BAR_END,(divider+(divider*(core))),G15_COLOR_BLACK,b_user+1,b_total,4);
                g15r_drawBar(canvas,BAR_START,12+(divider*(core)),BAR_END,12+(divider+(divider*(core))),G15_COLOR_BLACK,b_sys+1,b_total,4);
                y1  = 0;
                y2  = 24+(divider+(divider*(core)));
                g15r_drawBar(canvas,BAR_START,24+(divider*(core)),BAR_END,y2,G15_COLOR_BLACK,b_nice+1,b_total,4);
                
                divider = y2/ncpu;
                drawBar_reversed(canvas,BAR_START,(divider*core),BAR_END,y2,G15_COLOR_BLACK,b_idle+1,b_total,5);
                break;
            case    MODE_CPU_TOTAL   :
                if (ncpu < 6) {
                    sprintf(tmpstr,"CPU%.f%3.f%%",(float)core,((float)(b_total-b_idle)/(float)b_total)*100);
                    if (ncpu < 5) {
                        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, y1+1);
                    } else {
                        g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 1, y1+1);
                    }
                }

                g15r_drawBar(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, currentValue, b_total,4);
                drawBar_reversed(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, b_total-currentValue, b_total,5);
                y1 = 0;
                break;
            case    MODE_CPU_USR_SYS_NCE_2   :
                g15r_drawBar(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, currentValue, b_total,4);
                g15r_drawBar(canvas, BAR_START,12+y1,BAR_END,12+y2,G15_COLOR_BLACK,b_sys+1,b_total,4);
                g15r_drawBar(canvas, BAR_START,24+y1,BAR_END,24+y2,G15_COLOR_BLACK,b_nice+1,b_total,4);


                drawBar_reversed(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, b_total-currentValue, b_total,5);
                drawBar_reversed(canvas, BAR_START,12+y1,BAR_END,12+y2,G15_COLOR_BLACK,b_total-b_sys,b_total,5);

                y2 += 24;
                drawBar_reversed(canvas, BAR_START,24+y1,BAR_END, y2,G15_COLOR_BLACK,b_total-b_nice,b_total,5);

                y1 = 0;
                break;
            case    MODE_CPU_SUMARY   :
                g15r_drawBar(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, currentValue, b_total,4);
                g15r_drawBar(canvas, BAR_START,19+y1,BAR_END,19+y2,G15_COLOR_BLACK, mem_used+1, mem_total,4);

                drawBar_reversed(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, b_total-currentValue, b_total,5);
                drawBar_reversed(canvas, BAR_START,19+y1,BAR_END,19+y2,G15_COLOR_BLACK, mem_total - mem_used, mem_total,5);
                y1 = 0;
                break;
        }
    }

    g15r_drawLine (canvas, VL_LEFT, y1, VL_LEFT, y2, G15_COLOR_BLACK);
    g15r_drawLine (canvas, VL_LEFT+1, y1, VL_LEFT+1, y2, G15_COLOR_BLACK);

    if(mode == 3) {
        g15r_drawLine (canvas, VL_LEFT, 19+y1, VL_LEFT, 19+y2, G15_COLOR_BLACK);
        g15r_drawLine (canvas, VL_LEFT+1, 19+y1, VL_LEFT+1, 19+y2, G15_COLOR_BLACK);
    }
}

void draw_net_screen(g15canvas *canvas, char *interface) {
    int i;
    int x=0;
    char tmpstr[128];
    x=0;
    float diff=0;
    float height=0;
    float last=0;

    g15r_clearScreen (canvas, G15_COLOR_WHITE);
    glibtop_netload netload;
    glibtop_get_netload(&netload,interface);    
    // in
    x=53;
    for(i=net_rr_index+1;i<MAX_NET_HIST;i++) {
      diff = (float) net_max_in / (float) net_hist[i][0];
      height = 16-(16/diff);
      g15r_setPixel(canvas,x,height,G15_COLOR_BLACK);
      g15r_drawLine(canvas,x,height,x-1,last,G15_COLOR_BLACK);
      last=height;
      x++;
    }
    for(i=0;i<net_rr_index;i++) {
      diff = (float) net_max_in / (float) net_hist[i][0];
      height = 16-(16 / diff);
      g15r_drawLine(canvas,x,height,x-1,last,G15_COLOR_BLACK);
      last=height;
      x++;
    }
    // out
    x=53;
    for(i=net_rr_index+1;i<MAX_NET_HIST;i++) {
      diff = (float) net_max_out / (float) net_hist[i][1];
      height = 34-(16/diff);
      g15r_setPixel(canvas,x,height,G15_COLOR_BLACK);
      g15r_drawLine(canvas,x,height,x-1,last,G15_COLOR_BLACK);
      last=height;
      x++;
    }
    for(i=0;i<net_rr_index;i++) {
      diff = (float) net_max_out / (float) net_hist[i][1];
      height = 34-(16 / diff);
      g15r_drawLine(canvas,x,height,x-1,last,G15_COLOR_BLACK);
      last=height;
      x++;
    }
    g15r_drawLine (canvas, 52, 0, 52, 34, G15_COLOR_BLACK);
    g15r_drawLine (canvas, 53, 0, 53, 34, G15_COLOR_BLACK);
    g15r_drawLine (canvas, 54, 0, 54, 34, G15_COLOR_BLACK);

    sprintf(tmpstr,"IN %s",show_bytes(netload.bytes_in));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 2);
    sprintf(tmpstr,"OUT %s",show_bytes(netload.bytes_out));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 26);

    sprintf(tmpstr,"%s",interface);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_LARGE, 25-(strlen(tmpstr)*9)/2, 14);
}

void draw_bat_screen(g15canvas *canvas, int all) {

	g15_stats_bat_info bats[NUM_BATS];
	long	tot_max_charge = 0;
	long	tot_cur_charge = 0;

	FILE	*fd_state;
	FILE	*fd_info;
	char	line	[MAX_LINES];
	char	value	[MAX_LINES];
	char	tmpstr	[MAX_LINES];

	int i = 0;
	for (i = 0; i < NUM_BATS; i++)
	{
		char filename[30];

		// Initialize battery state
		bats[i].max_charge = 0;
		bats[i].cur_charge = 0;
		bats[i].status = -1;

		sprintf(filename, "/proc/acpi/battery/BAT%d/state", i);
		fd_state=fopen (filename,"r");
		if (fd_state!=NULL)
		{
			while (fgets (line,MAX_LINES,fd_state)!=NULL)
			{
				// Parse the state file for battery info
				if (strcasestr (line,"remaining capacity")!=0)
				{
					strncpy ((char *)value,((char *)line)+25,5);
					bats[i].cur_charge=atoi (value);
				}
				if (strcasestr (line,"charging state")!=0)
				{
					if (strcasestr (line,"charged")!=0)
					{
						bats[i].status=0;
					}
					if (strcasestr (line," charging")!=0)
					{
						bats[i].status=1;
					}
					if (strcasestr (line,"discharging")!=0)
					{
						bats[i].status=2;
					}
				}
			}
			fclose (fd_state);
			sprintf(filename, "/proc/acpi/battery/BAT%d/info", i);
			fd_info=fopen (filename,"r");

			if (fd_info!=NULL)
			{
				while (fgets (line,MAX_LINES,fd_info)!=NULL)
				{
					// Parse the info file for battery info
					if (strcasestr (line,"last full capacity")!=0)
					{
						strncpy ((char *)value,((char *)line)+25,5);
						bats[i].max_charge=atoi (value);
					}
				}
				fclose (fd_info);
			}

			tot_cur_charge += bats[i].cur_charge;
			tot_max_charge += bats[i].max_charge;

		}
	}
        if (all) {
            g15r_clearScreen (canvas, G15_COLOR_WHITE);

            g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, 155, 4);
            g15r_renderString (canvas, (unsigned char*)"U", 1, G15_TEXT_MED, 155, 4);
            g15r_renderString (canvas, (unsigned char*)"L", 2, G15_TEXT_MED, 155, 4);
            g15r_renderString (canvas, (unsigned char*)"L", 3, G15_TEXT_MED, 155, 4);

            g15r_drawLine (canvas, VL_LEFT, 1, VL_LEFT, 32, G15_COLOR_BLACK);
            g15r_drawLine (canvas, VL_LEFT+1, 1, VL_LEFT+1, 32, G15_COLOR_BLACK);

            for (i = 0; i < NUM_BATS; i++)
            {
                    register float charge = 0;
                    register int bar_top = (i*10) + 1 + i;
                    register int bar_bottom = ((i+1)*10) + i;
                    if (bats[i].max_charge > 0)
                    {
                            charge = ((float)bats[i].cur_charge/(float)bats[i].max_charge)*100;
                    }
                    sprintf(tmpstr,"Bt%d %2.f%%", i, charge);
                    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, (i*12) + 2);
                    g15r_drawBar(canvas, BAR_START, bar_top, BAR_END, bar_bottom, G15_COLOR_BLACK, bats[i].cur_charge, bats[i].max_charge, 4);
            }

            drawBar_reversed(canvas,BAR_START,1,BAR_END,32,G15_COLOR_BLACK,100-(((float)tot_cur_charge/(float)tot_max_charge)*100),100,5);
        }

	float total_charge = 0;
	if (tot_max_charge > 0)
	{
		total_charge = ((float)tot_cur_charge/(float)tot_max_charge)*100;
	}
	sprintf (tmpstr,"Total %2.f%% | ", total_charge);

	for (i = 0; i < NUM_BATS; i++)
	{
		char extension[11];
		switch (bats[i].status)
		{
			case -1:
			{
				sprintf(extension, "Bt%d - | ", i);
				break;
			}
			case 0:
			{
				sprintf(extension, "Bt%d F | ", i);
				break;
			}
			case 1:
			{
				sprintf(extension, "Bt%d C | ", i);
				break;
			}
			case 2:
			{
				sprintf(extension, "Bt%d D | ", i);
				break;
			}
		}

		strcat (tmpstr, extension);
	}

	g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);
}

void print_info_label(g15canvas *canvas) {
    static int timer;
    int my_cycle = cycle;
    timer++;

    if (! submode) {
        my_cycle = -1;
    }

    switch (my_cycle){
        case    CYCLE_CPU_SCREEN    :
            print_sys_load_info(canvas);
            break;
        case    CYCLE_MEM_SCREEN    :
            print_mem_info(canvas);
            break;
        case    CYCLE_SWAP_SCREEN   :
            print_swap_info(canvas);
            break;
        case    CYCLE_NET_SCREEN    :
            print_net_info(canvas);
            break;
        case    CYCLE_BAT_SCREEN    :
            draw_bat_screen(canvas, 0);
            break;
        default :
            if ((timer < PAUSE)) {
                print_sys_load_info(canvas);
            } else if (timer < PAUSE_2) {
                print_mem_info(canvas);
            } else if (timer < PAUSE_3) {
                print_swap_info(canvas);
            } else if (timer < PAUSE_4) {
                draw_bat_screen(canvas, 0);
            } else if (timer < PAUSE_5) {
                print_time_info(canvas);
            } else if (have_nic) {
                print_net_info(canvas);
            } else {
                print_sys_load_info(canvas);
                timer = 0;
            }
            break;
    }
    if (timer > PAUSE_5) {
        timer=0;
    }
}

void keyboard_watch(void) {
    unsigned int keystate;
    int change   = 0;

    while(1) {
        recv(g15screen_fd,&keystate,4,0);

        if(keystate & G15_KEY_L1) {
        }
        else if(keystate & G15_KEY_L2) {
            cycle--;
            change++;
        }
        else if(keystate & G15_KEY_L3) {
            cycle++;
            change++;
        }
        else if(keystate & G15_KEY_L4) {
            mode++;
            change++;
        }
        else if(keystate & G15_KEY_L5) {
            submode++;
            change++;
        }
	if (cycle<0)
	{
		// Wrap around the apps
		cycle=MAX_SCREENS;
	}
	if (cycle>MAX_SCREENS)
	{
		//Wrap around the apps
		cycle=0;
	}

        if (mode>MAX_MODE)
        {
            mode=0;
        }

        if (submode>MAX_SUB_MODE)
        {
            submode=0;
        }

        if (change) 
        {
            pthread_cond_broadcast(&wake_now);
            change = 0;
        }
        usleep(100*900);
    }

    return;
}

void network_watch(void *iface) {
  char *interface = (char*)iface;
  int i=0;
  glibtop_netload netload;
  int mac=0;

  glibtop_get_netload(&netload,interface);
  for(i=0;i<8;i++)
    mac+=netload.hwaddress[i];
  if(!mac) {
    printf("Interface %s does not appear to exist. Net screen will be disabled.\n",interface);
    have_nic = 0;
    return; // interface probably doesn't exist - no mac address
  }

  while(1) {
    int j=0, max_in=0, max_out=0;

    if(previous_in+previous_out==0)
       goto last;
      
      net_hist[i][0] = netload.bytes_in-previous_in;
      net_hist[i][1] = netload.bytes_out-previous_out;
      if(net_scale_absolute==1) {
         net_max_in = maxi(net_max_in,netload.bytes_in-previous_in);
         net_max_out = maxi(net_max_out,netload.bytes_out-previous_out);
      } else {
        /* Try ti auto-resize the net graph */
        /* check for max value */
        for (j=0;j<MAX_NET_HIST;j++){
      	  max_in = maxi(max_in,net_hist[j][0]);
      	  max_out = maxi(max_out,net_hist[j][1]);
        }

        /* Assign new values */
        net_max_in = max_in;
        net_max_out = max_out;
    }
      net_rr_index=i;    
      i++; if(i>MAX_NET_HIST) i=0;

      last: 
      previous_in = netload.bytes_in;
      previous_out = netload.bytes_out;
      sleep (1);
      glibtop_get_netload(&netload,interface);

    }
}

/* wait for a max of <seconds> seconds.. if condition &wake_now is received leave immediately */
void g15stats_wait(int seconds) {
    pthread_mutex_t dummy_mutex;
    struct timespec timeout;
      /* Create a dummy mutex which doesn't unlock for sure while waiting. */
    pthread_mutex_init(&dummy_mutex, NULL);
    pthread_mutex_lock(&dummy_mutex);

    time(&timeout.tv_sec);
    timeout.tv_sec += seconds;
    timeout.tv_nsec = 0L;

    pthread_cond_timedwait(&wake_now, &dummy_mutex, &timeout);
    pthread_mutex_unlock(&dummy_mutex);
    pthread_mutex_destroy(&dummy_mutex);
}

int main(int argc, char *argv[]){

    g15canvas *canvas;
    pthread_t keys_thread;
    pthread_t net_thread;
    
    int i;
    int go_daemon=0;
    unsigned char interface[128];
    int unicore = 0;
    // Set the default CPU Screen mode to mem plus cpu
    mode = MODE_CPU_SUMARY;
    
    for (i=0;i<argc;i++) {
        if(0==strncmp(argv[i],"-d",2)||0==strncmp(argv[i],"--daemon",8)) {
            go_daemon=1;
        }
        if(0==strncmp(argv[i],"-u",2)||0==strncmp(argv[i],"--unicore",8)) {
            unicore=1;
        }
        if(0==strncmp(argv[i],"-nsa",4)||0==strncmp(argv[i],"--net-scale-absolute",20)) {
            net_scale_absolute=1;
        }

        if(0==strncmp(argv[i],"-h",2)||0==strncmp(argv[i],"--help",6)) {
            printf("%s (c) 2008 Mike Lampard\n",PACKAGE_NAME);
            printf("\nOptions:\n");
            printf("--daemon (-d) run in background\n");
            printf("--unicore (-u) display unicore graphs only on the CPU screen\n");
            printf("--help (-h) this help text.\n");
            printf("--interface [interface] (-i) monitor network interface [interface] ie -i eth0\n");
            printf("--net-scale-absolute (-nsa) scale net graphs against maximum speed seen.\n\tDefault is to scale fullsize, similar to apps like gkrellm.\n");
            return 0;
        }
        if(0==strncmp(argv[i],"-i",2)||0==strncmp(argv[i],"--interface",8)) {
          if(argv[i+1]!=NULL) {
            have_nic=1;
            i++;
            strncpy((char*)interface,argv[i],128);
          }
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

    glibtop_init();
    pthread_create(&keys_thread,NULL,(void*)keyboard_watch,NULL);
  
    if(have_nic==1)
      pthread_create(&net_thread,NULL,(void*)network_watch,&interface);

    while(1) {

        switch(cycle) {
            case CYCLE_CPU_SCREEN:
               draw_cpu_screen_multicore(canvas,unicore);
               print_info_label(canvas);
               break;
            case CYCLE_MEM_SCREEN:
               draw_mem_screen(canvas);
               print_info_label(canvas);
               break;
            case CYCLE_SWAP_SCREEN:
               draw_swap_screen(canvas);
               print_info_label(canvas);
               break;
            case CYCLE_NET_SCREEN:
              if(have_nic) {
                 draw_net_screen(canvas,(char*)interface);
                 print_info_label(canvas);
                 break;
              }else
                cycle++;
	    case CYCLE_BAT_SCREEN:
               draw_bat_screen(canvas,1);
    	       break;
            default:
                printf("cycle reached %i\n",cycle);
                cycle=0;
        }

        canvas->mode_xor = 0;

        g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
        g15stats_wait(1);
    }
    glibtop_close();

    close(g15screen_fd);
    free(canvas);
    return 0;  
}

