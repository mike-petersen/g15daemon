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

(c) 2008-2010 Mike Lampard

$Revision$ -  $Date$ $Author$

This daemon listens on localhost port 15550 for client connections,
and arbitrates LCD display.  Allows for multiple simultaneous clients.
Client screens can be cycled through by pressing the 'L1' key.

This is a simple stats client showing graphs for CPU, MEM & Swap usage, Network traffic, Temperatures, CPU Frequency and Battery life.
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

int cycle       = 0;
int info_cycle  = 0;
/** Holds the mode type variable of the application running */
int mode[MAX_SCREENS];
/** Holds the sub mode type variable of the application running */
int submode     = 1;

int info_cycle_timer    = -1;

// Summary Screen indicators count 4 or 5
int summary_rows    = 4;

int have_freq   = 2;
_Bool have_temp = 1;
_Bool have_fan  = 1;
_Bool have_bat  = 1;
_Bool have_nic  = 0;
_Bool variable_cpu = 0;

_Bool sensor_type_temp[MAX_SENSOR];
_Bool sensor_type_fan[MAX_SENSOR];

int sensor_lost_temp[MAX_SENSOR];
int sensor_lost_fan[MAX_SENSOR];
int sensor_lost_bat     = 1;

int sensor_temp_id      = 0;
int sensor_temp_main    = 0;

int sensor_fan_id       = 0;

/** Holds the number of the cpus */
int ncpu;

unsigned int net_hist[MAX_NET_HIST][2];
int net_rr_index=0;

unsigned long net_max_in    = 100;
unsigned long net_max_out   = 100;

unsigned long net_cur_in    = 0;
unsigned long net_cur_out   = 0;

float temp_tot_cur  = 1;
float temp_tot_max  = 1;

float fan_tot_cur   = 1;
float fan_tot_max   = 1;

_Bool net_scale_absolute=0;

pthread_cond_t wake_now = PTHREAD_COND_INITIALIZER;

unsigned long maxi(unsigned long a, unsigned long b) {
  if(a>b)
    return a;
  return b;
}

void format_float(char *tmpstr, char *format_less, char * format_great, float value){
    if (value < 9.9) {
        sprintf(tmpstr,format_less,value);
      } else {
        sprintf(tmpstr,format_great,(unsigned long)value);
      }
}

char * show_bytes_short(unsigned long bytes) {
    static char tmpstr[32];
    if(bytes>=1024*1024) {
      format_float(tmpstr, "%4.1fM","%4liM", (float)bytes / (1024*1024));
    }
    else if(bytes >= 1024) {
      format_float(tmpstr, "%4.1fk","%4lik", (float)bytes / 1024);
    }
    else {
        sprintf(tmpstr,"%4liB",bytes);
    }
    return tmpstr;
}


char * show_bytes(unsigned long bytes) {
    static char tmpstr[32];
    if(bytes>=1024*1024) {
      format_float(tmpstr, "%2.1fMB","%liMB", (float)bytes / (1024*1024));
    }
    else if(bytes >= 1024) {
      format_float(tmpstr, "%2.1fkB","%likB", (float)bytes / 1024);
    }
    else {
        sprintf(tmpstr,"%liB",bytes);
    }
    return tmpstr;
}

char * show_hertz_logic(int hertz, char *hz) {
    static char tmpstr[32];
    if (hertz >= 1000000) {
        format_float(tmpstr, "%3.1fG","%3.fG", (float)hertz / 1000000);
    } else if (hertz >= 1000) {
        format_float(tmpstr, "%3.1fM","%3liM", (float)hertz / 1000);
    } else {
        sprintf(tmpstr, "%3iK", hertz);
    }
    if (strlen((char *)hz) > 0) {
        strcat(tmpstr, hz);
    }
    return tmpstr;
}

char * show_hertz_short(int hertz) {
    return show_hertz_logic(hertz, "");
}

char * show_hertz(int hertz) {
    return show_hertz_logic(hertz, "Hz");
}

void print_vert_label_logic(g15canvas *canvas, char *label, unsigned int sx){
    int i;
    int len =strlen((char *)label);
    if (len > 5) {
        len = 5;
    }
    int starty = 0;
    int incy   = 0;
    switch (len){
        case    6   :
            break;
        case    5   :
            starty  = 0;
            break;
        case    4   :
            starty  = 4;
            break;
        case    3   :
            starty  = 3;
            incy    = 4;
            break;
        default :
            return;
    }
    char val[2];
    for (i=0; i<len; i++) {
        memcpy (val, &(label[i]), 1);
        val[1] = '\0';
        g15r_renderString (canvas, (unsigned char*)val, i, G15_TEXT_MED, sx, starty+(i*incy));
    }
}

void print_vert_label(g15canvas *canvas, char *label){
    print_vert_label_logic(canvas, label, TEXT_RIGHT);
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

void drawBar_both(g15canvas *canvas, int y1, int y2, int bar_current, int bar_total, int rbar_current, int rbar_total) {
    g15r_drawBar(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, bar_current, bar_total, 4);
    drawBar_reversed(canvas, BAR_START, y1, BAR_END, y2, G15_COLOR_BLACK, rbar_current, rbar_total, 5);
}

void drawLine_both(g15canvas *canvas, int y1, int y2) {
    g15r_drawLine(canvas, VL_LEFT, y1, VL_LEFT, y2, G15_COLOR_BLACK);
    g15r_drawLine(canvas, VL_LEFT + 1, y1, VL_LEFT + 1, y2, G15_COLOR_BLACK);
}

void drawAll_both(g15canvas *canvas, int y1, int y2, int bar_current, int bar_total, int rbar_current, int rbar_total) {
    drawBar_both(canvas, y1, y2, bar_current, bar_total, rbar_current, rbar_total);
    drawLine_both(canvas, y1, y2);
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

void init_cpu_count(void) {
    if ((variable_cpu) || (!ncpu)) {
        const glibtop_sysinfo *cpuinfo = glibtop_get_sysinfo();

        if(cpuinfo->ncpu == 0) {
            ncpu = 1;
        } else {
            ncpu = cpuinfo->ncpu;
        }
    }
}

int get_sysfs_value(char *filename) {
    int ret_val = -1;
    FILE *fd_main;
    static char tmpstr [MAX_LINES];

    fd_main = fopen(filename, "r");
    if (fd_main != NULL) {
        if (fgets(tmpstr, MAX_LINES, fd_main) != NULL) {
            fclose(fd_main);
            ret_val = atoi(tmpstr);
        } else
            fclose(fd_main);
    } else {
        ret_val = SENSOR_ERROR;
    }
    return ret_val;
}

int get_processor_freq(char *which, int core) {
    static char tmpstr [MAX_LINES];
    sprintf(tmpstr, "/sys/devices/system/cpu/cpu%d/cpufreq/%s", core, which);
    return get_sysfs_value(tmpstr);
}

int get_cpu_freq_cur(int core) {
    int ret_val;
    switch (have_freq) {
        case 2:
            ret_val = get_processor_freq("scaling_cur_freq", core);
            break;
        default:
            ret_val = get_processor_freq("cpuinfo_%d_freq", core);
            break;
    }
    if ((!core) && (ret_val == SENSOR_ERROR)) {
        have_freq--;
        if (have_freq < 0) {
            have_freq = 0;
        }
        printf("Frequency sensor doesn't appear to exist for core=%d . ", core);
    }
    return ret_val;
}

int get_cpu_freq_max(int core) {
    return get_processor_freq("cpuinfo_max_freq", core);
}

int get_cpu_freq_min(int core) {
    return get_processor_freq("cpuinfo_min_freq", core);
}

int get_hwmon(int sensor_id, char *sensor, char *which, int id, _Bool sensor_type) {
    char tmpstr [MAX_LINES];
    if(sensor_type) {
        sprintf(tmpstr, "/sys/class/hwmon/hwmon%d/device/%s%d_%s", sensor_id, sensor, id, which);
    } else {
        sprintf(tmpstr, "/sys/class/hwmon/hwmon%d/%s%d_%s", sensor_id, sensor, id, which);
    }
    return get_sysfs_value(tmpstr);
}

int get_sensor_cur(int id, int screen_id) {
    if (screen_id == SCREEN_TEMP) {
        return get_hwmon(sensor_temp_id, "temp", "input", id, sensor_type_temp[sensor_temp_id]);
    } else {
        return get_hwmon(sensor_fan_id, "fan", "input", id, sensor_type_fan[sensor_fan_id]);
    }
}

int get_sensor_max(int id, int screen_id) {
    if (screen_id == SCREEN_TEMP) {
        return get_hwmon(sensor_temp_id, "temp", "max", id, sensor_type_temp[sensor_temp_id]);
    } else {
        return get_hwmon(sensor_fan_id, "fan", "alarm", id, sensor_type_fan[sensor_fan_id]);
    }
}

int get_next(int sensor_id, int *sensor_lost){
    int new_sensor_id;
    new_sensor_id = sensor_id;
    do {
        new_sensor_id++;
        if (new_sensor_id >= MAX_SENSOR) {
            new_sensor_id = 0;
        }
        if (sensor_lost[new_sensor_id]){
            return new_sensor_id;
        }
    } while(sensor_id != new_sensor_id);
    return SENSOR_ERROR;
}

int get_sensors(g15_stats_info *sensors, int screen_id, _Bool *sensor_type, int *sensor_lost, int sensor_id) {
    char label[16];
    int count       = 0;

    if (screen_id == SCREEN_TEMP) {
        temp_tot_cur    = 0;
        sprintf(label, "Temperature");
    } else { //SCREEN_FAN
        fan_tot_cur     = 0;
        sprintf(label, "Fan");
    }

    for (count = 0; count < NUM_PROBES; count++) {
        if ((sensors[count].cur = get_sensor_cur(count + 1, screen_id)) == SENSOR_ERROR) {
            break;
        }
        sensors[count].max = get_sensor_max(count + 1, screen_id);

        if (screen_id == SCREEN_TEMP) {
            sensors[count].cur /= 1000;
            sensors[count].max /= 1000;
            if (temp_tot_max < sensors[count].max) {
                temp_tot_max = sensors[count].max;
            }
            if ((sensor_temp_main == (count +1)) || ((!sensor_temp_main) && (temp_tot_cur < sensors[count].cur))) {
                temp_tot_cur = sensors[count].cur;
            }
        } else { //SCREEN_FAN
            if (fan_tot_cur <sensors[count].cur) {
                fan_tot_cur = sensors[count].cur;
            }

            if (fan_tot_max < fan_tot_cur) {
                fan_tot_max = (fan_tot_cur * 1.2);
            }
        }
    }
    if ((!count) && (sensors[0].cur == SENSOR_ERROR)) {
        if (sensor_type[sensor_id]) {
            sensor_lost[sensor_id]--;
            if(sensor_lost[sensor_id] < 0) {
                sensor_lost[sensor_id] = 0;
            }
            printf("%s sensor doesn't appear to exist with id=%d .\n", label, sensor_id);
            sensor_id = get_next(sensor_id, sensor_lost);
            if (screen_id == SCREEN_TEMP) {
                sensor_temp_id = sensor_id;
            } else { //SCREEN_FAN
                sensor_fan_id = sensor_id;
            }
            if (sensor_id != SENSOR_ERROR) {
                return get_sensors(sensors, screen_id, sensor_type, sensor_lost, sensor_id);
            }
        } else {
            sensor_type[sensor_id] = 1;
            return get_sensors(sensors, screen_id, sensor_type, sensor_lost, sensor_id);
        }

        if (sensor_id == SENSOR_ERROR) {
            if (screen_id == SCREEN_TEMP) {
                have_temp = 0;
            } else { //SCREEN_FAN
                have_fan = 0;
            }
            printf("%s sensor doesn't appear to exist. %s screen will be disabled.\n", label, label);
            return 0;
        }
    }

    if (count >= NUM_PROBES) {
        count = NUM_PROBES;
    } else if(sensors[count].cur != SENSOR_ERROR) {
        count++;
    }
    return count;
}

void print_sys_load_info(g15canvas *canvas, char *tmpstr) {
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
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void print_mem_info(g15canvas *canvas, char *tmpstr) {
    glibtop_mem mem;
    glibtop_get_mem(&mem);

    sprintf(tmpstr,"Memory Used %dMB | Memory Total %dMB",(unsigned int)((mem.buffer+mem.cached+mem.user)/(1024*1024)),(unsigned int)(mem.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void print_swap_info(g15canvas *canvas, char *tmpstr) {
    glibtop_swap swap;
    glibtop_get_swap(&swap);

    sprintf(tmpstr,"Swap Used %dMB | Swap Avail. %dMB",(unsigned int)(swap.used/(1024*1024)),(unsigned int)(swap.total/(1024*1024)));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void print_net_peak_info(g15canvas *canvas, char *tmpstr) {
    sprintf(tmpstr,"Peak IN %s/s|",show_bytes(net_max_in));
    strcat(tmpstr,"Peak OUT ");
    strcat(tmpstr,show_bytes(net_max_out));
    strcat(tmpstr,"/s");
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void print_net_current_info(g15canvas *canvas, char *tmpstr) {
    sprintf(tmpstr,"Current IN %s/s|",show_bytes(net_cur_in));
    strcat(tmpstr,"Current OUT ");
    strcat(tmpstr,show_bytes(net_cur_out));
    strcat(tmpstr,"/s");
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void print_freq_info(g15canvas *canvas, char *tmpstr) {
    char proc[12];
    int core;
    init_cpu_count();
    sprintf(tmpstr, "%s", "");

    for (core = 0; (core < ncpu) && (core < 6); core++) {
        sprintf(proc, "C%d ", core);
        strcat(tmpstr,proc);
        if (ncpu < 4) {
            strcat(tmpstr, show_hertz(get_cpu_freq_cur(core)));
        } else {
            strcat(tmpstr, show_hertz_short(get_cpu_freq_cur(core)));
        }
        if ((core + 1) < ncpu) {
            strcat(tmpstr, "|");
        }
    }

    g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_SMALL, 80 - (strlen(tmpstr)*4) / 2, INFO_ROW);
}

void print_time_info(g15canvas *canvas, char *tmpstr){
    time_t now;
    time(&now);

    sprintf(tmpstr,"%s",ctime(&now));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
}

void draw_mem_screen(g15canvas *canvas, char *tmpstr) {
    glibtop_mem mem;

    glibtop_get_mem(&mem);

    int mem_total   = mem.total / 1024;
    int mem_free    = mem.free / 1024;
    int mem_user    = mem.user / 1024;
    int mem_buffer  = mem.buffer / 1024;
    int mem_cached  = mem.cached / 1024;

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    sprintf(tmpstr,"Usr %2.f%%",((float)mem_user/(float)mem_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 2);
    sprintf(tmpstr,"Buf %2.f%%",((float)mem_buffer/(float)mem_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 14);
    sprintf(tmpstr,"Che %2.f%%",((float)mem_cached/(float)mem_total)*100);
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 26);

    g15r_drawBar(canvas,BAR_START,1,BAR_END,10,G15_COLOR_BLACK,mem_user,mem_total,4);
    g15r_drawBar(canvas,BAR_START,12,BAR_END,21,G15_COLOR_BLACK,mem_buffer,mem_total,4);
    g15r_drawBar(canvas,BAR_START,23,BAR_END,BAR_BOTTOM,G15_COLOR_BLACK,mem_cached,mem_total,4);
    drawBar_reversed(canvas,BAR_START,1,BAR_END,BAR_BOTTOM,G15_COLOR_BLACK,mem_free,mem_total,5);

    drawLine_both(canvas, 1, BAR_BOTTOM);

    print_vert_label(canvas, "FREE");
}

void draw_swap_screen(g15canvas *canvas, char *tmpstr) {
    glibtop_swap swap;

    g15r_clearScreen (canvas, G15_COLOR_WHITE);

    glibtop_get_swap(&swap);

    int swap_used   = swap.used / 1024;
    int swap_total   = swap.total / 1024;

    g15r_renderString (canvas, (unsigned char*)"Swap", 0, G15_TEXT_MED, TEXT_LEFT, 9);
    sprintf(tmpstr,"Used %i%%",(unsigned int)(((float)swap_used/(float)swap_total)*100));
    g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, 19);

    drawAll_both(canvas, 1, BAR_BOTTOM, swap_used, swap_total, swap_total-swap_used, swap_total);

    print_vert_label(canvas, "FREE");
}

void print_label(g15canvas *canvas, char *tmpstr, int cur_shift) {
    g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_MED, TEXT_LEFT, cur_shift + 1);
}

void draw_summary_sensors_logic(g15canvas *canvas, char *tmpstr, g15_stats_info *sensors, 
        char *label, int text_shift, int y1, int y2, int move, int cur_shift, int shift, int count, float tot_cur, float tot_max) {

    if (count) {
        int j = 0;
        int rest;
        int step = (int) (y2 / count);
        rest = y2 - (step * count);
        int y = cur_shift + y1;
        int last_y;
        for (j = 0; j < count; j++) {
            last_y = y;
            y += step;
            if (rest > 0) {
                rest--;
                y++;
            }
            drawBar_both(canvas, last_y + move, y + move, sensors[j].cur + 1, tot_max, tot_max - sensors[j].cur, tot_max);
        }
        drawLine_both(canvas, cur_shift + y1 + move, y + move);

        sprintf(tmpstr, label, tot_cur);
        print_label(canvas, tmpstr, text_shift);
    }
}

void draw_summary_screen(g15canvas *canvas, char *tmpstr, int y1, int y2, int move, int shift, int text_shift, int id) {
    // Memory section
    glibtop_mem mem;
    glibtop_get_mem(&mem);

    int mem_total = (mem.total / 1024);
    int mem_used = mem_total - (mem.free / 1024);

    int cur_shift = shift * id;

    if (summary_rows > 4){
        if (shift > 7) {
            if (id == 2) {
                y2      = 4;
                shift   = 6;
            } else {
                y2      = 5;
                shift   = 7;
            }
        }
    }

    int y;
    int count;

    // Memory section
    sprintf(tmpstr, "MEM %3.f%%", ((float) (mem_used) / (float) mem_total)*100);
    print_label(canvas, tmpstr, text_shift * id);

    drawAll_both(canvas, cur_shift + y1 + move, cur_shift + y2 + move, mem_used + 1, mem_total, mem_total - mem_used, mem_total);

    id++;
    cur_shift += shift;

    // Network section
    if (have_nic) {
        y = y2 / 2;

        drawLine_both(canvas, cur_shift + y1 + move, cur_shift + y2 + move);
        drawBar_both(canvas, cur_shift + y1 + move, cur_shift + y + move, net_cur_in + 1, net_max_in, net_max_in - net_cur_in, net_max_in);

        
        drawBar_both(canvas, cur_shift + y + move, cur_shift + y2 + move, net_cur_out + 1, net_max_out, net_max_out - net_cur_out, net_max_out);

        if (net_cur_in > net_cur_out) {
            sprintf(tmpstr, "IN %s", show_bytes_short((int) net_cur_in));
        } else {
            sprintf(tmpstr, "OUT%s", show_bytes_short((int) net_cur_out));
        }
        print_label(canvas, tmpstr, text_shift * id);

        id++;
        cur_shift += shift;
    }
    if ((have_temp) || (have_fan)) {
        g15_stats_info sensors[NUM_PROBES];
        // Temperature section
        if ((have_temp) && (id < summary_rows)) {
            count = get_sensors(sensors, SCREEN_TEMP, sensor_type_temp, sensor_lost_temp, sensor_temp_id);
            if ((count) && (have_temp)) {
                draw_summary_sensors_logic(canvas, tmpstr, sensors, "TEM %3.f\xb0", text_shift * id, y1, y2, move, cur_shift, shift, count, temp_tot_cur, temp_tot_max);
                id++;
                cur_shift += shift;
            }
        }

        // Fan section
        if ((have_fan) && (id < summary_rows)) {
            count = get_sensors(sensors, SCREEN_FAN, sensor_type_fan, sensor_lost_fan, sensor_fan_id);
            if ((count) && (have_fan)) {
                draw_summary_sensors_logic(canvas, tmpstr, sensors, "RPM%5.f", text_shift * id, y1, y2, move, cur_shift, shift, count, fan_tot_cur, fan_tot_max);
                id++;
                cur_shift += shift;
            }
        }
    }

    // Swap section
    if (id < summary_rows) {
        glibtop_swap swap;

        glibtop_get_swap(&swap);

        int swap_used = swap.used / 1024;
        int swap_total = swap.total / 1024;

        drawAll_both(canvas, cur_shift + y1 + move, cur_shift + y2 + move, swap_used, swap_total, swap_total - swap_used, swap_total);

        sprintf(tmpstr, "Swp %3i%%", (unsigned int) (((float) swap_used / (float) swap_total)*100));
        print_label(canvas, tmpstr, text_shift * id);

        id++;
        cur_shift += shift;
    }
}
/* draw cpu screen.  if drawgraph = 0 then no graph is drawn */
void draw_cpu_screen_unicore_logic(g15canvas *canvas, glibtop_cpu cpu, char *tmpstr, int drawgraph, int printlabels, int cpuandmemory) {
    int total,user,nice,sys,idle;
    int b_total,b_user,b_nice,b_sys,b_idle,b_irq,b_iowait;
    static int last_total,last_user,last_nice,last_sys,last_idle,last_iowait,last_irq;

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
        g15r_drawBar(canvas,BAR_START,23,BAR_END,BAR_BOTTOM,G15_COLOR_BLACK,b_nice+1,b_total,4);
        drawBar_reversed(canvas,BAR_START,1,BAR_END,BAR_BOTTOM,G15_COLOR_BLACK,b_idle+1,b_total,5);

        drawLine_both(canvas, 1, BAR_BOTTOM);
    }
    if (cpuandmemory) {
        print_vert_label(canvas, "TOTAL");

        sprintf(tmpstr,"CPU %3.f%%",((float)(b_total-b_idle)/(float)b_total)*100);
        print_label(canvas, tmpstr, 0);
    } else if ((cycle == SCREEN_FREQ) && (mode[SCREEN_FREQ]) && (have_freq)) {
        print_vert_label(canvas, "FREQ");
    } else {
        print_vert_label(canvas, "Idle");
    }
}

void draw_cpu_screen_unicore(g15canvas *canvas, char *tmpstr, int drawgraph, int printlabels) {
    glibtop_cpu cpu;
    glibtop_get_cpu(&cpu);

    draw_cpu_screen_unicore_logic(canvas, cpu, tmpstr, drawgraph, printlabels, 0);
}

void draw_cpu_screen_multicore(g15canvas *canvas, char *tmpstr, int unicore) {
    glibtop_cpu cpu;
    int core,ncpumax;
    int divider = 0;
        
    int total,user,nice,sys,idle;
    int b_total,b_user,b_nice,b_sys,b_idle,b_irq,b_iowait;
    int sub_val;
    static int last_total[GLIBTOP_NCPU],last_user[GLIBTOP_NCPU],last_nice[GLIBTOP_NCPU],
            last_sys[GLIBTOP_NCPU],last_idle[GLIBTOP_NCPU],last_iowait[GLIBTOP_NCPU],last_irq[GLIBTOP_NCPU];

    init_cpu_count();

    ncpumax = ncpu;

    glibtop_get_cpu(&cpu);

    switch (cycle) {
        case    SCREEN_SUMMARY  :
            draw_cpu_screen_unicore_logic(canvas, cpu, tmpstr, 0, 0, 1);
            break;
        case SCREEN_CPU :
            if ((unicore) || (ncpu==1)) {
               draw_cpu_screen_unicore_logic(canvas, cpu, tmpstr, 1, 1, 0);
                return;
            }
            draw_cpu_screen_unicore_logic(canvas, cpu, tmpstr, 0, 1, 0);
            break;
        case SCREEN_FREQ   :
            draw_cpu_screen_unicore_logic(canvas, cpu, tmpstr, 0, 0, 0);
            break;
    }

    int y1=0, y2=BAR_BOTTOM;
    int shift = 12;
    int shift2 = 24;
    int current_value=1;
    int freq_cur = 1, freq_total = 1;
    int freq_sum = 0;

    float result;

    int spacer = 1;
    int height = 9;
    int move   = 0;
    switch (cycle) {
        case    SCREEN_FREQ    :
            if(ncpu > 11){
                spacer = 0;
            }
            height = 36;
            break;
        case    SCREEN_CPU :
            if(ncpu > 4){
                spacer = 0;
            }
            height = 12;
            break;
        case    SCREEN_SUMMARY :
            spacer = 0;
            if (!mode[SCREEN_SUMMARY]) {
                summary_rows = 5;
                switch (ncpu) {
                    case 1  :
                    case 2  :
                    case 3  :
                    case 5  :
                    case 7  :
                        move    = 1;
                        height  = 6;
                        break;
                    default :
                        height  = 8;
                        break;
                }
            } else {
                summary_rows = 4;
                switch (ncpu) {
                    case 3  :
                    case 5  :
                        move    = 1;
                        break;
                }
                height  = 8;
            }

            shift   = height + 1;
            shift2  = (2 * shift);
            break;
    }

    ncpumax = height;
    if (ncpumax < ncpu) {
        height = (height - ((ncpumax - 1) * spacer)) / (ncpumax);
    } else {
        height = (height - ((ncpu - 1) * spacer)) / (ncpu);
    }

    if (height < 1) {
        height = 1;
    }

    for(core = 0; ((core < ncpu) && (core < ncpumax)); core++) {
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

        y1 = (core * height) + (core * spacer);
        y2 = y1 + height - 1;

        switch (cycle) {
            case SCREEN_FREQ:
                if ((mode[SCREEN_FREQ]) && (have_freq)) {
                    freq_cur = get_cpu_freq_cur(core);
                    if (core < 6) {
                        result = ((float) (b_total - b_idle) / (float) b_total)*100;
                        if (result < 100.0) {
                            sprintf(tmpstr, "%2.f%% %s", result, show_hertz_short(freq_cur));
                        } else {
                            sprintf(tmpstr, "%3.f%%%s", result, show_hertz_short(freq_cur));
                        }
                        if (ncpu < 5) {
                            g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_MED, 1, y1 + 1);
                        } else {
                            g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_SMALL, 1, core * 6);
                        }
                    }
                    freq_total = get_cpu_freq_max(core);
                    drawBar_both(canvas, y1, y2, freq_cur, freq_total, freq_total - freq_cur, freq_total);
                    y1 = 0;
                } else {
                    if (core < 6) {
                        if (have_freq) {
                            freq_cur = get_cpu_freq_cur(core);
                            sprintf(tmpstr, "%s%3.f%%", show_hertz_short(freq_cur), ((float) (b_total - b_idle) / (float) b_total)*100);
                        } else {
                            sprintf(tmpstr, "CPU%.f%3.f%%", (float) core, ((float) (b_total - b_idle) / (float) b_total)*100);
                        }
                        if (ncpu < 5) {
                            g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_MED, 1, y1 + 1);
                        } else {
                            g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_SMALL, 1, core * 6);
                        }
                    }
                    current_value = b_total - b_idle;
                    drawBar_both(canvas, y1, y2, current_value, b_total, b_total - current_value, b_total);
                    y1 = 0;
                }
                break;
            case SCREEN_CPU:
                if (mode[SCREEN_CPU]) {
                    divider = 9 / ncpu;
                    sub_val = divider * core;
                    g15r_drawBar(canvas, BAR_START, sub_val, BAR_END, divider + sub_val, G15_COLOR_BLACK, b_user + 1, b_total, 4);
                    g15r_drawBar(canvas, BAR_START, shift + sub_val, BAR_END, shift + divider + sub_val, G15_COLOR_BLACK, b_sys + 1, b_total, 4);
                    y1 = 0;
                    y2 = shift2 + divider + sub_val;
                    g15r_drawBar(canvas, BAR_START, shift2 + sub_val, BAR_END, y2, G15_COLOR_BLACK, b_nice + 1, b_total, 4);

                    divider = y2 / ncpu;
                    drawBar_reversed(canvas, BAR_START, sub_val, BAR_END, y2, G15_COLOR_BLACK, b_idle + 1, b_total, 5);
                } else {
                    current_value = b_total - b_idle;
                    drawBar_both(canvas, y1, y2, current_value, b_total, b_total - current_value, b_total);

                    drawBar_both(canvas, shift + y1, shift + y2, b_sys + 1, b_total, b_total - b_sys, b_total);

                    y2 += shift2;
                    drawBar_both(canvas, shift2 + y1, y2, b_nice + 1, b_total, b_total - b_nice, b_total);

                    y1 = 0;
                }
                break;
            case SCREEN_SUMMARY:
                current_value = b_total - b_idle;
                drawBar_both(canvas, y1 + move, y2 + move, current_value, b_total, b_total - current_value, b_total);

                if (have_freq) {
                    freq_cur = get_cpu_freq_cur(core);
                    freq_total = get_cpu_freq_max(core);
                    freq_sum = maxi(freq_sum, freq_cur);
                    drawBar_both(canvas, shift + y1 + move, shift + y2 + move, freq_cur, freq_total, freq_total - freq_cur, freq_total);
                }

                y1 = 0;
                break;
        }
    }

    drawLine_both(canvas, y1 + move, y2 + move);

    if (cycle == SCREEN_SUMMARY) {
        int text_shift;
        if (summary_rows > 4) {
            text_shift = 7;
        } else {
            text_shift = 9;
        }
        if (have_freq) {
            drawLine_both(canvas, shift + y1 + move, shift + y2 + move);

            sprintf(tmpstr, "FRQ %s", show_hertz_short((int) freq_sum));

            print_label(canvas, tmpstr, text_shift);

            draw_summary_screen(canvas, tmpstr, y1, y2, move, shift, text_shift, 2);
        } else {
            draw_summary_screen(canvas, tmpstr, y1, y2, move, shift, text_shift, 1);
        }
    }
}

void draw_net_screen(g15canvas *canvas, char *tmpstr, char *interface) {
    int i;
    int x=0;

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

    if (net_scale_absolute) {
        print_vert_label_logic(canvas, "ABS  ", 47);
    } else {
        print_vert_label_logic(canvas, "REL  ", 47);
    }
}

void draw_bat_screen(g15canvas *canvas, char *tmpstr, int all) {

	g15_stats_bat_info bats[NUM_BATS];
	long	tot_max_charge = 0;
	long	tot_cur_charge = 0;

	FILE	*fd_state;
	FILE	*fd_info;
	char	line	[MAX_LINES];
	char	value	[MAX_LINES];

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

		} else {
                    break;
                }
	}

        if (i) {
            sensor_lost_bat = RETRY_COUNT;
        } else {
            sensor_lost_bat--;
            if (sensor_lost_bat <= 0) {
                printf("Battery sensor doesn't appear to exist. Battery screen will be disabled.\n");
                have_bat = 0;
                return;
            }
        }

        if (all) {
            g15r_clearScreen (canvas, G15_COLOR_WHITE);

            print_vert_label(canvas, "FULL");

            drawLine_both(canvas, 1, BAR_BOTTOM);

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

            drawBar_reversed(canvas,BAR_START,1,BAR_END,BAR_BOTTOM,G15_COLOR_BLACK,100-(((float)tot_cur_charge/(float)tot_max_charge)*100),100,5);
        }

        if ((!all) || (info_cycle == SCREEN_BAT)) {
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

            g15r_renderString (canvas, (unsigned char*)tmpstr, 0, G15_TEXT_SMALL, 80-(strlen(tmpstr)*4)/2, INFO_ROW);
        }
}

void  draw_g15_stats_info_screen_logic(g15canvas *canvas, char *tmpstr, int all, int screen_type,
        g15_stats_info *probes, int count, float tot_max, int *sensor_lost, int sensor_id,
        char *vert_label, char *format_main, char *format_bottom) {
    
    if ((!count) || (probes[0].cur == SENSOR_ERROR)) {
        return;
    } else {
        sensor_lost[sensor_id] = RETRY_COUNT;
    }

    int j = 0;

    int shift;
    shift = 32 / count;

    if (count) {
        if (all) {
            g15r_clearScreen(canvas, G15_COLOR_WHITE);
            print_vert_label(canvas, vert_label);

            for (j = 0; j < count; j++) {
                register int bar_top = (j * shift) + 1 + j;
                register int bar_bottom = ((j + 1)*shift) + j;
                sprintf(tmpstr, format_main, j + 1, probes[j].cur);
                g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_MED, 1, bar_top + 1);
                drawBar_both(canvas, bar_top, bar_bottom, probes[j].cur + 1, tot_max, tot_max - probes[j].cur, tot_max);
            }
            drawLine_both(canvas, 1, ((j*shift) + j-1));
        }

        if ((!all) || (info_cycle == screen_type)) {
            char extension[16];
            tmpstr[0] = '\0';
            for (j = 0; j < count; j++) {
                sprintf(extension, format_bottom, j + 1, probes[j].cur);
                if (j) {
                    strcat(tmpstr, "| ");
                }
                strcat(tmpstr, extension);
            }
            g15r_renderString(canvas, (unsigned char*) tmpstr, 0, G15_TEXT_SMALL, 80 - (strlen(tmpstr)*4) / 2, INFO_ROW);
        }
    }
}

void draw_g15_stats_info_screen(g15canvas *canvas, char *tmpstr, int all, int screen_type) {
    static g15_stats_info probes[NUM_PROBES];
    int count;
    switch (screen_type) {
        case SCREEN_TEMP:
            count = get_sensors(probes, SCREEN_TEMP, sensor_type_temp, sensor_lost_temp, sensor_temp_id);
            draw_g15_stats_info_screen_logic(canvas, tmpstr, all, screen_type, probes, 
                    count, temp_tot_max, sensor_lost_temp, sensor_temp_id, "TEMP", "t-%d %3.f\xb0", "temp%d %1.f\xb0 ");
            break;
        case SCREEN_FAN:
            count = get_sensors(probes, SCREEN_FAN, sensor_type_fan, sensor_lost_fan, sensor_fan_id);
            draw_g15_stats_info_screen_logic(canvas, tmpstr, all, screen_type, probes, 
                    count, fan_tot_max, sensor_lost_fan, sensor_fan_id, "RPM", "F-%d%5.f", "Fan%d %1.f ");
            break;
        default:
            break;
    }
}

void calc_info_cycle(void) {
    info_cycle = cycle;
    info_cycle_timer++;

    if (!submode) {
        switch ((int) (info_cycle_timer / PAUSE)) {
            case SCREEN_SUMMARY:
                info_cycle = SCREEN_SUMMARY;
                break;
            case SCREEN_CPU:
                info_cycle = SCREEN_CPU;
                break;
            case SCREEN_FREQ:
                if (have_freq) {
                    info_cycle = SCREEN_FREQ;
                    break;
                }
                info_cycle_timer += PAUSE;
            case SCREEN_MEM:
                info_cycle = SCREEN_MEM;
                break;
            case SCREEN_SWAP:
                info_cycle = SCREEN_SWAP;
                break;
            case SCREEN_NET:
                if (have_nic) {
                    info_cycle = SCREEN_NET;
                    break;
                }
                info_cycle_timer += PAUSE;
            case SCREEN_BAT:
                if (have_bat) {
                    info_cycle = SCREEN_BAT;
                    break;
                }
                info_cycle_timer += PAUSE;
            case SCREEN_TEMP:
                if (have_temp) {
                    info_cycle = SCREEN_TEMP;
                    break;
                }
                info_cycle_timer += PAUSE;
            case SCREEN_FAN:
                if (have_fan) {
                    info_cycle = SCREEN_FAN;
                    break;
                }
                info_cycle_timer += PAUSE;
            case SCREEN_NET2:
                if (have_nic) {
                    info_cycle = SCREEN_NET2;
                    break;
                }
                info_cycle_timer += PAUSE;
            default:
                info_cycle_timer = 0;
                info_cycle = SCREEN_SUMMARY;
                break;
        }
    }
}

void print_info_label(g15canvas *canvas, char *tmpstr) {
    switch (info_cycle) {
        case SCREEN_SUMMARY :
            print_time_info(canvas, tmpstr);
            break;
        case SCREEN_CPU :
            print_sys_load_info(canvas, tmpstr);
            break;
        case SCREEN_FREQ   :
            print_freq_info(canvas, tmpstr);
            break;
        case SCREEN_MEM :
            print_mem_info(canvas, tmpstr);
            break;
        case SCREEN_SWAP    :
            print_swap_info(canvas, tmpstr);
            break;
        case SCREEN_NET :
            print_net_peak_info(canvas, tmpstr);
            break;
        case SCREEN_BAT :
            if (cycle != SCREEN_BAT) {
                draw_bat_screen(canvas, tmpstr, 0);
            }
            break;
        case SCREEN_TEMP    :
            if (cycle != SCREEN_TEMP) {
                draw_g15_stats_info_screen(canvas, tmpstr, 0, SCREEN_TEMP);
            }
            break;
        case SCREEN_FAN    :
            if (cycle != SCREEN_FAN) {
                draw_g15_stats_info_screen(canvas, tmpstr, 0, SCREEN_FAN);
            }
            break;
        case SCREEN_NET2    :
            print_net_current_info(canvas, tmpstr);
            break;
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
            change = CHANGE_DOWN;
        }
        else if(keystate & G15_KEY_L3) {
            cycle++;
            change = CHANGE_UP;
        }
        else if(keystate & G15_KEY_L4) {
            mode[cycle]++;
            change = CHANGE_MODE;
        }
        else if(keystate & G15_KEY_L5) {
            submode++;
            change = CHANGE_SUBMODE;
        }

        if (change) {
            switch (change) {
                case CHANGE_UP :
                    if (cycle > MAX_SCREENS) {
                        cycle = 0;
                    }
                    switch (cycle) {
                        case SCREEN_NET:
                            if (have_nic) {
                                break;
                            }
                            cycle++;
                        case SCREEN_BAT:
                            if (have_bat) {
                                break;
                            }
                            cycle++;
                        case SCREEN_TEMP:
                            if (have_temp) {
                                break;
                            }
                            cycle++;
                        case SCREEN_FAN:
                            if (have_fan) {
                                break;
                            }
                            cycle = 0;
                            break;
                    }
                    info_cycle_timer = cycle * PAUSE;
                    break;
                case CHANGE_DOWN :
                    if (cycle < 0) {
                        cycle = MAX_SCREENS;
                    }
                    switch (cycle) {
                        case SCREEN_FAN:
                            if (have_fan) {
                                break;
                            }
                            cycle--;
                        case SCREEN_TEMP:
                            if (have_temp) {
                                break;
                            }
                            cycle--;
                        case SCREEN_BAT:
                            if (have_bat) {
                                break;
                            }
                            cycle--;
                        case SCREEN_NET:
                            if (have_nic) {
                                break;
                            }
                            cycle--;
                            break;
                    }
                    info_cycle_timer = cycle * PAUSE;
                    break;
                case CHANGE_MODE :
                    switch (cycle) {
                        case SCREEN_NET:
                            if (have_nic) {
                                net_scale_absolute ^= 1;
                            } else {
                                change = 0;
                            }
                            break;
                        case SCREEN_TEMP:
                            sensor_temp_id = get_next(sensor_temp_id, sensor_lost_temp);
                            if (sensor_temp_id == SENSOR_ERROR) {
                                sensor_temp_id = 0;
                            }
                            break;
                        case SCREEN_FAN:
                            sensor_fan_id = get_next(sensor_fan_id, sensor_lost_fan);
                            if (sensor_fan_id == SENSOR_ERROR) {
                                sensor_fan_id = 0;
                            }
                            break;
                        case    SCREEN_SWAP:
                        case    SCREEN_MEM:
                        case    SCREEN_BAT:
                            change = 0;
                            break;
                    }
                    break;
            }
            if (cycle > MAX_SCREENS) {
                //Wrap around the apps
                cycle = 0;
            }

            if (mode[cycle] > MAX_MODE) {
                mode[cycle] = 0;
            }

            if (submode > MAX_SUB_MODE) {
                submode = 0;
            }

            if (change) {
                pthread_cond_broadcast(&wake_now);
            }
            change = 0;
        }
        usleep(100 * 900);
    }

    return;
}

void network_watch(void *iface) {
  char *interface = (char*)iface;
  static unsigned long previous_in;
  static unsigned long previous_out;
  int i=0, j = 0, max_in = 0, max_out = 0;
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

    while (1) {
        j = 0, max_in = 0, max_out = 0;

        if (previous_in + previous_out) {
            net_cur_in = netload.bytes_in - previous_in;
            net_cur_out = netload.bytes_out - previous_out;
            net_hist[i][0] = net_cur_in;
            net_hist[i][1] = net_cur_out;

            if (net_scale_absolute==1) {
                net_max_in = maxi(net_max_in, net_cur_in);
                net_max_out = maxi(net_max_out, net_cur_out);
            } else {
                /* Try ti auto-resize the net graph */
                /* check for max value */
                for (j = 0; j < MAX_NET_HIST; j++) {
                    max_in = maxi(max_in, net_hist[j][0]);
                    max_out = maxi(max_out, net_hist[j][1]);
                }

                /* Assign new values */
                net_max_in = max_in;
                net_max_out = max_out;
            }
            net_rr_index = i;
            i++;
            if (i > MAX_NET_HIST) i = 0;
        }
        previous_in = netload.bytes_in;
        previous_out = netload.bytes_out;
        sleep(1);
        glibtop_get_netload(&netload, interface);
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
    static char tmpstr[MAX_LINES];
    int unicore = 0;
    
    for (i=0;i<argc;i++) {
        if(0==strncmp(argv[i],"-d",2)||0==strncmp(argv[i],"--daemon",8)) {
            go_daemon=1;
        }
        if(0==strncmp(argv[i],"-u",2)||0==strncmp(argv[i],"--unicore",9)) {
            unicore=1;
        }
        if(0==strncmp(argv[i],"-nsa",4)||0==strncmp(argv[i],"--net-scale-absolute",20)) {
            net_scale_absolute=1;
        }

        if(0==strncmp(argv[i],"-df",3)||0==strncmp(argv[i],"--disable-freq",14)) {
            have_freq=0;
        }

        if(0==strncmp(argv[i],"-rc",3)||0==strncmp(argv[i],"--info-rotate",13)) {
            submode = 0;
        }

        if(0==strncmp(argv[i],"-vc",3)||0==strncmp(argv[i],"--variable-cpu",14)) {
            variable_cpu = 1;
        }

        if(0==strncmp(argv[i],"-h",2)||0==strncmp(argv[i],"--help",6)) {
            printf("%s %s - (c) 2008-2010 Mike Lampard, Piotr Czarnecki\n",PACKAGE_NAME,VERSION);
            printf("Usage: %s [Options]\n", PACKAGE_NAME);
            printf("Options:\n");
            printf("--daemon (-d) run in background\n");
            printf("--unicore (-u) display unicore graphs only on the CPU screen\n");
            printf("--help (-h) this help text.\n");
            printf("--interface [interface] (-i) monitor network interface [interface] ie -i eth0\n");
            printf("--temperature [id] (-t) monitor temperatures sensor [id] ie -t 1\n"
                    "\t[id] should point to sysfs path /sys/class/hwmon/hwmon[id]/device/temp1_input\n");
            printf("--global-temp [id] (-gt) force to show temperature [id] in place of the maximal one\n"
                    "\ton the Summary Screen ie -gt 1\n"
                    "\t[id] should point to sysfs path /sys/class/hwmon/hwmon../device/temp[id]_input\n");
            printf("--fan [id] (-f) monitor fans sensor [id] ie -f 1\n"
                    "\t[id] should point to sysfs path /sys/class/hwmon/hwmon[id]/device/fan1_input\n");
            printf("--net-scale-absolute (-nsa) scale net graphs against maximum speed seen.\n"
                    "\tDefault is to scale fullsize, similar to apps like gkrellm.\n");
            printf("--info-rotate (-ir) enable the bottom info bar content rotate.\n");
            printf("--variable-cpu (-vc) the cpu cores will be calculated every time (for systems with the cpu hotplug).\n");
            printf("--disable-freq (-df) disable monitoring CPUs frequencies.\n\n");
            return 0;
        }
        if(0==strncmp(argv[i],"-i",2)||0==strncmp(argv[i],"--interface",11)) {
          if(argv[i+1]!=NULL) {
            have_nic=1;
            i++;
            strncpy((char*)interface,argv[i],128);
          }
        }
        if(0==strncmp(argv[i],"-t",2)||0==strncmp(argv[i],"--temperature",13)) {
          if(argv[i+1]!=NULL) {
            i++;
            sensor_temp_id = atoi(argv[i]);
            if (sensor_temp_id >= MAX_SENSOR) {
                sensor_temp_id = 0;
            }
          }
        }

        if(0==strncmp(argv[i],"-gt",3)||0==strncmp(argv[i],"--global-temp",13)) {
          if(argv[i+1]!=NULL) {
            i++;
            sensor_temp_main = atoi(argv[i]);
          }
        }

        if(0==strncmp(argv[i],"-f",2)||0==strncmp(argv[i],"--fan",5)) {
          if(argv[i+1]!=NULL) {
            i++;
            sensor_fan_id = atoi(argv[i]);
            if (sensor_fan_id >= MAX_SENSOR) {
                sensor_fan_id = 0;
            }
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

    for (i=0;i<MAX_SENSOR;i++) {
        sensor_lost_fan[i] = 1;
        sensor_lost_temp[i] = 1;
    }

    int cycle_old   = cycle;
    while(1) {
        calc_info_cycle();
        switch (cycle) {
            case SCREEN_SUMMARY:
            case SCREEN_CPU:
            case SCREEN_FREQ:
                draw_cpu_screen_multicore(canvas, tmpstr, unicore);
                break;
            case SCREEN_MEM:
                draw_mem_screen(canvas, tmpstr);
                break;
            case SCREEN_SWAP:
                draw_swap_screen(canvas, tmpstr);
                break;
            case SCREEN_NET:
            case SCREEN_BAT:
            case SCREEN_TEMP:
            case SCREEN_FAN:
                if ((cycle_old != cycle) && ((!cycle_old) ^ (cycle_old < cycle))) {
                    switch (cycle) {
                        case SCREEN_FAN:
                            if (have_fan) {
                                draw_g15_stats_info_screen(canvas, tmpstr, 1, SCREEN_FAN);
                                if (have_fan) {
                                    break;
                                }
                            }
                            cycle--;
                            info_cycle  = cycle;
                        case SCREEN_TEMP:
                            if (have_temp) {
                                draw_g15_stats_info_screen(canvas, tmpstr, 1, SCREEN_TEMP);
                                if (have_temp) {
                                    break;
                                }
                            }
                            cycle--;
                            info_cycle  = cycle;
                        case SCREEN_BAT:
                            if (have_bat) {
                                draw_bat_screen(canvas, tmpstr, 1);
                                if (have_bat) {
                                    break;
                                }
                            }
                            cycle--;
                            info_cycle  = cycle;
                        case SCREEN_NET:
                            if (have_nic) {
                                draw_net_screen(canvas, tmpstr, (char*) interface);
                                if (have_nic) {
                                    break;
                                }
                            }
                            cycle--;
                            info_cycle  = cycle;
                        case SCREEN_SWAP:
                            draw_swap_screen(canvas, tmpstr);
                            break;
                    }
                } else {
                    switch (cycle) {
                        case SCREEN_NET:
                            if (have_nic) {
                                draw_net_screen(canvas, tmpstr, (char*) interface);
                                break;
                            }
                            cycle++;
                            info_cycle  = cycle;
                        case SCREEN_BAT:
                            if (have_bat) {
                                draw_bat_screen(canvas, tmpstr, 1);
                                if (have_bat) {
                                    break;
                                }
                            }
                            cycle++;
                            info_cycle  = cycle;
                        case SCREEN_TEMP:
                            if (have_temp) {
                                draw_g15_stats_info_screen(canvas, tmpstr, 1, SCREEN_TEMP);
                                if (have_temp) {
                                    break;
                                }
                            }
                            cycle++;
                            info_cycle  = cycle;
                        case SCREEN_FAN:
                            if (have_fan) {
                                draw_g15_stats_info_screen(canvas, tmpstr, 1, SCREEN_FAN);
                                if (have_fan) {
                                    break;
                                }
                            }
                            cycle++;
                            info_cycle  = cycle;
                    }
                }
                if (cycle <= MAX_SCREENS) {
                    break;
                }
            default:
                draw_cpu_screen_multicore(canvas, tmpstr, unicore);
                cycle = 0;
                info_cycle  = cycle;
                break;
        }
        cycle_old   = cycle;
        print_info_label(canvas, tmpstr);

        canvas->mode_xor = 0;

        g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
        g15stats_wait(1);
    }
    glibtop_close();

    close(g15screen_fd);
    free(canvas);
    return 0;  
}
