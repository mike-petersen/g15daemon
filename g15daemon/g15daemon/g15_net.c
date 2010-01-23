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

    (c) 2006-2007 Mike Lampard, Philip Lawatsch, and others

    $Revision$ -  $Date$ $Author$

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
#include <pthread.h>
#include <fcntl.h>


#include <errno.h>
#include <libg15.h>
#include "g15daemon.h"
#include <libdaemon/daemon.h>
extern int leaving;
extern unsigned int current_key_state;
extern unsigned int client_handles_keys;
extern lcd_t *keyhandler;

#ifndef SO_PRIORITY
#define SO_PRIORITY 12
#endif

void send_keystate(lcd_t *client) {
    int sock = client->connection;
    int msgret;
    if(current_key_state){
            /* send the keystate inband back to the client */
            if((msgret = send(sock,(void *)&current_key_state,sizeof(current_key_state),0))<0)
                daemon_log(LOG_WARNING,"Error in send: %s\n",strerror(errno));
            current_key_state = 0;
    }
}

static void process_client_cmds(lcdnode_t *lcdnode, int sock, unsigned int *msgbuf, unsigned int len)
{
    int msgret;
    if(msgbuf[0] == CLIENT_CMD_GET_KEYSTATE)
    { /* client wants keypresses */
        if(lcdnode->list->current == lcdnode){
            /* send the keystate inband back to the client */
            if((msgret = send(sock,(void *)&current_key_state,sizeof(current_key_state),0))<0)
                daemon_log(LOG_WARNING,"Error in send: %s\n",strerror(errno));
            current_key_state = 0;
        }
        else{
            memset(msgbuf,0,4); /* client isn't currently being displayed.. tell them nothing */
            msgret=send(sock,(void *)msgbuf,sizeof(current_key_state),0);
        }
    } else if(msgbuf[0] == CLIENT_CMD_SWITCH_PRIORITIES)
    { /* client wants to switch priorities */
        pthread_mutex_lock(&lcdlist_mutex);
        if(lcdnode->list->current != lcdnode){
            lcdnode->last_priority = lcdnode->list->current;
            lcdnode->list->current = lcdnode;
        }
        else {
            if(lcdnode->list->current == lcdnode->last_priority){
                lcdnode->list->current = lcdnode->list->current->prev;
            } else{
                if(lcdnode->last_priority != NULL) {
                    lcdnode->list->current = lcdnode->last_priority;
                    lcdnode->last_priority = NULL;
                }
                else
                    lcdnode->list->current = lcdnode->list->current->prev;
            }
        }
        pthread_mutex_unlock(&lcdlist_mutex);
    } else if(msgbuf[0] == CLIENT_CMD_IS_FOREGROUND)
    { /* client wants to know if it's currently viewable */
        pthread_mutex_lock(&lcdlist_mutex);
        if(lcdnode->list->current == lcdnode){
            msgbuf[0] = '1';
        }else{
            msgbuf[0] = '0';
        }
        pthread_mutex_unlock(&lcdlist_mutex);
        send(sock,msgbuf,1,0);
    } else if (msgbuf[0] == CLIENT_CMD_IS_USER_SELECTED)
    { /* client wants to know if it was set to foreground by the user */
        pthread_mutex_lock(&lcdlist_mutex);
        if(lcdnode->lcd->usr_foreground)  /* user manually selected this lcd */
            msgbuf[0] = '1';
        else
            msgbuf[0] = '0';
        pthread_mutex_unlock(&lcdlist_mutex);
        send(sock,msgbuf,1,0);
    } else if (msgbuf[0] & CLIENT_CMD_BACKLIGHT)
    { /* client wants to change the backlight */
        lcdnode->lcd->backlight_state = msgbuf[0]-0x80;
		daemon_log(LOG_INFO,"CLIENT_CMD_BACKLIGHT. state_changed=1");
        lcdnode->lcd->state_changed = 1;
    } else if (msgbuf[0] & CLIENT_CMD_CONTRAST)
    { /* client wants to change the LCD contrast */
        lcdnode->lcd->contrast_state = msgbuf[0]-0x40;
		daemon_log(LOG_INFO,"CLIENT_CMD_CONTRAST. state_changed=1");
        lcdnode->lcd->state_changed = 1;
    } else if (msgbuf[0] & CLIENT_CMD_MKEY_LIGHTS)
    { /* client wants to change the M-key backlights */
        lcdnode->lcd->mkey_state = msgbuf[0]-0x20;
		daemon_log(LOG_INFO,"CLIENT_CMD_MKEY_LIGHTS. state_changed=1");
        lcdnode->lcd->state_changed = 1;
    } else if (msgbuf[0] & CLIENT_CMD_KEY_HANDLER)
    { /* client wants to take control of the G&M keys */
        daemon_log(LOG_WARNING, "Client is taking over keystate");

        client_handles_keys=1;
        keyhandler = &lcdnode->lcd;
        keyhandler->connection = sock;

        daemon_log(LOG_WARNING, "Client has taken over keystate");
    }

}

/* create and open a socket for listening */
int init_sockserver(){
    int listening_socket;
    int yes=1;
    int tos = 0x18;

    struct    sockaddr_in servaddr;

    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        daemon_log(LOG_WARNING, "Unable to create socket.\n");
        return -1;
    }

    setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(listening_socket, SOL_SOCKET, SO_PRIORITY, &tos, sizeof(tos));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    inet_aton (LISTEN_ADDR, &servaddr.sin_addr);
    servaddr.sin_port        = htons(LISTEN_PORT);

    if (bind(listening_socket, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
        daemon_log(LOG_WARNING, "error calling bind()\n");
        return -1;
    }

    if (listen(listening_socket, MAX_CLIENTS) < 0 ) {
        daemon_log(LOG_WARNING, "error calling listen()\n");
        return -1;
    }

    return listening_socket;
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
        pfd[0].events = POLLOUT | POLLERR | POLLHUP | POLLNVAL;
        if(poll(pfd,1,500)>0) {
            if(pfd[0].revents & POLLOUT && !(pfd[0].revents & POLLERR || pfd[0].revents & POLLHUP || pfd[0].revents & POLLNVAL)) {
                retval = send(sock, buf+total, bytesleft, 0);
                if (retval == -1) {
                    break;
                }
                bytesleft -= retval;
                total += retval;
            }
            if((pfd[0].revents & POLLERR || pfd[0].revents & POLLHUP || pfd[0].revents & POLLNVAL)){
                retval=-1;
                break;
            }
        }
    }
    return retval==-1?-1:0;
}


int g15_recv(lcdnode_t *lcdnode, int sock, char *buf, unsigned int len)
{
    int total = 0;
    int retval = 0;
    int msgret = 0;
    int bytesleft = len;
    struct pollfd pfd[1];
    unsigned int msgbuf[20];
	int pollresult = 0;

	int flags;
	flags = fcntl(sock,F_GETFL,0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while(total < len  && !leaving) {
        memset(pfd,0,sizeof(pfd));

        pfd[0].fd = sock;
        pfd[0].events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL;
		daemon_log(LOG_INFO,"loop: g15_recv");
		pollresult = poll(pfd,1,500);
        if(pollresult > 0)
		{
			daemon_log(LOG_INFO, "Polling socket");
            if(pfd[0].revents & POLLPRI && !(pfd[0].revents & POLLERR || pfd[0].revents & POLLHUP || pfd[0].revents & POLLNVAL))
			{ /* receive out-of-band request from client and deal with it */
				daemon_log(LOG_INFO,"Reading OOB data");
                memset(msgbuf,0,20);
                msgret = recv(sock, msgbuf, 20 , MSG_OOB);
				daemon_log(LOG_INFO,"Read OOB data");
                if (msgret < 1)
				{
					daemon_log(LOG_INFO,"Breaking due to low returnvalue from recv()");
                    break;
                }
				daemon_log(LOG_INFO,"Processing commands");
				process_client_cmds(lcdnode, sock, msgbuf,len);
				daemon_log(LOG_INFO,"Processed commands");
            }
            else if(pfd[0].revents & POLLIN && !(pfd[0].revents & POLLERR || pfd[0].revents & POLLHUP || pfd[0].revents & POLLNVAL || pfd[0].revents & POLLPRI))
			{
				daemon_log(LOG_INFO,"Reading normal data");
                retval = recv(sock, buf+total, bytesleft, 0);
				daemon_log(LOG_INFO,"Reading normal data finished");
                if (retval == 0)
				{
                    break;
                }
				else if (retval == -1)
					continue;

				total += retval;
				bytesleft -= retval;
			}
			if((pfd[0].revents & POLLERR || pfd[0].revents & POLLHUP || pfd[0].revents & POLLNVAL))
			{
				daemon_log(LOG_ERR,"Socket error: %s", strerror(errno));
				// Due to polling the above should never occur!
				if (errno != EAGAIN)
				{
					retval=-1;
					break;
				}
			}
			daemon_log(LOG_INFO,"Total: %i. len: %i. Leaving: %i %i",total,len,leaving, (total < len));
		}
		else if (pollresult == 0)
		{
			daemon_log(LOG_INFO, "Polling timeout");
		}
		else
		{
			daemon_log(LOG_INFO,"Poll: %i", pollresult);
		}
    }
    return total;
}
