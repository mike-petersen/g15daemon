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
#include <pthread.h>
#include <g15daemon.h>
#include <libg15.h>

extern lcd_t *keyhandler;
extern unsigned int client_handles_keys;
extern plugin_info_t *generic_info;

lcd_t static * ll_create_lcd () {

    lcd_t *lcd = g15daemon_xmalloc (sizeof (lcd_t));
    lcd->max_x = LCD_WIDTH;
    lcd->max_y = LCD_HEIGHT;
    lcd->backlight_state = G15_BRIGHTNESS_MEDIUM;
    lcd->mkey_state = 0;
    lcd->contrast_state = G15_CONTRAST_MEDIUM;
    lcd->state_changed = 1;
    lcd->usr_foreground = 0;
    lcd->g15plugin = g15daemon_xmalloc(sizeof (plugin_s)); 
    lcd->g15plugin->plugin_handle = NULL;
    lcd->g15plugin->info = (void*)&generic_info;
    
    return (lcd);
}

static void ll_quit_lcd (lcd_t * lcd) {
    free (lcd->g15plugin);
    free (lcd);
}


/* initialise a new displaylist, and add an initial node at the tail (used for the clock) */
lcdlist_t *ll_lcdlist_init () {
    
    lcdlist_t *displaylist = NULL;
    
    pthread_mutex_init(&lcdlist_mutex, NULL);
    pthread_mutex_lock(&lcdlist_mutex);
    
    displaylist = g15daemon_xmalloc(sizeof(lcdlist_t));
    
    displaylist->head = g15daemon_xmalloc(sizeof(lcdnode_t));
    
    displaylist->tail = displaylist->head;
    displaylist->current = displaylist->head;
    
    displaylist->head->lcd = ll_create_lcd();
    displaylist->head->lcd->mkey_state = 0;
    displaylist->head->lcd->masterlist = displaylist;
    /* first screen is the clock/menu */
    displaylist->head->lcd->g15plugin->info = NULL;
    
    displaylist->head->prev = displaylist->head;
    displaylist->head->next = displaylist->head;
    displaylist->head->list = displaylist;
    displaylist->keyboard_handler = NULL;
    displaylist->numclients = 0;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    return displaylist;
}

lcdnode_t *g15daemon_lcdnode_add(lcdlist_t **displaylist) {
    
    lcdnode_t *new = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    new = g15daemon_xmalloc(sizeof(lcdnode_t));
    new->prev = (*displaylist)->head;
    new->next = (*displaylist)->tail; 
    new->lcd = ll_create_lcd();
    new->lcd->masterlist = (*displaylist);
    new->last_priority = NULL;
    new->list = *displaylist;
    
    (*displaylist)->head->next=new;
    (*displaylist)->current = new;
    
    (*displaylist)->head = new;
    (*displaylist)->head->list = *displaylist;
    (*displaylist)->numclients++;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    
    return new;
}

/* cycle through connected client displays */
void g15daemon_lcdnode_cycle(lcdlist_t *displaylist)
{
    lcdnode_t *current_screen = displaylist->current;
    
    g15daemon_send_event(current_screen->lcd, G15_EVENT_VISIBILITY_CHANGED, SCR_HIDDEN);
    do
    {
        pthread_mutex_lock(&lcdlist_mutex);
        g15daemon_send_event(current_screen->lcd, G15_EVENT_USER_FOREGROUND, 0);
        
        if(displaylist->tail == displaylist->current){
            displaylist->current = displaylist->head;
        }else{
            displaylist->current = displaylist->current->prev;
        }
        pthread_mutex_unlock(&lcdlist_mutex);
    } 
    while (current_screen != displaylist->current);
    pthread_mutex_lock(&lcdlist_mutex);
    if(displaylist->tail == displaylist->current) {
        displaylist->current = displaylist->head;
    } else {
        displaylist->current = displaylist->current->prev;
    }
    pthread_mutex_unlock(&lcdlist_mutex);
    g15daemon_send_event(displaylist->current->lcd, G15_EVENT_VISIBILITY_CHANGED, SCR_VISIBLE);
    g15daemon_send_event(current_screen->lcd, G15_EVENT_USER_FOREGROUND, 1);
    pthread_mutex_lock(&lcdlist_mutex);
    displaylist->current->lcd->state_changed = 1;
    displaylist->current->last_priority =  displaylist->current;
    pthread_mutex_unlock(&lcdlist_mutex);
}

void g15daemon_lcdnode_remove (lcdnode_t *oldnode) {
    
    lcdlist_t **displaylist = NULL;
    lcdnode_t **prev = NULL;
    lcdnode_t **next = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    
    displaylist = &oldnode->list;
    prev = &oldnode->prev;
    next = &oldnode->next;
    
    ll_quit_lcd(oldnode->lcd);
    (*displaylist)->numclients--;
    if((*displaylist)->current == oldnode) {
        if((*displaylist)->current!=(*displaylist)->head){
            (*displaylist)->current = oldnode->next;
        } else {
            (*displaylist)->current = oldnode->prev;
        }
        (*displaylist)->current->lcd->state_changed = 1;
    }
    
    if(&oldnode->lcd == (void*)keyhandler) {
        client_handles_keys = 0;
        keyhandler = NULL;
        g15daemon_log(LOG_WARNING,"Client key handler quit, going back to defaults");
    }
    
    if((*displaylist)->head!=oldnode){
        (*next)->prev = oldnode->prev;
        (*prev)->next = oldnode->next;
    }else{
        (*prev)->next = (*displaylist)->tail;
        (*displaylist)->head = oldnode->prev;
    }

    free(oldnode);
    
    pthread_mutex_unlock(&lcdlist_mutex);
}

void ll_lcdlist_destroy(lcdlist_t **displaylist) {
    int i = 0;
    
    while ((*displaylist)->head != (*displaylist)->tail) {
        i++;
        g15daemon_lcdnode_remove((*displaylist)->head);
    }
    
    free((*displaylist)->tail->lcd);
    free((*displaylist)->tail);
    free(*displaylist);
    
    pthread_mutex_destroy(&lcdlist_mutex);
}

