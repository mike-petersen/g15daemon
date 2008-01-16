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

This is a helper application that exposes some of the libg15Render API to scripts.
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
#include <signal.h>
#include <errno.h>

#include <glib.h>

int leaving = 0;
int g15screen_fd = 0;

enum {
    NEWSCREEN=G_TOKEN_LAST,
    PRINT,
    DRAWLINE,
    PIXELBOX,
    CIRCLE,
    ROUNDBOX,
    DRAWBAR,
    BIGNUM,
    SETPIXEL,
    CLEARSCREEN,
    FLUSH,
    WBMPSPLASH,
    WBMPBUFICON,
    WBMPBUFSPRITE,
    WBMPBUF,
    INKEY,
    WAITKEY,
    QUIT
};

static const struct {
    gchar *symbol_name;
    guint symbol_token;
}symbols[] = {
    {"new", NEWSCREEN, },
    {"print", PRINT, },
    {"line", DRAWLINE, },
    {"box", PIXELBOX, },
    {"roundbox", ROUNDBOX, },
    {"circle", CIRCLE, },
    {"bar", DRAWBAR, },
    {"bignum", BIGNUM, },
    {"setpixel", SETPIXEL, },
    {"clear", CLEARSCREEN, },
    {"flush", FLUSH, },
    {"wbmpsplash", WBMPSPLASH, },
    {"wbmpicon", WBMPBUFICON, },
    {"wbmpsprite", WBMPBUFSPRITE, },
    {"wbmploadbuf", WBMPBUF, },
    {"inkey", INKEY, },
    {"waitkey", WAITKEY, },
    {"quit", QUIT, },
    {NULL, 0, },
}, *symbol_p = symbols;

int mode = 0;

char *cmdname = NULL;

void show_help(char *cmdname, int mode){
  static const char *helptext[]={
    "g15new: a microdaemon that sends output from the other commands to g15daemon.\n\tMust be run before any other command.\n",
    "g15print \"text\",row,size(0-2),x,y [,optional flags: c(centered) i(inverse) r(round inverse) f(inverse types are full width) x(xor mode)\n",
    "g15line x,y,x2,y2,color(0=white,1=black)\n",
    "g15box x,y,x2,y2,color(0=white,1=black),thickness,filled\n",
    "g15circle x,y,radius,filled,color(0=white,1=black)",
    "g15bar x,y,x2,y2,num,max,type - see libg15render.h for details\n",
    "",
    "",
    "g15clear: clear the canvas for new data",
    "g15flush: write the canvas to g15daemon",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "g15quit: quit the scriptool.. should be done at the end of each script.",
    NULL
  };
  if(strlen(helptext[mode-G_TOKEN_LAST]))
    printf("	%s\n",helptext[mode-G_TOKEN_LAST]);
}

void find_personality(char *whoami) {

    int i;
    /* strip any leading '/' */ 
    for(i=0;i<strlen(whoami);i++){
        if(whoami[i]=='/') {
            whoami+=++i;
        }
    }
    cmdname = whoami;
    if(!strcmp(whoami,"g15print"))
        mode = PRINT;
    else if(!strcmp(whoami,"g15new"))
        mode = NEWSCREEN;
    else if(!strcmp(whoami,"g15line"))
        mode = DRAWLINE;
    else if(!strcmp(whoami,"g15box"))
        mode = PIXELBOX;
    else if(!strcmp(whoami,"g15circle"))
        mode = CIRCLE;
    else if(!strcmp(whoami,"g15roundbox"))
        mode = ROUNDBOX;
    else if(!strcmp(whoami,"g15bar"))
        mode = DRAWBAR;
    else if(!strcmp(whoami,"g15bignum"))
        mode = BIGNUM;
    else if(!strcmp(whoami,"g15setpixel"))
        mode = SETPIXEL;
    else if(!strcmp(whoami,"g15clear"))
        mode = CLEARSCREEN;
    else if(!strcmp(whoami,"g15flush"))
        mode = FLUSH;
    else if(!strcmp(whoami,"g15quit"))
        mode = QUIT;
    else if(!strcmp(whoami,"g15wbmpsplash"))
        mode = WBMPSPLASH;
    else if(!strcmp(whoami,"g15wbmpicon"))
        mode = WBMPBUFICON;
    else if(!strcmp(whoami,"g15wbmpspritte"))
        mode = WBMPBUFSPRITE;
    else if(!strcmp(whoami,"g15inkey"))
        mode = INKEY;
    else if(!strcmp(whoami,"g15waitkey"))
        mode = WAITKEY;
    else {
        printf("please dont run %s directly\n",whoami);
        for(i=NEWSCREEN;i<=QUIT;i++)
          show_help("",i);
        exit(0);
    }
}

int retrieve_int_token (GScanner *scanner)
{
    int retval=-1;

// remove any comma after the token
    g_scanner_peek_next_token (scanner);
    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
    }

    g_scanner_get_next_token (scanner);

    if ( scanner->token == G_TOKEN_INT )
        retval = scanner->value.v_int;

    g_scanner_peek_next_token (scanner);
    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
    }

    return retval;
}

int retrieve_opt_int_token (GScanner *scanner, int defaultval)
{
    int retval=defaultval;

    g_scanner_peek_next_token (scanner);

    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
        g_scanner_peek_next_token (scanner); 
    }

    if ( scanner->next_token == G_TOKEN_INT ){
        g_scanner_get_next_token (scanner);
        retval = scanner->value.v_int;
    }

    g_scanner_peek_next_token (scanner);
    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
    }

    return retval;
}

char * retrieve_opt_string_last_token (GScanner *scanner)
{
    char *retval=NULL;
    static char tokenval[2048];
    memset(tokenval,0,2048);
    g_scanner_peek_next_token (scanner);

    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
        g_scanner_peek_next_token (scanner); 
    }
     
    if ( scanner->next_token == 266 ||(scanner->next_token >='A' && scanner->next_token <= 'z')) {
        g_scanner_get_next_token (scanner);

        if(scanner->value.v_comment!=NULL)
          memcpy(tokenval, scanner->value.v_comment,strlen(scanner->value.v_comment));
        else
          tokenval[0]=scanner->token;
        retval = tokenval;
    }
    g_scanner_peek_next_token (scanner);
    if(scanner->next_token == G_TOKEN_COMMA){
        g_scanner_get_next_token(scanner);
    }

    return retval;
}

GScanner *init_cmd_lexer()
{
    GScanner *scanner;
    scanner = g_scanner_new (NULL);

// adjust lexing behaviour to suit our needs
    // convert non-floats (octal values, hex values...) to G_TOKEN_INT
    scanner->config->numbers_2_int = TRUE;
    // do single & double quotes nicely
    scanner->config->scan_string_sq = TRUE;
    scanner->config->scan_string_dq = TRUE;
    // don't return G_TOKEN_SYMBOL, but the symbol's value
    scanner->config->symbol_2_token = TRUE;

    // load tokens into the scanner
    while (symbol_p->symbol_name)
    {
        g_scanner_add_symbol (scanner, symbol_p->symbol_name, GINT_TO_POINTER (symbol_p->symbol_token));
        symbol_p++;
    }

    return scanner;
}

static void fix_bounding_box(int *x1, int *y1, int *x2, int *y2) {
    if(*x1<0) *x1=0;
    if(*x1>159) *x1=159;
    if(*x2<0) *x2=0;
    if(*x2>159) *x2=159;
    if(*y1<0) *y1=0;
    if(*y2<0) *y2=0;
    if(*y1>42) *y1=42;
    if(*y2>42) *y2=42;
}

static guint parse_cmdstring (GScanner *scanner,g15canvas *canvas, FILE *fifo_fd){
    guint symbol;
    int dummy=0;
    g_scanner_get_next_token(scanner);
    symbol = scanner->token;

    if (symbol<NEWSCREEN || symbol > QUIT)
        return G_TOKEN_SYMBOL;

    switch (symbol) {
        case PRINT:
            if(canvas){
                char *tmpstring = NULL;
                int size;
                int x,y,row;
                char *opt_tokens;
                char opt_centered = 0;
                char opt_xor = 0;
                char opt_inverse = 0;
                char opt_fullwidth = 0;
                char opt_roundedbox = 0;
                int i;

                g_scanner_get_next_token (scanner);
                if(scanner->token == G_TOKEN_STRING) {
                    tmpstring = malloc(strlen(scanner->value.v_string));
                    strcpy(tmpstring,scanner->value.v_string);
                }
                row = retrieve_int_token(scanner);
                size = retrieve_int_token(scanner);
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);

                if((opt_tokens=retrieve_opt_string_last_token(scanner))!=NULL) {
                  for(i=0;i<strlen(opt_tokens);i++) {
                    if(opt_tokens[i]=='c')
                      opt_centered=1;
                    if(opt_tokens[i]=='x')
                      opt_xor = 1;
                    if(opt_tokens[i]=='i')
                      opt_inverse = 1;
                    if(opt_tokens[i]=='f') {
                      opt_inverse=1;
                      opt_fullwidth=1;
                    }
                    if(opt_tokens[i]=='r') {
                      opt_inverse=1;
                      opt_roundedbox=1;
                    }
                    
                  }
                 
                }
                if(opt_centered){
                    int len = 0;
                    len = strlen(tmpstring);
                    if (size == 0)  x=(80-((len*4)/2));
                    else if (size == 1)  x=(80-((len*5)/2));
                    else if (size == 2)  x=(80-((len*8)/2));
                }
                
                if(opt_inverse) { // draw a box and write in xor mode..
                  int height = 0;
                  int width = 0;
                  int boxy = 0;
                  int box_x2 = 0;
                  int box_x = 0;
                  
                  switch (size) {
                    case 0:
                      height=6;
                      width=4;
                      break;
                    case 1:
                      height=7;
                      width=5;
                      break;
                    case 2:
                      height=8;
                      width=8;
                      break;
                  }

                  if(row)
                    boxy = height*row;
                  else
                   boxy=y;
                  
                  if(opt_fullwidth){
                    box_x = 0;
                    box_x2 = 160;
                  } else {
                    box_x = x-3;
                    box_x2 =  x+(strlen(tmpstring)*width)+3;
                  }
                
                  fix_bounding_box(&box_x,&boxy,&box_x2,&dummy);
                
                  if(!opt_roundedbox)
                    g15r_pixelBox(canvas, box_x,boxy-1,box_x2,boxy+height,G15_COLOR_BLACK,1,1);
                  else
                    g15r_drawRoundBox (canvas, box_x, boxy-1, box_x2,boxy+height, 1, G15_COLOR_BLACK);
                  opt_xor=1;
                }

                if(opt_xor)  canvas->mode_xor = 1;
                 g15r_renderString (canvas, (unsigned char *)tmpstring, row, size, x, y);
                if(opt_xor) canvas->mode_xor = 0;
                
                free(tmpstring);
            }
            break;
        case DRAWLINE:
            if(canvas) {
                int x,y,x2,y2,colour;
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);
                x2 = retrieve_int_token(scanner);
                y2 = retrieve_int_token(scanner);
                colour = retrieve_int_token(scanner);
                fix_bounding_box(&x,&y,&x2,&y2);
                g15r_drawLine(canvas,x,y,x2,y2,colour);
            }
            break;
        case PIXELBOX:
            if(canvas) {
                int x,y,x2,y2,colour,thickness,filled;
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);
                x2 = retrieve_int_token(scanner);
                y2 = retrieve_int_token(scanner);
                colour = retrieve_int_token(scanner);
                thickness = retrieve_int_token(scanner);
                filled = retrieve_int_token(scanner);
                fix_bounding_box(&x,&y,&x2,&y2);
                g15r_pixelBox (canvas, x, y, x2, y2, colour, thickness, filled);
            }
            break;
        case CIRCLE:
            if(canvas) {
                int x,y,r,filled,colour;
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);
                r = retrieve_int_token(scanner);
                filled = retrieve_int_token(scanner);
                colour = retrieve_int_token(scanner);
                g15r_drawCircle (canvas, x, y, r, filled, colour);
            }
            break;
        case ROUNDBOX:
            if(canvas) {
                int x,y,x2,y2,colour,thickness,filled;
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);
                x2 = retrieve_int_token(scanner);
                y2 = retrieve_int_token(scanner);
                colour = retrieve_int_token(scanner);
                thickness = retrieve_int_token(scanner);
                filled = retrieve_int_token(scanner);
                fix_bounding_box(&x,&y,&x2,&y2);
                g15r_drawRoundBox (canvas, x, y, x2, y2, filled, colour);
            }
            break;
        case DRAWBAR:
            if(canvas) {
                int x,y,x2,y2,colour,num,max,type;
                x = retrieve_int_token(scanner);
                y = retrieve_int_token(scanner);
                x2 = retrieve_int_token(scanner);
                y2 = retrieve_int_token(scanner);
                colour = retrieve_int_token(scanner);
                num = retrieve_int_token(scanner);
                max = retrieve_int_token(scanner);
                type = retrieve_int_token(scanner);
                fix_bounding_box(&x,&y,&x2,&y2);
                g15r_drawBar (canvas, x, y, x2, y2, colour, num, max, type);
            }
            break;
        case BIGNUM:
        case SETPIXEL:
            break;
        case CLEARSCREEN:
            if(canvas) {
                int colour;
                colour = retrieve_opt_int_token(scanner,G15_COLOR_WHITE);
                g15r_clearScreen (canvas, colour);
            }
            break;
        case FLUSH:
            if(!g15screen_fd){
                g15screen_fd = new_g15_screen(G15_G15RBUF);
            }
            if(g15screen_fd) {
                g15_send(g15screen_fd,(char*)canvas->buffer,G15_BUFFER_LEN);
            }
            break;
        case INKEY:
            if(g15screen_fd) {
            	unsigned int keystate=0;
            	recv(g15screen_fd,&keystate,4,0);
                fwrite(&keystate,4,1,fifo_fd);
            }
            break;
        case WAITKEY:
            if(g15screen_fd) {
            	unsigned int keystate=0;
                while(!keystate) {
            		keystate=g15_send_cmd(g15screen_fd,G15DAEMON_GET_KEYSTATE,0);
                }
                fwrite(&keystate,4,1,fifo_fd);
            }
            break;
        case QUIT:
            close(g15screen_fd);
            leaving = 1;
            break;
    }
    
    /* make sure the next token is a ';' */
    if (g_scanner_peek_next_token (scanner) != ';')
    {
      /* not so, eat up the non-semicolon and error out */
        g_scanner_get_next_token (scanner);
        return ';';
    }

    g_scanner_get_next_token (scanner);

    return G_TOKEN_NONE;
}

void g15scriptool_sighandler(int sig) { 
    switch(sig) {
       case SIGINT:
       case SIGTERM:
       case SIGQUIT:
       case SIGPIPE:
          leaving = 1;
       break;
    }
}                                                                                                               
int main(int argc, char *argv[]){

    g15canvas *canvas = NULL;
    int retval;
    int parentid = 0;
    char fifoname[1024];
    FILE *fifo_fd;
    char linebuffer[2048];
    
    parentid = getppid();
    sprintf(fifoname,"/tmp/g15st-%i",parentid);

    find_personality(argv[0]);    

    if(mode==NEWSCREEN) {
        canvas = (g15canvas *) malloc (sizeof (g15canvas));
        if(canvas != NULL)
            g15r_initCanvas(canvas);
        else
            return -1;
        mode_t fifo_mode = S_IRUSR | S_IWUSR | S_IWGRP;
        if((retval = mkfifo(fifoname,fifo_mode))<0) {
          unlink(fifoname); // aren't we naughty..
          if((retval = mkfifo(fifoname,fifo_mode))<0) {
            printf("Sorry, cant create pipe\n");
            return -1;
          }
        }
        fifo_fd = fopen(fifoname,"r+");
        if(!fifo_fd){
            printf("Sorry, cant open pipe\n");
            return -1;
        }
        daemon(0,0);
        struct sigaction new_action;
        new_action.sa_handler = g15scriptool_sighandler;
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
        sigaction(SIGQUIT, &new_action, NULL);
        sigaction(SIGTERM, &new_action, NULL);
        sigaction(SIGPIPE, &new_action, NULL);
        
        GScanner *scanner = init_cmd_lexer();
        int expected_token = 0;
        while(leaving == 0) {
            memset(linebuffer,0,2048);
            while(!fgets(linebuffer,2048,fifo_fd))
                usleep(70000);

            g_scanner_input_text(scanner,linebuffer,strlen(linebuffer));
            scanner->input_name = "G15Scriptool";
            do
            {
                expected_token = parse_cmdstring(scanner,canvas,fifo_fd);
                g_scanner_peek_next_token (scanner);
            }
            while (expected_token == G_TOKEN_NONE &&
                   scanner->next_token != G_TOKEN_EOF &&
                   scanner->next_token != G_TOKEN_ERROR && leaving == 0);

        }
        fclose(fifo_fd);
        unlink(fifoname);
	close(g15screen_fd);
    	free(canvas);
    }else {
        int i;
        char cmdline[2048];
        memset(cmdline,0,2028);
        
        for(i=1;i<argc;i++){
            strncat(cmdline,argv[i],2048);
            strncat(cmdline," ",1);
        }
        
        switch (mode) {
            case DRAWLINE:
            case PIXELBOX:
            case CIRCLE:
            case ROUNDBOX:
            case DRAWBAR:
            case BIGNUM:
            case SETPIXEL:
            case CLEARSCREEN:
            case FLUSH:
            case INKEY:
            case WAITKEY:
            case QUIT:
            break;
            case PRINT:{
              int commas=0;
              int opt_flags=0;
              char cc[]={'"'};
              int firstarg_len=0;
              char modded_cmd[2048];
              memset(modded_cmd,0,2048);
              strncat(modded_cmd,cc,1);
              if(isalpha(cmdline[strlen(cmdline)-2])) {
                opt_flags=1;
              }
              for(i=strlen(cmdline);i>0;i--){
                if(cmdline[i]==',')
                  commas++;
                if(commas==4+opt_flags){
                 firstarg_len =  i;
                 strncat(modded_cmd,cmdline,firstarg_len);
                 strncat(modded_cmd,cc,1);
                 strncat(modded_cmd,cmdline+firstarg_len,strlen(cmdline)-firstarg_len);
                 memset(cmdline,0,2048);
                 strncpy(cmdline,modded_cmd,strlen(modded_cmd));
                 break;
                }
              }
             if(commas<=3){
              printf("incorrect number of args %s\n",cmdline);
              return -1;
             }
            }
            break;

            default:
              printf("%s is not implemented yet, please check and try again\n",cmdname+3);
        }
            fifo_fd = fopen(fifoname,"wr+");

            fprintf(fifo_fd,"%s %s\n",cmdname+3,cmdline);
            fclose(fifo_fd);
    }

    return 0;  
}

