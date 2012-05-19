
/*
 * confscan.h Created 8/11/99
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
 * Stephen Rodgers <hwstar@cox.net>
 *
 * $Id$
 */

#ifndef CONFSCAN_H
#define CONFSCAN_H

/* Typedefs */

typedef struct keyent Key_Entry;
typedef struct sectionent Section_Entry;

/* Entry for a key table entry */

struct	keyent{
	char *key;
	short handling;
	void *result;
	int (*action)(char *value, short handling, void *result);
};

/* Entry for a section block table */

struct sectionent{
	char *section;
	struct keyent *keylist;
};

/* Function prototypes */

extern  void confscan(char *confpath, Section_Entry *sectlist);


#endif	
	
	

