
/*
 * irrconfscan.h Created 9/10/99
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

#ifndef IRRCONFSCAN_H
#define IRRCONFSCAN_H

typedef struct irr_controller_listent Irr_Controller_Listent;
typedef struct irr_valve_listent Irr_Valve_Listent;
typedef struct irr_program_listent Irr_Prog_Listent;
typedef struct irr_prog_sequence Irr_Prog_Sequence;
typedef struct irr_season_listent Irr_Season_Listent;

struct irr_controller_listent{
	char *name;
	unsigned short nodeaddress;
	unsigned short pumpmaster_defined;
	unsigned short pumpmaster;
  unsigned short firmwarelevel;
	struct irr_controller_listent *next;
};

struct irr_valve_listent{
	char *name;
	unsigned short valve;
	struct irr_controller_listent *controller;
	struct irr_valve_listent *next;
};

struct irr_prog_sequence{
	unsigned short duration;
	struct irr_valve_listent *valve;
	struct irr_prog_sequence *next;
};


struct irr_program_listent{
	char *name;
	struct irr_prog_sequence *seq;
	struct irr_program_listent *next;
};
	
struct irr_season_listent{
	char *name;
	unsigned short first;
	unsigned short last;
	unsigned short factor;
	struct irr_season_listent *next;
};

/* Function prototypes */

extern void irrconf_scan(char *confpath);
extern Irr_Controller_Listent *irrconf_pumpmaster(void);
extern Irr_Prog_Listent *irrconf_find_program(char *progname);
extern Irr_Valve_Listent *irrconf_find_valve(char *valvename);
extern unsigned short irrconf_seasonfactor(unsigned short month);
extern Irr_Valve_Listent *irrconf_index_valve_list(int n);

#endif	
	
	

