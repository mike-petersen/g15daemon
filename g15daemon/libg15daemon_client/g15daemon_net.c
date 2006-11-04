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

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <config.h>

#include <libg15.h>
#include "g15daemon_client.h" 

#define G15SERVER_PORT 15550
#define G15SERVER_ADDR "127.0.0.1"
int leaving = 0;

const char *g15daemon_version () {
  return VERSION;
}

int new_g15_screen(int screentype)
{
    int g15screen_fd;
    struct sockaddr_in serv_addr;

    char buffer[256];
    
    g15screen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g15screen_fd < 0) 
        return -1;
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    inet_aton (G15SERVER_ADDR, &serv_addr.sin_addr);
    serv_addr.sin_port        = htons(G15SERVER_PORT);

    if (connect(g15screen_fd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
        return -1;
    
    memset(buffer,0,256);
    if(g15_recv(g15screen_fd, buffer, 16)<0)
        return -1;
    
    /* here we check that we're really talking to the g15daemon */
    if(strcmp(buffer,"G15 daemon HELLO") != 0)
        return -1;
    if(screentype == G15_TEXTBUF) /* txt buffer - not supported yet */
        g15_send(g15screen_fd,"TBUF",4);
    else if(screentype == G15_WBMPBUF) /* wbmp buffer */
        g15_send(g15screen_fd,"WBUF",4);
    else if(screentype == G15_G15RBUF)
        g15_send(g15screen_fd,"RBUF",4);
    else 
        g15_send(g15screen_fd,"GBUF",4);
    
    return g15screen_fd;
}

int g15_close_screen(int sock) 
{
    return close(sock);
}

int g15_send(int sock, char *buf, unsigned int len)
{
    int total = 0;
    int retval = 0;
    int bytesleft = len;
    struct pollfd pfd[1];
    
    while(total < len && !leaving) {
        memset(pfd,0,sizeof(pfd));
        pfd[0].fd = sock;
        pfd[0].events = POLLOUT;
        if(poll(pfd,1,500)>0) {
            if(pfd[0].revents & POLLOUT) {
                retval = send(sock, buf+total, bytesleft, 0);
                if (retval == -1) { 
                    break; 
                }
                bytesleft -= retval;
                total += retval;
            }
        }
    }
    return retval==-1?-1:0;
} 

int g15_recv(int sock, char *buf, unsigned int len)
{
    int total = 0;
    int retval = 0;
    int bytesleft = len; 
    struct pollfd pfd[1];
    
    while(total < len  && !leaving) {
        memset(pfd,0,sizeof(pfd));
        pfd[0].fd = sock;
        pfd[0].events = POLLIN;
        if(poll(pfd,1,500)>0){
            if(pfd[0].revents & POLLIN) {
                retval = recv(sock, buf+total, bytesleft, 0);
                if (retval < 1) { 
                    break; 
                }
                total += retval;
                bytesleft -= retval;
            }
        }
    }
    return total;
} 

int g15_send_cmd (int sock, unsigned char command, unsigned char value)
{
    int retval;
    unsigned char packet[2];
    
    switch (command) {
        case G15DAEMON_KEY_HANDLER:
            if (value > G15_LED_MR)
                value = G15_LED_MR;
            packet[0] = command | value;
            retval = send( sock, packet, 1, MSG_OOB );
            break;
        case G15DAEMON_CONTRAST:
            if (value > G15_CONTRAST_HIGH)
                value = G15_CONTRAST_HIGH;
            packet[0] = command | value;
            retval = send( sock, packet, 1, MSG_OOB );
            break;
        case G15DAEMON_BACKLIGHT:
            if (value > G15_BRIGHTNESS_BRIGHT)
                value = G15_BRIGHTNESS_BRIGHT;
            packet[0] = command | value;
            retval = send( sock, packet, 1, MSG_OOB );
            break;
        case G15DAEMON_MKEYLEDS:
            if (value > G15_LED_MR)
                value = G15_LED_MR;
            packet[0] = command | value;
            retval = send( sock, packet, 1, MSG_OOB );
            break;
        case G15DAEMON_SWITCH_PRIORITIES:
            packet[0] = command;
            retval = send( sock, packet, 1, MSG_OOB );
            break;
        case G15DAEMON_GET_KEYSTATE:{
            retval = 0;
            packet[0] = command;
            send( sock, packet, 1, MSG_OOB );
            recv(sock, &retval, sizeof(retval),0);
            break;
        }
        case G15DAEMON_IS_FOREGROUND:{
            unsigned int foreground = 0;
            packet[0] = command;
            send( sock, packet, 1, MSG_OOB );
            memset(packet,0,sizeof(packet));
            recv( sock, packet, 1, 0);
            retval =  packet[0] - 48;
            break;
        }
        case G15DAEMON_IS_USER_SELECTED:{
            unsigned int chosen = 0;
            packet[0] = command;
            send( sock, packet, 1, MSG_OOB );
            memset(packet,0,sizeof(packet));
            retval = recv(sock, packet , 1,0);              
            retval = packet[0] - 48;
            break;
        }       
        default:
            return -1;    
    }
    
    return retval;       
}
