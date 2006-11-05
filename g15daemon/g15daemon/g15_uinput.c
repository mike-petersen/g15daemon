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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdaemon/daemon.h>
#include <config.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#ifdef HAVE_LINUX_UINPUT_H
#include <linux/input.h>
#include <linux/uinput.h>
   
#include <libg15.h>
#include "g15daemon.h"

static int uinp_fd = -1;

extern int leaving;
extern unsigned int connected_clients;


int g15_init_uinput() {
    
    int i=0;
    struct uinput_user_dev uinp;
    static const char *uinput_device_fn[] = { "/dev/uinput", "/dev/input/uinput",0};
    
    while (uinput_device_fn[i] && (uinp_fd = open(uinput_device_fn[i],O_RDWR))<0){
        ++i;
    }
    if(uinp_fd<0){
        daemon_log(LOG_ERR,"Unable to open UINPUT device.  Please ensure the uinput driver is loaded into the kernel and that you have permission to open the device.");
        return -1;
    }

    memset(&uinp,0,sizeof(uinp));
    strncpy(uinp.name, "G15 Extra Keys", UINPUT_MAX_NAME_SIZE);

#ifdef HAVE_UINPUT_USER_DEV_ID
    uinp.id.version = 4;
    uinp.id.bustype = BUS_USB;
#else
    uinp.idversion = 4;
    uinp.idbus = BUS_USB;
#endif 

    ioctl(uinp_fd, UI_SET_EVBIT, EV_KEY);

    for (i=0; i<256; ++i)
        ioctl(uinp_fd, UI_SET_KEYBIT, i);

    write(uinp_fd, &uinp, sizeof(uinp));
    
    if (ioctl(uinp_fd, UI_DEV_CREATE))
    {
        daemon_log(LOG_ERR,"Unable to create UINPUT device.");
        return -1;
    }
    return 0;
}

void g15_exit_uinput(){
    ioctl(uinp_fd, UI_DEV_DESTROY);
    close(uinp_fd);
}


void g15_uinput_keydown(unsigned char code)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));

    event.type = EV_KEY;
    event.code = code;
    event.value = G15KEY_DOWN;
    
    write (uinp_fd, &event, sizeof(event));
}

void g15_uinput_keyup(unsigned char code)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));

    event.type = EV_KEY;
    event.code = code;
    event.value = G15KEY_UP;
    
    write (uinp_fd, &event, sizeof(event));
}

#endif 
#endif
