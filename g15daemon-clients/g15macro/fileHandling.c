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

        (c) 2006-2009 Mike Lampard

        $Revision: 508 $ -  $Date: 2009-06-02 16:52:42 +0200 (Tue, 02 Jun 2009) $ $Author: steelside $

        This daemon listens on localhost port 15550 for client connections,
        and arbitrates LCD display.  Allows for multiple simultaneous clients.
        Client screens can be cycled through by pressing the 'L1' key.

        This is a macro recorder and playback utility for the G15 and g15daemon.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/time.h>
#include <config.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>
#include "config.h"

#define  XK_MISCELLANY
#define XK_LATIN1
#define XK_LATIN2
#include <X11/keysymdef.h>

#include "g15macro.h"

unsigned char* getConfigName(unsigned int id)
{
	if (id < 0 || id > numConfigs)
		return NULL;

	if (id != 0)
		return (unsigned char*)configs[id]->configfile;
	else
		return (unsigned char*)"Default\0";
}

// Various distributions have started giving
// different keycodes to the special keys
// Next 2 functions handles this
// TODO: We got alot of config files for very little info in each now..
// Also this is most likely systemwide, but get's written for each user.
int writeKeyDefs(char *filename)
{
	FILE *f;
	unsigned int i=0;
	f = fopen(filename,"w");
	if(!f)
	{
		printf("ERROR: Unable to open keycode definition file (%s). Aborting.\n",filename);
		return False;
	}
	fprintf(f,"#Use this file to define what keycode each key has.\n");
	fprintf(f,"#You can use the following command to get the keycodes:.\n");
	fprintf(f,"#xev | grep 'keycode' --line-buffered | grep --line-buffered -o -E 'keycode [0-9]+' | cut -d' ' -f 2\n");
	fprintf(f,"#Run the command and hit each key, remember in what order you pressed the keys,then write the number returned at the right place.\n");
	fprintf(f,"#Keep in mind; each number will appear twice.\n");
	fprintf(f,"#Format is Key:Keycode. Example: G1:138\n");
	for (i = 0;i < 18;++i)
	{
		fprintf(f,"G%i:%i\n",i+1,gkeycodes[i]);
	}
	fprintf(f,"AudioStop:%i\n",mmedia_codes[0]);
	fprintf(f,"AudioPlay:%i\n",mmedia_codes[1]);
	fprintf(f,"AudioPrev:%i\n",mmedia_codes[2]);
	fprintf(f,"AudioNext:%i\n",mmedia_codes[3]);
	fprintf(f,"AudioLowerVolume:%i\n",mmedia_codes[4]);
	fprintf(f,"AudioRaiseVolume:%i\n",mmedia_codes[5]);

	fclose(f);
	return True;
}
void getKeyDefs(char *filename)
{
	FILE *f = NULL;
	char buf[1024];
	unsigned int key=0;
// 	unsigned int i=0;
	unsigned int keycode=0;

	while (!f)
	{
		f=fopen(filename,"r");
		if (!f)
		{
			if (!writeKeyDefs(filename))
				return;
		}
	}

	printf("Reading keycodes for keys from %s\n",filename);
	while (!feof(f))
	{
		memset(buf,0,sizeof(buf));
		fgets(buf,sizeof(buf),f);

		// Ignore comments and blanklines
		if (buf[0] == '#' || strlen(buf) == 0)
			continue;

		if (sscanf(buf,"G%i:%i", &key,&keycode)){
// 			printf("%i:%i\n",key,keycode);
// 			printf("Gkeycode%i:%i\n",key,gkeycodes[key-1]);
			gkeycodes[key-1] = keycode;
// 			printf("Gkeycode%i:%i\n",key,gkeycodes[key-1]);
		}
		sscanf(buf,"AudioStop:%i",&mmedia_codes[0]);
		sscanf(buf,"AudioPlay:%i",&mmedia_codes[1]);
		sscanf(buf,"AudioPrev:%i",&mmedia_codes[2]);
		sscanf(buf,"AudioNext:%i",&mmedia_codes[3]);
		sscanf(buf,"AudioLowerVolume:%i",&mmedia_codes[4]);
		sscanf(buf,"AudioRaiseVolume:%i",&mmedia_codes[5]);
	}
	fclose(f);
}

/* WARNING:  uses global mkey state */
void dump_config(FILE *configfile)
{
	int i=0,gkey=0;
	KeySym key;
	pthread_mutex_lock(&config_mutex);
	int orig_mkeystate=mkey_state;
	for(mkey_state=0;mkey_state<3;mkey_state++){
		if (mkey_state > 0)
			fprintf(configfile,"\n\n");
		fprintf(configfile,"Codes for MKey %i\n",mkey_state+1);
		for(gkey=0;gkey<18;gkey++){
			fprintf(configfile,"Key %s:",gkeystring[gkey]);
			/* if no macro has been recorded for this key, dump the g15daemon default keycode */
			if(mstates[mkey_state]->gkeys[gkey].keysequence.record_steps==0){
				int mkey_offset=0;
				mkey_offset = calc_mkey_offset();
				fprintf(configfile,"\t%s\n",XKeysymToString(gkeydefaults[gkey+mkey_offset]));
			}else{
				fprintf(configfile,"\n");
				for(i=0;i<mstates[mkey_state]->gkeys[gkey].keysequence.record_steps;i++){
					key = XKeycodeToKeysym(dpy,mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].keycode,0);
					fprintf(configfile,"\t%s %s %u\n",XKeysymToString(key),mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].pressed?"Down":"Up",(unsigned int)mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].modifiers);
				}
			}
		}
	}
	mkey_state=orig_mkeystate;
	pthread_mutex_unlock(&config_mutex);
}

void save_macros(char *filename)
{
	printf("Saving macros to %s\n",filename);
	FILE *configfile;
	configfile=fopen(filename,"w");
	if (!configfile)
	{
		printf("Unable to open %s\n",filename);
		return;
	}

	dump_config(configfile);

	fsync( fileno(configfile) );
	fclose(configfile);
}