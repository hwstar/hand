
/*
 * statevar.c Created 9/13/99
 * 
 *  Copyright (C) 1999,2002  Stephen Rodgers
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * 
 * Stephen "Steve" Rodgers <hwstar@cox.net>
 *
 * $Id$
 */

#include "tnd.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include "error.h"
#include "statevar.h"

/*
* Constants
*/

#define	SV_MAX_LINE 80

/*
* Internal function prototypes
*/

static void appendToList(State_Var *handle, State_Var_Entry *new_entry);
static State_Var_Entry *findInList(State_Var *handle, char *key);


/*
* Internal globals
*/

char *outofmemory = "Out of Memory";

/*
* Internal functions
*/

/*
* Return the list entry matching the key, or NULL if no match
*/

static State_Var_Entry *findInList(State_Var *handle, char *key){

	State_Var_Entry *p;

	for(p = handle->head; p != NULL; p = p->next){
		if(!strcmp(key, p->key)){
			return p;
		}
	}
	return NULL;
}

/*
* Append a new entry to the end of the list
*/

static void appendToList(State_Var *handle, State_Var_Entry *new_entry){

	State_Var_Entry *p;
	
	if(handle->head == NULL)
		handle->head = new_entry;
	else{
		for(p = handle->head; p->next != NULL; p = p->next);

		p->next = new_entry;
	}
}
		
/*
* Load a list of state variables into memory
*/

State_Var *statevar_load(char *path){

	State_Var *handle;
	State_Var_Entry *newEntry;
	FILE *file;
	char line[SV_MAX_LINE + 1], value[SV_MAX_LINE + 1], key[SV_MAX_LINE + 1];
	
	
	/* Allocate the list header */

	if((handle = malloc(sizeof(State_Var))) == NULL){
		debug(DEBUG_UNEXPECTED, "malloc() call for handle in statevar_load() failed");
		fatal(outofmemory);
		}

	/* Save the pointer to the pathname */

	handle->path = path;

	/* Attempt to open the file */

	handle->head = NULL;

	if((file = fopen(handle->path, "r")) == NULL){
		debug(DEBUG_EXPECTED, "Note: state file: %s does not exist", handle->path);
		return handle;
	}

	/* Read in the contents of the file */

	for(;;){
		if(fgets(line, SV_MAX_LINE, file) == NULL){
			if(feof(file))
				break;
			else
				fatal_with_reason(errno, "Error reading %s: %s", handle->path);			
		}
	
		if(sscanf(line,"%s\t=\t%s\n", key, value) != 2){
			debug(DEBUG_UNEXPECTED, "can't find 2 substrings, skipping: %s",line);
			continue;
			}
						
		debug(DEBUG_EXPECTED,"key = %s, value = %s", key, value);
		
		if((newEntry = malloc(sizeof(State_Var_Entry))) == NULL){
			debug(DEBUG_UNEXPECTED,"malloc() call for newEntry in statevar_load() failed"); 
			fatal(outofmemory);
		}

		if((newEntry->key = strdup(key)) == NULL){
			debug(DEBUG_UNEXPECTED,"malloc() call for newEntry->key in statevar_load() failed"); 
			fatal(outofmemory);
		}
		
		if((newEntry->value = strdup(value)) == NULL){
			debug(DEBUG_UNEXPECTED,"malloc() call for newEntry->value in statevar_load() failed"); 
			fatal(outofmemory);
		}
				
		appendToList(handle, newEntry);
	}
			
	if(fclose(file)){
		debug(DEBUG_UNEXPECTED,"Error closing file in statevar_load()");  
		fatal_with_reason(errno, "Error closing %s", handle->path);
		}

	return handle;	
}

/*
* Lookup a state variable
*/

char *statevar_lookup(State_Var *handle, char *key){
	
	State_Var_Entry *p;
	
	p = findInList(handle, key);
	
	if(p == NULL)
		return NULL;
	else
		return p->value;
}

/*
* Remove a state variable. Return a TRUE if the key to delete was found, and
* a FALSE if no key match occurred.
*/


int statevar_remove(State_Var *handle, char *key){
	
	State_Var_Entry **p, *q, *r;

	/* Find the entry to delete */
	
	for(p = &handle->head; *p != NULL ; p = &((*p)->next)){

		/* deref p to get r, a pointer to the entry to check */ 

		r = *p;
		
		/* remember where the next item in the list is */

		q = r->next;
		
		/* check for a match */
		
		if(!strcmp(r->key, key)){

			/* a match, free key and value strings */
			
			free(r->key);
			free(r->value);
			
			/* free the entry itself */
			
			free(r);
			
			/* adjust the link to account for the deletion */
			
			*p = q;
			
			/* return indicating we deleted something */
			
			return TRUE;
		}
	}
	/* return indicating we did not delete something */
		
	return FALSE;
}

/*
* Add a state variable, if it exists, replace it, and return
* TRUE indicating something was replaced. Return FALSE if the
* key/value pair is added to the end of the list.
*/


int statevar_add(State_Var *handle, char *key, char *value){
	State_Var_Entry **p, *q;
	char *k, *v;

	/* make a copy of value */
		
	v = strdup(value);
	if(v == NULL){
		debug(DEBUG_UNEXPECTED,"malloc() call for v in statevar_add() failed");
		fatal(outofmemory);
	}
	
	/* search the list for a key match */
	
	for(p = &handle->head; *p != NULL;  p = &((*p)->next)){
		q = *p;
		debug(DEBUG_EXPECTED,"key = %s, value = %s, next = %08X", q->key, q->value, q->next);
		if(!strcmp(q->key, key)){

			/* free the current value string */
			
			free(q->value);
			
			/* assign the new string to the entry value member */
			
			q->value = v;
			
			/* return the fact we did a replace */
			
			return TRUE;
		}
	}
	
	debug(DEBUG_EXPECTED,"not in list, append to end *p = %08X", *p);	

	/* it is not in the current list, so postpend it to the end */
	/* make a new entry */
	
	q = malloc(sizeof(State_Var_Entry));
	if(q == NULL){
		debug(DEBUG_UNEXPECTED,"malloc() call for q in statevar_add() failed");
		fatal(outofmemory);
	}
	
	/* make a copy of the key string passed to us */
		
	k = strdup(key);
	if(k == NULL){
		debug(DEBUG_UNEXPECTED,"malloc() call for k in statevar_add() failed");
		fatal(outofmemory);
	}

	/* Initialize the member to add */

	q->key = k;
	q->value = v;
	q->next = NULL;

	/* link it onto the end of the list */

	*p = q;

	/* return signifying we added it to the end of the list */
	
	return FALSE;
}

/*
* Write the list back out to the file 
*/

int statevar_sync(State_Var *handle){
	
	FILE *file;
	State_Var_Entry *p;
	int i;
	
	/* open the file as text, and truncate it to 0 length */
	if((file = fopen(handle->path,"w")) == NULL)
		fatal_with_reason(errno, "Error opening %s", handle->path);
	
	/* write to the file, the key/value pairs one line at a time */
	for(i = 0, p = handle->head; (i < 5) && (p != NULL); i++,  p = p->next){
		debug(DEBUG_EXPECTED," p = %08X, p->next = %08X", p, p->next);
		fprintf(file, "%s\t=\t%s\n", p->key, p->value);
	}
	
	if(fclose(file)){  
		debug(DEBUG_UNEXPECTED,"Error closing file in statevar_sync()");  
		fatal_with_reason(errno, "Error closing %s", handle->path);
	}
	return PASS;
}


