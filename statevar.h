
/*
 * statevar.h Created 9/13/99
 * 
 *  Copyright (C) 1999  Stephen Rodgers
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
 * Stephen Rodgers <highwaystar@home.com>
 *
 * $Id$
 */

#ifndef IRRSTATE_H
#define IRRSTATE_H

typedef struct state_var State_Var;
typedef struct state_var_entry State_Var_Entry; 


struct state_var_entry{
	char	*key;
	char	*value;
	struct state_var_entry *next;
};


struct state_var{
	char *path;
	struct state_var_entry *head;
};


extern State_Var *statevar_load(char *path);
extern char *statevar_lookup(State_Var *handle, char *key);
extern int statevar_remove(State_Var *handle, char *key);
extern int statevar_add(State_Var *handle, char *key, char *value);
extern int statevar_flush(State_Var *handle);
extern int statevar_sync(State_Var *handle);

#endif
