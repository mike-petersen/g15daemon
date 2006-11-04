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
#include <stdio.h>
#include <stdlib.h>
#include <libdaemon/daemon.h>
#include "g15daemon.h"
#include <libg15.h>

extern lcd_t *keyhandler;
extern unsigned int client_handles_keys;

lcd_t * create_lcd () {

    lcd_t *lcd = g15_xmalloc (sizeof (lcd_t));
    lcd->max_x = LCD_WIDTH;
    lcd->max_y = LCD_HEIGHT;
    lcd->backlight_state = G15_BRIGHTNESS_MEDIUM;
    lcd->mkey_state = G15_LED_MR;
    lcd->contrast_state = G15_CONTRAST_MEDIUM;
    lcd->state_changed = 1;
    lcd->usr_foreground = 0; 
    
    return (lcd);
}

void quit_lcd (lcd_t * lcd) {
    free (lcd);
}


/* initialise a new displaylist, and add an initial node at the tail (used for the clock) */
lcdlist_t *lcdlist_init () {
    
    lcdlist_t *displaylist = NULL;
    
    pthread_mutex_init(&lcdlist_mutex, NULL);
    pthread_mutex_lock(&lcdlist_mutex);
    
    displaylist = g15_xmalloc(sizeof(lcdlist_t));
    
    displaylist->head = g15_xmalloc(sizeof(lcdnode_t));
    
    displaylist->tail = displaylist->head;
    displaylist->current = displaylist->head;
    
    displaylist->head->lcd = create_lcd();
    displaylist->head->lcd->mkey_state = 0;
    
    displaylist->head->prev = displaylist->head;
    displaylist->head->next = displaylist->head;
    displaylist->head->list = displaylist;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    return displaylist;
}

lcdnode_t *lcdnode_add(lcdlist_t **display_list) {
    
    lcdnode_t *new = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    
    new = g15_xmalloc(sizeof(lcdnode_t));
    new->prev = (*display_list)->head;
    new->next = (*display_list)->tail; 
    new->lcd = create_lcd();
    new->last_priority = NULL;
    
    (*display_list)->head->next=new;
    (*display_list)->current = new;
    
    (*display_list)->head = new;
    (*display_list)->head->list = *display_list;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    
    return new;
}

void lcdnode_remove (lcdnode_t *oldnode) {
    
    lcdlist_t **display_list = NULL;
    lcdnode_t **prev = NULL;
    lcdnode_t **next = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    
    display_list = &oldnode->list;
    prev = &oldnode->prev;
    next = &oldnode->next;
    
    quit_lcd(oldnode->lcd);
    
    if((*display_list)->current == oldnode) {
        if((*display_list)->current!=(*display_list)->head){
            (*display_list)->current = oldnode->next;
        } else {
            (*display_list)->current = oldnode->prev;
        }
        (*display_list)->current->lcd->state_changed = 1;
    }
    
    if(&oldnode->lcd == keyhandler) {
        client_handles_keys = 0;
        keyhandler = NULL;
        daemon_log(LOG_WARNING,"Client key handler quit, going back to defaults");
    }
    
    if((*display_list)->head!=oldnode){
        (*next)->prev = oldnode->prev;
        (*prev)->next = oldnode->next;
    }else{
        (*prev)->next = (*display_list)->tail;
        (*display_list)->head = oldnode->prev;
    }

    free(oldnode);
    
    pthread_mutex_unlock(&lcdlist_mutex);
}

void lcdlist_destroy(lcdlist_t **displaylist) {
    
    int i = 0;
    
    while ((*displaylist)->head != (*displaylist)->tail) {
        i++;
        lcdnode_remove((*displaylist)->head);
    }
    
    free((*displaylist)->tail->lcd);
    free((*displaylist)->tail);
    free(*displaylist);
    
    pthread_mutex_destroy(&lcdlist_mutex);
}

