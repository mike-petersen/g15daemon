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

(c) 2007-2008 Mike Lampard 

$Revision$ -  $Date$ $Author$

This daemon listens on localhost port 15550 for client connections,
and arbitrates LCD display.  Allows for multiple simultaneous clients.
Client screens can be cycled through by pressing the 'L1' key.

This is a simple client that spawns a process and redirects output to the LCD on the G15.
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


int main(int argc, char *argv[]){

    FILE *applink=NULL;
    int g15screen_fd;
    unsigned int keystate;
    char cmd[1024];
    int cmdpid=0;

    int p[2];
    int leaving=0;
    g15canvas *canvas;
    int i;
    int voffset=0;
    int hoffset=0;
    unsigned char line[1024][256];
    unsigned char tmpstr[65536];
    int replayover=0;

    if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon\n");
        return -1;
    }

    if(argc<2) {
        printf("G15Term (c) 2007 Mike Lampard\n");
        printf("Please put the application to run on the commandline\n");
        printf("and try again.\n");
        return -1;
    }

    if(0==strncmp(argv[1],"-r",2)) {
      replayover=1;
      *argv++;
    }
    
    strcpy(cmd,argv[1]);
    canvas = (g15canvas *) malloc (sizeof (g15canvas));

    if(canvas != NULL)
        g15r_initCanvas(canvas);
    else
        return -1;
    
start:
    
    if(pipe(p)<0)
        printf("Pipe error!");

    cmdpid=fork();
    switch(cmdpid) {
        case 0:
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            setenv("COLUMNS","43",1);
            setenv("LINES","6",1);
            setenv("TERM","glasstty",1);
            argv++;
            execvp(*argv,argv);
            break;
        default:
            close(p[1]);
            applink=fdopen(p[0],"r");
             fcntl (p[0], F_SETFL, O_NONBLOCK);
    }

    while(!leaving) {

        g15r_clearScreen (canvas, G15_COLOR_WHITE);
        g15r_pixelBox (canvas, 1, 0, 159, 6,1,G15_COLOR_BLACK,1 );

        canvas->mode_xor = 1;    
        g15r_renderString (canvas, (unsigned char *)cmd, 0, G15_TEXT_SMALL, 2, 1);
        canvas->mode_xor = 0;
        i=0;

        while(fgets((char*)tmpstr,65536,applink)!=NULL && i<65536){


          int z=0,x=9;
          char ctrl=0;
          memset(line[i],0,256);
            for(x=0;x<strlen((char*)tmpstr);x++){
              if((ctrl=iscntrl(tmpstr[x]))==0){
                line[i][z++]=tmpstr[x];
                }else{
                int ctrlchar=tmpstr[x++];
                 if(ctrl==2){
                   switch(ctrlchar){
                     case 13:
                       i++;
                       z=0;
                       break;
                     case 12: {
                       int zz;
                       for(zz=0;zz<1024;zz++){
                         memset(line[zz],0,256);
                       }
                       x=0; z=0; i=0;  voffset = hoffset = 0;
                       break;
                      }
                      case 10:
                        break;
                      case 9:
                        z+=2;
                      case 0:
                        break;
                      default:
                         printf("G15Term: Unhandled CTRL char %i\n",ctrlchar);
                   }
                 }
                }
            }
            i++;
        }

        for(i=0;i<7;i++){
            g15r_renderString (canvas, (unsigned char *)line[i+voffset]+(hoffset), i+1, G15_TEXT_SMALL,1,1);
        }

        g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);  

        recv(g15screen_fd,&keystate,4,0);

        if(keystate & G15_KEY_L1) {
          leaving=1;
        }
        if(keystate & G15_KEY_L2) {
            if(hoffset)
                hoffset-=5;
        }
        if(keystate & G15_KEY_L3) {
            if(hoffset<1024)
                hoffset+=5;
        }
        if(keystate & G15_KEY_L4) {
            if(voffset)
                voffset-=2;
        }
        if(keystate & G15_KEY_L5) {
            if(voffset<1024)
                voffset+=2;
        }

        usleep(70000);
        if(feof(applink))
         break;
    }
    fclose(applink);

    if(replayover&!leaving)
      goto start;
          
    close(g15screen_fd);
    free(canvas);
    return 0;  
}

