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

int g15screen_fd;
int cycle = 0;
#define MAX_NET_HIST 107

unsigned int net_hist[MAX_NET_HIST][2];
int net_rr_index=0;
unsigned long net_max_in=0;
unsigned long net_max_out=0;

unsigned long maxi(unsigned long a, unsigned long b) {
  if(a>b)
    return a;
  return b;
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

void draw_mem_screen(g15canvas *canvas) {
    glibtop_mem mem;
    char tmpstr[1024];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_mem(&mem);

    sprintf(tmpstr,"Usr %2.f%%",((float)mem.user/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 2);
    sprintf(tmpstr,"Buf %2.f%%",((float)mem.buffer/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 14);
    sprintf(tmpstr,"Che %2.f%%",((float)mem.cached/(float)mem.total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 26);

    g15r_drawBar(canvas,43,1,150,10,1,mem.user,mem.total,4);
    g15r_drawBar(canvas,43,12,150,21,1,mem.buffer,mem.total,4);
    g15r_drawBar(canvas,43,23,150,32,1,mem.cached,mem.total,4);
    drawBar_reversed(canvas,43,1,150,32,1,mem.free,mem.total,5);

    g15r_drawLine (canvas, 40, 1, 40, 32, 1);
    g15r_drawLine (canvas, 41, 1, 41, 32, 1);

    g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"R", 1, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 2, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 3, G15_TEXT_MED, 152, 4);

    sprintf(tmpstr,"Memory Used %dMb | Memory Total %dMb",(unsigned int)((mem.buffer+mem.cached+mem.user)/(1024*1024)),(unsigned int)(mem.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);

}

void draw_swap_screen(g15canvas *canvas) {
    glibtop_swap swap;
    char tmpstr[1024];

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_swap(&swap);

    g15r_renderString (canvas, (unsigned char*)"Swap", 0, G15_TEXT_MED, 1, 9);
    sprintf(tmpstr,"Used %i%%",(unsigned int)(((float)swap.used/(float)swap.total)*100));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 19);

    g15r_drawBar(canvas,43,1,150,32,1,(swap.used/1024),swap.total/1024,4);

    drawBar_reversed(canvas,43,1,150,32,1,(swap.total/1024)-(swap.used/1024),swap.total/1024,5);

    g15r_drawLine (canvas, 40, 1, 40, 32, 1);
    g15r_drawLine (canvas, 41, 1, 41, 32, 1);

    g15r_renderString (canvas, (unsigned char*)"F", 0, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"R", 1, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 2, G15_TEXT_MED, 152, 4);
    g15r_renderString (canvas, (unsigned char*)"E", 3, G15_TEXT_MED, 152, 4);

    sprintf(tmpstr,"Swap Used %dMb | Swap Avail. %dMb",(unsigned int)(swap.used/(1024*1024)),(unsigned int)(swap.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);

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

void draw_net_screen(g15canvas *canvas, char *interface) {
    int i;
    int x=0;
    char tmpstr[128];
    x=0;
    float diff=0;
    float height=0;
    float last=0;
    
    glibtop_netload netload;
    glibtop_get_netload(&netload,interface);    
    // in
    x=53;
    g15r_clearScreen (canvas, G15_COLOR_WHITE);
    for(i=net_rr_index+1;i<MAX_NET_HIST;i++) {
      diff = (float) net_max_in / (float) net_hist[i][0];
      height = 16-(16/diff);
      g15r_setPixel(canvas,x,height,1);
      g15r_drawLine(canvas,x,height,x-1,last,1);
      last=height;
      x++;
    }
    for(i=0;i<net_rr_index;i++) {
      diff = (float) net_max_in / (float) net_hist[i][0];
      height = 16-(16 / diff);
      g15r_drawLine(canvas,x,height,x-1,last,1);
      last=height;
      x++;
    }
    // out
    x=53;
    for(i=net_rr_index+1;i<MAX_NET_HIST;i++) {
      diff = (float) net_max_out / (float) net_hist[i][1];
      height = 34-(16/diff);
      g15r_setPixel(canvas,x,height,1);
      g15r_drawLine(canvas,x,height,x-1,last,1);
      last=height;
      x++;
    }
    for(i=0;i<net_rr_index;i++) {
      diff = (float) net_max_out / (float) net_hist[i][1];
      height = 34-(16 / diff);
      g15r_drawLine(canvas,x,height,x-1,last,1);
      last=height;
      x++;
    }
    g15r_drawLine (canvas, 52, 0, 52, 34, 1);
    g15r_drawLine (canvas, 53, 0, 53, 34, 1);
    g15r_drawLine (canvas, 54, 0, 54, 34, 1);

    sprintf(tmpstr,"IN %s",show_bytes(netload.bytes_in));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 2);
    sprintf(tmpstr,"OUT %s",show_bytes(netload.bytes_out));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, 1, 26);

    sprintf(tmpstr,"%s",interface);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_LARGE, 25-(strlen(tmpstr)*9)/2, 14);
    
    sprintf(tmpstr,"Peak IN %s/s|",show_bytes(net_max_in));
    strcat(tmpstr,"Peak OUT ");
    strcat(tmpstr,show_bytes(net_max_out));
    strcat(tmpstr,"/s");
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, 36);

}

void keyboard_watch(void) {
    unsigned int keystate;

    while(1) {
        recv(g15screen_fd,&keystate,4,0);

        if(keystate & G15_KEY_L1) {
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
            cycle=3;
        }
        usleep(100*900);
    }

    return;
}

void network_watch(void *iface) {
  char *interface = (char*)iface;
  int i=0;
  glibtop_netload netload;
  static unsigned long previous_in;
  static unsigned long previous_out;

  while(1) {
    glibtop_get_netload(&netload,interface);
    if(previous_in+previous_out==0)
      goto last;
      
    net_hist[i][0] = netload.bytes_in-previous_in;
    net_hist[i][1] = netload.bytes_out-previous_out;
    net_max_in = maxi(net_max_in,netload.bytes_in-previous_in);
    net_max_out = maxi(net_max_out,netload.bytes_out-previous_out);

    net_rr_index=i;    
    i++; if(i>MAX_NET_HIST) i=0;
    
    sleep (1);
    last:
    previous_in = netload.bytes_in;
    previous_out = netload.bytes_out;

  }
}

int main(int argc, char *argv[]){

    g15canvas *canvas;
    pthread_t keys_thread;
    pthread_t net_thread;
    int i;
    int go_daemon=0;
    int have_nic=0;
    unsigned char interface[128];

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
            case 0:
               draw_cpu_screen(canvas);
                break;
            case 1:   
                draw_mem_screen(canvas);
                break;
            case 2:
                draw_swap_screen(canvas);
                break;
            case 3:
                if(have_nic==1)
                  draw_net_screen(canvas,(char*)interface);
                else {
                  printf("Please set the interface on the cmdline\n");
                  cycle=0;
                }
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

