
/*
 * confscan.c Created 8/11/99
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
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include "confscan.h"
#include "error.h"

/* Definitions */

#define MAX_CONFIG_LINE 384
#define MAX_PARAM 256
#define MAX_SECTION 64
#define MAX_KEY 64
#define MAX_VALUE 256

/* Scanner tokens */

#define TOK_ERR -1
#define TOK_NL 0
#define TOK_SECTION 1
#define TOK_KEY 2
#define TOK_VALUE 3
#define TOK_COMMENT 4

/* Internal functions */

static int linescan(char **lp, char *tokstring);
static char *eatwhite(char *curlinepos);
static void syntax_error(int linenum);
static char copyuntil(char *dest, char **srcp, int max_dest_len, char *stopchrs);

/* Global functions */


void confscan(char *confpath, Section_Entry *sectlist){
	FILE *conf_file;
	char *p;
	Section_Entry *sl;
	Key_Entry *ck,*kl = NULL;
	int linenum;
	static char line[MAX_CONFIG_LINE];
	static char param[MAX_PARAM];
	static char section[MAX_SECTION];
	static char key[MAX_KEY];
	static char value[MAX_VALUE];

	/* Open the config file */

	if((conf_file = fopen(confpath, "r")) == NULL)
		fatal("Can't open config file: %s\n",confpath);
	
	for(linenum = 1; ; linenum++){
		if(fgets(line, MAX_CONFIG_LINE, conf_file) == NULL)
			break;
		
		p = line;
		
		/* Parse tree root */

		switch(linescan(&p, param)){

			/* It was a newline or a comment, get another line */

			case TOK_NL:
			case TOK_COMMENT:
				break;

			/* It was a section ID, get it, and try to match it */
			
			case TOK_SECTION:
				strncpy(section, param, MAX_SECTION - 1); // Save param
				section[MAX_SECTION - 1] = 0;
				/* Scan the rest of the line for a token */
				/* only allow a comment or a newline */
				
				switch(linescan(&p, param)){
					case TOK_NL:
					case TOK_COMMENT:
						break;

					default:
						debug(DEBUG_UNEXPECTED,"only newline or comment token is valid after a section token");
						syntax_error(linenum);
				}

				/* Search the key table for a match */
				
				for(sl = sectlist; sl->section != NULL; sl++){
					if(!strcmp(sl->section, section))
						break;
				}

				/* Set kl pointing to a keylist */
				/* if a matching section is found */
				/* else NULL if no matching section found */

				if(sl->section != NULL)
					kl = sl->keylist;
				else
					kl = NULL;
				break; 

			case TOK_KEY:					
				strncpy(key, param, MAX_KEY - 1); // Save the key
				key[MAX_KEY - 1] = 0;
			
				/* Next token had better be a value */

				switch(linescan(&p, param)){
					case TOK_VALUE:
						/* Save value */
						strncpy(value, param, MAX_VALUE - 1);
						value[MAX_VALUE - 1] = 0;
						break;
					default:
						debug(DEBUG_UNEXPECTED, "should have received a value token");
						syntax_error(linenum);
				}

				/* Next token had better be a */
				/* newline or comment */

				switch(linescan(&p, param)){
					case TOK_NL:
					case TOK_COMMENT:
						break;
					default:
						debug(DEBUG_UNEXPECTED, "invalid token found while parsing a key/value");
						syntax_error(linenum);
				}

				/* We now have a key/value pair */
				/* if kl is pointing to a valid */
				/* key table, attempt to match the */
				/* key to it. */

				if(kl != NULL){ /* Valid section ? */
					for(ck = kl; ck->key != NULL; ck++){
						if(!strcmp(key,ck->key))
							break;
					}
					if(ck->key != NULL){ /* Key found ? */
						if(ck->action != NULL) /* Call user function if ptr != NULL */
							if((*ck->action)(value, ck->handling, ck->result)){
								debug(DEBUG_UNEXPECTED,"action function returned fail");
								syntax_error(linenum);
							}
					}
					else{
						debug(DEBUG_UNEXPECTED,"invalid key for %s",section);
						syntax_error(linenum);
					}
				}
				break;

			default:
				debug(DEBUG_UNEXPECTED,"top level default; bad token");
				syntax_error(linenum);
		}
	
	}			 

	if(ferror(conf_file))
		fatal_with_reason(errno, "I/O error on config_file");
	else
		fclose(conf_file);
}

/*
* Scan the line for tokens. Return a token code indicating what was
* found.
*/

static int linescan(char **lp, char *tokstring){
	int retval;
	
	*lp = eatwhite(*lp);

	switch(**lp){
		case '\n':
			/* New line */

			retval = TOK_NL;
			break;

		case ';': 
		case '#':
			/* Comment */

			retval = TOK_COMMENT;
			break;

		case '=':
			/* Value */
			
			*lp = eatwhite((++(*lp)));
			copyuntil(tokstring, lp, MAX_VALUE, " #;\t\n");
			retval = TOK_VALUE;
			break;


 		case '[':
			/* Section start */

			(*lp)++;
			if(copyuntil(tokstring, lp, MAX_SECTION, "]\n") == ']'){
				(*lp)++;
				retval = TOK_SECTION;
			}
			else{
				debug(DEBUG_UNEXPECTED, "Section not closed off");
				retval = TOK_ERR; // Section broken
			}
			break;

		default:
			/* Look for a key */

			if(isalpha(**lp)){
				copyuntil(tokstring, lp, MAX_KEY, "= \t\n");
				*lp = eatwhite(*lp);
				if(**lp == '=')
					retval = TOK_KEY;
				else
					retval = TOK_ERR; // Key broken
			}
			else
				retval = TOK_ERR; // Not something valid

			break;
	}				
	return retval;
}					

/*
* Copy a string from src pointer to a pointer to dest of a maximum
* specified length looking for one or more stop characters.
* Do not copy the stop character to the destination
* and always terminate the destination with a NUL character. Return the
* character the copy stopped on. In the case of no stop character
* match, return a NUL.
*/

static char copyuntil(char *dest, char **srcp, int max_dest_len, char *stopchrs){

	char *p = "";
	int i;

	/* Note: max_dest_len check below accounts for NUL at eos. */

	for(i = 0; i < max_dest_len - 1; i++){

		/* If NUL at current src string pos, stop copy */
		/* and point p to the NUL character in the stop char string */

		if(**srcp == '\0'){
			p = stopchrs + strlen(stopchrs); // Point to NUL in string
			break;
		}

		/* Check for one of the stop characters */

		for(p = stopchrs; *p != '\0'; p++){
			if(**srcp == *p)
				break;
		}

		/* If a stop character was matched, *p will be nz */
		/* if *p is zero, then copy the character to the */
		/* destination string */

		if(*p)
			break;	
		else
			*dest++ = *(*srcp)++;
			
	}

	/* NUL terminate the destination string */
	
	*dest = 0;
	return *p; // Return character which stopped copy
}

			
/*
* Eat up the white space characters in the line
*/
					
static char *eatwhite(char *curlinepos){

	while((*curlinepos == ' ') || (*curlinepos == '\t'))
		curlinepos++;
	return curlinepos;
}

/*
* Print a syntax error message with a line number, and exit
*/

void syntax_error(int linenum){
	
	fatal("Configuration file syntax error on line %d\n", linenum);
}

