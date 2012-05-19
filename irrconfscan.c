
/*
 * irrconfscan.c Created 9/10/99
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
#include <malloc.h>
#include <stdlib.h>

#include "irrconfscan.h"
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
#define TOK_KEY2 5


/* Consistency flags */

#define	NAME_CFLAG		1
#define	NODEADDRESS_CFLAG	2
#define PUMPMASTER_CFLAG	4
#define VALVE_CFLAG		8
#define DURATION_CFLAG		0x10
#define FIRST_CFLAG		0x20
#define LAST_CFLAG		0x40
#define FACTOR_CFLAG		0x80

#define CONTROLLER_CONSISTENCY	(NAME_CFLAG | NODEADDRESS_CFLAG | VALVE_CFLAG)
#define PROGRAM_CONSISTENCY (NAME_CFLAG | DURATION_CFLAG)
#define SEASON_CONSISTENCY (NAME_CFLAG | FIRST_CFLAG | LAST_CFLAG | FACTOR_CFLAG)


typedef struct sectionent Section_Entry;
typedef struct keyent Key_Entry;

/* Entry for a key table entry */

struct	keyent{
	char *key;
	int(*action)(char *ident, char *value, unsigned *consistency);
	};

/* Entry for a section block table */

struct sectionent{
	char *section;
	struct keyent *keylist;
	unsigned consistency;
};





/* Internal functions */

static int linescan(char **lp, char *tokstring);
static char *eatwhite(char *curlinepos);
static void syntax_error(int linenum, char *reason);
static char copyuntil(char *dest, char **srcp, int max_dest_len, char *stopchrs);
static int checknamedefn(unsigned *consistency);
static int addValve(char *ident, char *value, unsigned *consistency);
static int addCName(char *ident, char *value, unsigned *consistency);
static int addNodeAddress(char *ident, char *value, unsigned *consistency);
static int addPumpMaster(char *ident, char *value, unsigned *consistency);
static int addPName(char *ident, char *value, unsigned *consistency);
static int addDuration(char *ident, char *value, unsigned *consistency);
static int addSname(char *ident, char *value, unsigned *consistency);
static int addFirst(char *ident, char *value, unsigned *consistency);
static int addLast(char *ident, char *value, unsigned *consistency);
static int addFactor(char *ident, char *value, unsigned *consistency);





/* Internal variables */

static char *path = NULL;
static char *actionreason = NULL;

static Irr_Controller_Listent *controllerListHead = NULL;
static Irr_Prog_Listent *programListHead = NULL;
static Irr_Valve_Listent *valveListHead = NULL;
static Irr_Season_Listent *seasonListHead = NULL;

static Irr_Controller_Listent *newControllerEntry = NULL;
static Irr_Prog_Listent *newProgramEntry = NULL;
static Irr_Valve_Listent *newValveEntry = NULL;
static Irr_Season_Listent *newSeasonEntry = NULL;

static Key_Entry controllerkeys[] = {
{"NAME", addCName},
{"NODEADDRESS", addNodeAddress},
{"PUMPMASTER", addPumpMaster},
{"VALVE", addValve},
{NULL, NULL}
};

static Key_Entry programkeys[] = {
{"NAME", addPName},
{"DURATION", addDuration},
{NULL, NULL}
};

static Key_Entry seasonkeys[] = {
{"NAME", addSname},
{"FIRST", addFirst},
{"LAST", addLast},
{"FACTOR", addFactor},
{NULL, NULL}
};

static Section_Entry sectlist[] = {
{"CONTROLLER", controllerkeys, CONTROLLER_CONSISTENCY},
{"PROGRAM",programkeys, PROGRAM_CONSISTENCY},
{"SEASON",seasonkeys, SEASON_CONSISTENCY},
{NULL, NULL}
};

static char *dupnamedef = "Name for section previously defined";
static char *memallocerr = "Memory allocation error";
static char *idnotallowed = "Identifier not allowed";
static char *idrequired = "Identifier required";
static char *monthoutofrange = "Month must be between 1 and 12";
/*
* Start of code
*/

/*
* Internal functions
*/

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
			/* Look for a key = val or key ident = val */

			if(isalpha(**lp)){
				copyuntil(tokstring, lp, MAX_KEY, "= \t\n");
				*lp = eatwhite(*lp);
				if(**lp == '=')
					retval = TOK_KEY;
				else
					retval = TOK_KEY2; // key ident = val?
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

void syntax_error(int linenum, char *reason){
	
	fatal("Error in %s(%d): %s\n", path, linenum, reason);
}

/*
* Check for the presence of a name definition
*/

int checknamedefn(unsigned *consistency){

	if(*consistency & NAME_CFLAG)
		return PASS;
	actionreason = "Name must be defined first";
	return FAIL;
}

/*
* Add a valve to the valve list
*/

static int addValve(char *ident, char *value, unsigned *consistency){

	Irr_Valve_Listent *p; 

	/* Name must be defined first */

	if(checknamedefn(consistency))
		return FAIL;

	/* Report error if identifier is not present */

	if(ident[0] == '\0'){
		actionreason = idrequired;
		return FAIL;
	}

	/* Allocate memory for the new valve entry */
	

	if((newValveEntry = malloc(sizeof(Irr_Valve_Listent))) == NULL)
		fatal(memallocerr);
	
	/* Insert new valve in list, looking for duplicate entries */
		
	if(valveListHead == NULL)		
		valveListHead = newValveEntry;
	else{
	
		for(p = valveListHead; ; p = p->next){
		
			/* If valve exists by that name, it is an error */
			if(!strcmp(p->name, ident)){
				actionreason = dupnamedef;
				return 1;
			}
			if(p->next == NULL)
				break;

			
		}
		p->next = newValveEntry;
	}
	
	/* Insert name into data structure */
	
	if((newValveEntry->name  = strdup(ident)) == NULL)
		fatal(memallocerr); 

	debug(DEBUG_EXPECTED,"inserted valve %s",newValveEntry->name);

	/* Mark the end of the list */
	
	newValveEntry->next = NULL;
	
	/* Make a link to the controller for this valve */
	
	newValveEntry->controller = newControllerEntry;
	
	/* Save the valve number */
	
	newValveEntry->valve = (unsigned short) atoi(value);

	*consistency |= VALVE_CFLAG;

	return PASS;
	
}

/*
* Add the controller name
*/

static int addCName(char *ident, char *value, unsigned *consistency){


	Irr_Controller_Listent *p; 

	/* Report error if more than 1 name in section */

	if(*consistency & NAME_CFLAG){
		actionreason = dupnamedef;
		return FAIL;
	}

	/* Report error if identifier is present */

	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}

	/* Allocate memory for the new controller entry */
	

	if((newControllerEntry = malloc(sizeof(Irr_Controller_Listent))) == NULL)
		fatal(memallocerr);
	
	/* Insert new controller in list, looking for duplicate entries */
		
	if(controllerListHead == NULL)		
		controllerListHead = newControllerEntry;
	else{
	
		for(p = controllerListHead; ; p = p->next){
		
			/* If controller exists by that name, it is an error */
			if(!strcmp(p->name, value)){
				actionreason = dupnamedef;
				return 1;
			}
			if(p->next == NULL)
				break;

			
		}
		p->next = newControllerEntry;
	}
	
	/* Insert name into data structure */

	
	if((newControllerEntry->name  = strdup(value)) == NULL)
		fatal(memallocerr); 

	debug(DEBUG_EXPECTED,"inserted controller %s",newControllerEntry->name);

  newControllerEntry->firmwarelevel = 0;  // Assume lowest level.

	newControllerEntry->pumpmaster_defined = 0;
	
	newControllerEntry->next = NULL;

	*consistency |= NAME_CFLAG;
	return PASS;
	
}

/*
* Add the controller node address
*/

static int addNodeAddress(char *ident, char *value, unsigned *consistency){

	/* Name must be defined first */

	if(checknamedefn(consistency))
		return FAIL;


	if(*consistency & NODEADDRESS_CFLAG){
		actionreason = dupnamedef;
		return FAIL;
	}

	/* Report error if identifier is present */

	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}

	newControllerEntry->nodeaddress = (unsigned short) atoi(value);
	

	*consistency |= NODEADDRESS_CFLAG;
	return PASS;
}

/*
* Add the controller pumpmaster address
*/

static int addPumpMaster(char *ident, char *value, unsigned *consistency){

	/* Name must be defined first */

	if(checknamedefn(consistency))
		return FAIL;

	/* Can't be already defined */

	if(newControllerEntry->pumpmaster_defined){
		actionreason = dupnamedef;
		return FAIL;
	}

	/* Report error if identifier is present */

	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}

	newControllerEntry->pumpmaster_defined = 1;
	
	newControllerEntry->pumpmaster = (unsigned short) atoi(value);
	
	return PASS;

}
	
/*
* Add the program name
*/

static int addPName(char *ident, char *value, unsigned *consistency){

	Irr_Prog_Listent *p; 

	/* Report error if more than 1 name in section */

	if(*consistency & NAME_CFLAG){
		actionreason = dupnamedef;
		return FAIL;
	}

	*consistency |= NAME_CFLAG;

	/* Report error if identifier is present */

	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}

	/* Allocate memory for the new program entry */
	

	if((newProgramEntry = malloc(sizeof(Irr_Prog_Listent))) == NULL)
		fatal(memallocerr);
	
	/* Insert new program in list, looking for duplicate entries */
	
	
	if(programListHead == NULL)		
		programListHead = newProgramEntry;
	else{
	
		for(p = programListHead; ; p = p->next){

		
			/* If program exists by that name, it is an error */
			if(!strcmp(p->name, value)){
				actionreason = dupnamedef;
				return FAIL;
			}
			if(p->next == NULL)
				break;

		}
		p->next = newProgramEntry;
	}
	
	/* Insert name into data structure */

	if((newProgramEntry->name  = strdup(value)) == NULL)
		fatal(memallocerr); 

	debug(DEBUG_EXPECTED,"inserted program %s",newProgramEntry->name);

	newProgramEntry->next = NULL;
	newProgramEntry->seq = NULL;
	

	return PASS;
	
}

/*
* Add the duration
*/

static int addDuration(char *ident, char *value, unsigned *consistency){

	Irr_Prog_Sequence *p, *newDurationEntry; 


	/* Name must be defined first */

	if(checknamedefn(consistency))
		return FAIL;



	/* Report error if identifier is not present */

	if(ident[0] == '\0'){
		actionreason = idrequired;
		return FAIL;
	}

	/* Allocate memory for the new duration entry */
	

	if((newDurationEntry = malloc(sizeof(Irr_Prog_Sequence))) == NULL)
		fatal(memallocerr);
	
	/* Insert new duration in list */
		
	if(newProgramEntry->seq == NULL)		
		newProgramEntry->seq = newDurationEntry;
	else{
	
		for(p = newProgramEntry->seq; ; p = p->next){
		
			if(p->next == NULL)
				break;
		}
		p->next = newDurationEntry;
	}
	
	/* Mark the end of the list */
	
	newDurationEntry->next = NULL;
	
	/* Search the valve list for the valve */

	if((newDurationEntry->valve = irrconf_find_valve(ident)) == NULL){
		actionreason = "Can't find valve in valve list";
		return FAIL;
	}
	
	/* Save the duration */
	
	newDurationEntry->duration = (unsigned short) atoi(value);
	
	debug(DEBUG_EXPECTED,"inserted duration of %s minutes for valve %s",
	value, ident);
	
	*consistency |= DURATION_CFLAG;
	return PASS;
	
}

/*
* Add a season name to the season list
*/

static int addSname(char *ident, char *value, unsigned *consistency){


	Irr_Season_Listent *p; 

	/* Report error if more than 1 name in section */

	if(*consistency & NAME_CFLAG){
		actionreason = dupnamedef;
		return FAIL;
	}

	*consistency |= NAME_CFLAG;

	/* Report error if identifier is present */

	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}

	/* Allocate memory for the new season entry */
	

	if((newSeasonEntry = malloc(sizeof(Irr_Season_Listent))) == NULL)
		fatal(memallocerr);
	
	/* Insert new season in list, looking for duplicate entries */
	
	
	if(seasonListHead == NULL)		
		seasonListHead = newSeasonEntry;
	else{
	
		for(p = seasonListHead; ; p = p->next){

		
			/* If program exists by that name, it is an error */
			if(!strcmp(p->name, value)){
				actionreason = dupnamedef;
				return FAIL;
			}
			if(p->next == NULL)
				break;

		}
		p->next = newSeasonEntry;
	}
	
	/* Insert name into data structure */

	if((newSeasonEntry->name  = strdup(value)) == NULL)
		fatal(memallocerr); 

	debug(DEBUG_EXPECTED,"inserted season %s",newSeasonEntry->name);

	newSeasonEntry->next = NULL;

	return PASS;
	
}


/*
* Add a first month to the season list
*/

static int addFirst(char *ident, char *value, unsigned *consistency){
	int month = atoi(value);
	
	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}
	
	if((month < 1)||(month > 12))
		actionreason = monthoutofrange;
		
	newSeasonEntry->first = (unsigned short) month;
	
	*consistency |= FIRST_CFLAG;
	
	return PASS;
	
}

/*
* Add a last month to the season list
*/

static int addLast(char *ident, char *value, unsigned *consistency){
	
	int month = atoi(value);
	
	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}
	
	if((month < 1)||(month > 12))
		actionreason = monthoutofrange;
		
	newSeasonEntry->last = (unsigned short) month;
	
	*consistency |= LAST_CFLAG;

	return PASS;
	
}

/*
* Add a factor to the season list
*/

static int addFactor(char *ident, char *value, unsigned *consistency){
	int factor = atoi(value);
	
	if(ident[0]){
		actionreason = idnotallowed;
		return FAIL;
	}
	
	if((factor < 10)||(factor > 100))
		actionreason = "Factor must be between 10 and 100";
		
	newSeasonEntry->factor = (unsigned short) factor;
	
	*consistency |= FACTOR_CFLAG;
	return PASS;
	
}

/*
* Public functions
*/

void irrconf_scan(char *confpath){
	FILE *conf_file;
	char *p;
	Section_Entry *sl = NULL;
	Key_Entry *ck,*kl = NULL;
	int linenum, tok1;
	unsigned section_consistency;
	static char line[MAX_CONFIG_LINE];
	static char param[MAX_PARAM];
	static char section[MAX_SECTION];
	static char ident[MAX_KEY];
	static char key[MAX_KEY];
	static char value[MAX_VALUE];

	path = confpath;
	
	/* Open the config file */

	if((conf_file = fopen(confpath, "r")) == NULL)
		fatal_with_reason(errno, "Can't open config file: %s",confpath);
	
	for(linenum = 1; ; linenum++){
		if(fgets(line, MAX_CONFIG_LINE, conf_file) == NULL)
			break;
		
		p = line;
		
		/* Parse tree root */

		switch((tok1 = linescan(&p, param))){

			/* It was a newline or a comment, get another line */

			case TOK_NL:
			case TOK_COMMENT:
				break;

			/* It was a section ID, get it, and try to match it */
			
			case TOK_SECTION:
			
				if(sl != NULL) // Transistioning to another section?
					if(section_consistency != sl->consistency)
						syntax_error(linenum,"Section missing a required key");
			
				section_consistency = 0; // Start new
				
				debug(DEBUG_EXPECTED, "Found section %s.",param);
				strncpy(section, param, MAX_SECTION - 1); // Save param
				section[MAX_SECTION -1] = 0;

				/* Scan the rest of the line for a token */
				/* only allow a comment or a newline */
				
				switch(linescan(&p, param)){
					case TOK_NL:
					case TOK_COMMENT:
						break;

					default:
						debug(DEBUG_UNEXPECTED,"only newline or comment token is valid after a section token");
						syntax_error(linenum, "Extra characters after a section identifier");
				}

				/* Search the sectlist table for a match */
	
				for(sl = sectlist; sl->section != NULL; sl++){
					if(!strcasecmp(sl->section, section))
						break;
				}
				/* Set kl pointing to a keylist */
				/* if a matching section is found */
				/* else error if no matching section found */

				if(sl->section != NULL)
					kl = sl->keylist;
				else
					syntax_error(linenum,"Unrecognized section header");
				break; 

			case TOK_KEY:
			case TOK_KEY2:
				debug(DEBUG_EXPECTED,"Found key: %s",param);
			
				/* parse either key = val or key ident = val */
				
				ident[0] = '\0';
										
				strncpy(key, param, MAX_KEY - 1); // Save the key
				key[MAX_KEY - 1] = 0;
				
				if(tok1 == TOK_KEY2){
					switch(linescan(&p, param)){
						case TOK_KEY:
							strncpy(ident, param, MAX_KEY - 1);
							ident[MAX_KEY - 1] = 0;
							break;
								
						default:
							syntax_error(linenum, "Identifier must follow key");
							break;		
					}
				}	
				/* Next token had better be a value */

				switch(linescan(&p, param)){
				
					case TOK_VALUE:
						/* Save value */
						strncpy(value, param, MAX_VALUE - 1);
						value[MAX_VALUE - 1] = 0;
						break;
					default:
						debug(DEBUG_UNEXPECTED, "should have received a value token");
						syntax_error(linenum,"Missing value");
				}

				/* Next token had better be a */
				/* newline or comment */

				switch(linescan(&p, param)){
					case TOK_NL:
					case TOK_COMMENT:
						break;
					default:
						debug(DEBUG_UNEXPECTED, "invalid token found while parsing a key/value");
						syntax_error(linenum,"Only newline or comment allowed after an assigment");
				}

				/* We now have a key/value pair or */
				/* a key/ident/value triplett */
				/* if kl is pointing to a valid */
				/* key table, attempt to match the */
				/* key to it. */

				if(kl != NULL){ /* Valid section ? */
					for(ck = kl; ck->key != NULL; ck++){
						if(!strcasecmp(key,ck->key))
							break;
					}
					if(ck->key != NULL){ /* Key found ? */
						if(ck->action != NULL) /* Call user function if ptr != NULL */
							if((*ck->action)(ident, value, &section_consistency)){
								debug(DEBUG_UNEXPECTED,"action function returned fail");
								syntax_error(linenum, actionreason);
							}
					}
					else{
						debug(DEBUG_UNEXPECTED,"Invalid key for %s",section);
						syntax_error(linenum,"Invalid key in section");
					}
				}
				else{
					debug(DEBUG_UNEXPECTED,"No section header seen");
					syntax_error(linenum,"No section header preceding assignment");
				}
					
				break;

			default:
				debug(DEBUG_UNEXPECTED,"top level default; bad token");
				syntax_error(linenum,"Unrecognized construct");
		}
	
	}			 
			
	if(sl != NULL) // Check last section processed
		if(section_consistency != sl->consistency)
			syntax_error(linenum,"Section missing a required key");

	if(ferror(conf_file))
		fatal_with_reason(errno, "I/O error on config_file");
	else
		fclose(conf_file);
}

/*
* Try to match a program name to a program in the program list
*/

Irr_Prog_Listent *irrconf_find_program(char *progname){
	
	Irr_Prog_Listent *p;
	
	for(p = programListHead; ; p = p->next){
		if(!strcmp(p->name, progname))
			break;
		if(p->next == NULL)
			return NULL;
		}
	return p;
}

/*
* Try to match a valve name to a valve in the valve list
*/

Irr_Valve_Listent *irrconf_find_valve(char *valvename){
	
	Irr_Valve_Listent *p;
	
	for(p = valveListHead; ; p = p->next){
		if(!strcmp(p->name, valvename))
			break;
		if(p->next == NULL)
			return NULL;
		}
	return p;
}

/*
* Return a pointer to the nth valve entry from the valve list.
* Return NULL if the end of the list is reached
*/

Irr_Valve_Listent *irrconf_index_valve_list(int n){

  Irr_Valve_Listent *p;
  int i;
  
  for(p = valveListHead, i = 0 ; (i != n) && (p != NULL) ; p = p->next, i++)
    debug(DEBUG_EXPECTED, "n = %d, p->name = %s", n, p->name);
  
  return p;
}
    
    

/*
* Try to find a controller with a pumpmaster definition 
*/

Irr_Controller_Listent *irrconf_pumpmaster(void){
	
	Irr_Controller_Listent *p;
	
	for(p = controllerListHead; ; p = p->next){
		if(p->pumpmaster_defined)
			break;
		if(p->next == NULL)
			return NULL;
		}
	return p;
}

/*
* Return a factor for a given month (1-12) based on the season list
* entries. If none is found, return a factor of 100
*/

unsigned short irrconf_seasonfactor(unsigned short month){
	Irr_Season_Listent *p;
	unsigned short factor = 100;

	for(p = seasonListHead; p != NULL; p = p->next){
		if((month >= p->first)&&(month <= p->last)){
			factor = p->factor;
			break;
		}
	}
	debug(DEBUG_EXPECTED,"Using a season factor of %d",(int) factor);
	return factor;
}  		
