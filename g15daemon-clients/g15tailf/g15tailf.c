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

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.
        
  (c) 2008 Mike Lampard 
  Using code from tailf -> (c) 1996, 2003 Rickard E. Faith (faith@acm.org)

$Revision$ -  $Date$ $Author$

This daemon listens on localhost port 15550 for client connections,
and arbitrates LCD display.  Allows for multiple simultaneous clients.
Client screens can be cycled through by pressing the 'L1' key.

This is a simple client that tails a file and outputs to the LCD on the G15.
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

static size_t filesize(const char *filename)
{
    struct stat sb;
    
        if (!stat(filename, &sb)) return sb.st_size;
            return 0;
}
            
static void tailf(g15canvas *canvas, const char *filename, int lines,int hoffset)
{
    char **buffer;
    int  head = 0;
    int  tail = 0;
    FILE *str;
    int  i;

    if (!(str = fopen(filename, "r"))) {
	fprintf(stderr, "Cannot open \"%s\" for read\n", filename);
	perror("");
	exit(1);
    }

    buffer = malloc(lines * sizeof(*buffer));
    for (i = 0; i < lines; i++) buffer[i] = malloc(BUFSIZ + 1);

    while (fgets(buffer[tail], BUFSIZ, str)) {
	if (++tail >= lines) {
	    tail = 0;
	    head = 1;
	}
    }
    /* clean up EOL ctrl characters */
    for(i=0;i<lines;i++)
      buffer[i][strlen(buffer[i])-1]='\0';

    if (head) {
     int v=7-lines;
	for (i = tail; i < lines; i++) {
	   g15r_renderString (canvas, (unsigned char *)buffer[i]+hoffset, v++, G15_TEXT_SMALL,1,1);
	   }
	for (i = 0; i < tail; i++) {
	   g15r_renderString (canvas, (unsigned char *)buffer[i]+hoffset, v++, G15_TEXT_SMALL,1,1);
        }

    } else {
	for (i = head; i < tail; i++) 
	   g15r_renderString (canvas, (unsigned char *)buffer[i]+hoffset, i+1, G15_TEXT_SMALL,1,1);

    }

    for (i = 0; i < lines; i++) free(buffer[i]);
    free(buffer);
    fclose(str);
}

int main(int argc, char *argv[]){

    int g15screen_fd;
    unsigned int keystate;
    char pipefname[1024];
    char pipetitle[256];
    g15canvas *canvas;
    int i;
    int hoffset=0;
    int lastsize=0;
    int go_daemon=0;
    int opts=1,title=0,name=0;

    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon\n");
        return -1;
    }

    if(argc<2) {
        printf("G15tailf (c) 2008 Mike Lampard\n");
        printf("Please put the /full/file/name to read on the cmdline and (optionally) title of the screen \n");
        printf("and try again.\n");
        printf("ie. g15tailf -t [\"Message Log\"] /var/log/messages \n");
        printf("if run with -d as the first option, g15tailf will run in the background\n");
        return -1;
    }
    for(i=0;i<argc;i++) {
      if(0==strncmp(argv[i],"-d",2)) {
        go_daemon=1;
        opts++;
      }
      else if(0==strncmp(argv[i],"-t",2)) {
        strcpy(pipetitle,argv[++i]);
        title=1;
        opts+=2;
      }
    }

    strcpy(pipefname,argv[opts]);

    if(title==0)    
      strcpy(pipetitle,pipefname);    
    
    canvas = (g15canvas *) malloc (sizeof (g15canvas));
    
    if(canvas != NULL)
        g15r_initCanvas(canvas);
    else
        return -1;
    if(go_daemon==1)
      daemonise(0,0);
          
    while(1) {
        i=0;
        int size=filesize(pipefname);
        
        g15r_clearScreen (canvas, G15_COLOR_WHITE);
        if(title) {
          g15r_pixelBox (canvas, 1, 0, 159, 6,1,G15_COLOR_BLACK,1 );
          canvas->mode_xor = 1;
          g15r_renderString (canvas, (unsigned char *)pipetitle, 0, G15_TEXT_SMALL, 2, 1);
          canvas->mode_xor = 0;
        }
        recv(g15screen_fd,&keystate,4,0);

        if(size!=lastsize||keystate!=0) {
          tailf(canvas,pipefname,7-title,hoffset);
          lastsize=size;
          g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
        }

        if(keystate & G15_KEY_L2) {
            if(hoffset)
                hoffset-=5;
        }
        if(keystate & G15_KEY_L3) {
            if(hoffset<1024)
                hoffset+=5;
        }
        usleep(50000);
    }
    close(g15screen_fd);
    free(canvas);
    return 0;  
}

