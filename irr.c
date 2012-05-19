/*
 * irr.c.  Sprinkler control program for the home automation network (HAN).
 *
 * Copyright (C) 1999,2011 Stephen Rodgers
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
 * Stephen Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 * $Id$
 */
#include "tnd.h"
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <popt.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "error.h"
#include "socket.h"
#include "confscan.h"
#include "han.h"
#include "irrconfscan.h"
#include "statevar.h"
#include "hanclient.h"
#include "pid.h"

/* Local Defines */

#define IRR_HELP 'h' 
#define IRR_VERSION 'v'
#define IRR_IRRIGATE 'i'
#define IRR_LIST_VALVES 'l'
#define IRR_TEST_VALVE 't'
#define IRR_CHECK_SYNTAX 'c'
#define IRR_PRESSURIZE 'p'
#define IRR_SUSPEND 's'

#define CONF_STRING	1
#define CONF_FLAG 2
#define CONF_INTEGER	3


#define VALVE_OPEN 1
#define VALVE_CLOSED 0


/* Global variables */

int debuglvl = 0;
char *progname;	// Pointer to argv[0] for error.c
char *version_string = VERSION;


/* Local prototypes. */
/* static int confFlag(char *value, short handling, void *result); */
static int confOrFlag(char *value, short handling, void *result);
static int confSaveString(char *value, short handling, void *result);
static int confSaveInteger(char *value, short handling, void *result);
static void irrShowHelp(void);
static void irrShowVersion(void);
static void irrExitRequest(int sig);
static void irrHupRequest(int sig);
static int irrRefreshController(Irr_Controller_Listent *controller);
static Irr_Controller_Listent *irrCommandValve(char *valvename, unsigned short state);
static Irr_Controller_Listent *irrPumpmaster(unsigned short state);
static void checkHandInfo();
static void irrPressurize(int argc, char **argv);
static void irrTestValve(int argc, char **argv);
static void irrListValves(void);
static void irrIrrigate(int argc, char **argv);
static void irrSuspend( int argc, char **argv);


static void irrExecuteCommand(int command, int argc, char **argv);

/* Internal global variables */

// Signal handler tables

struct sigaction term_sig_action = {{irrExitRequest}, {{0}}, 0, NULL};

struct sigaction quit_sig_action = {{irrExitRequest}, {{0}}, 0, NULL};

struct sigaction int_sig_action =  {{irrExitRequest}, {{0}}, 0, NULL};

struct sigaction hup_sig_action =  {{irrHupRequest}, {{0}}, 0, NULL};


static char defaultConfFile[MAX_CONFIG_STRING] = CONF_FILE_PATH;
static char *newConfFile = NULL;
static State_Var *sv_handle = NULL;
static char *suspendUntil = "suspend_until";

static char irrConfFilePath[MAX_CONFIG_STRING] = IRR_CONF_FILE_PATH;
static char confPidPath[MAX_CONFIG_STRING] = CONF_PID_PATH;
static char confSocketPath[MAX_CONFIG_STRING] = "";
static char confStateFilePath[MAX_CONFIG_STRING] = IRR_STATE_FILE_PATH;
static char confService[MAX_CONFIG_STRING] = "";
static char confHost[MAX_CONFIG_STRING] = "";
static int conf_nocheck = 0;
static int confInterValveDelay = 15; 
static int confPumpmasterDelay = 15;
static int irrBackground = 0;
static unsigned short termRequest = 0;


static char *commandLineParseErr = "Command line parse error: ";
static char *parmsbad = "Incorrect number of parameters";

/* Command line option table */

static struct poptOption irrOptions[] = {
	{"background", 'b', POPT_ARG_NONE,&irrBackground,0},
	{"nocheck", 'n', POPT_ARG_NONE, &conf_nocheck,0},
	{"check-syntax", 'c',POPT_ARG_NONE, NULL,IRR_CHECK_SYNTAX},
	{"debug", 'd', POPT_ARG_INT, &debuglvl, 0},
	{"help", 'h', POPT_ARG_NONE, NULL, IRR_HELP },
	{"irrigate", 'i', POPT_ARG_NONE, NULL, IRR_IRRIGATE},
	{"list-valves",'l',POPT_ARG_NONE, NULL, IRR_LIST_VALVES},
	{"pressurize", 'p', POPT_ARG_NONE, NULL, IRR_PRESSURIZE},
	{"suspend", 's', POPT_ARG_NONE, NULL, IRR_SUSPEND},
	{"test-valve", 't', POPT_ARG_NONE, NULL, IRR_TEST_VALVE},
	{"version", 'v', POPT_ARG_NONE, NULL, IRR_VERSION},
	{NULL, '\0', 0, NULL, 0}
};
 
/* Configuration option tables */

static Key_Entry	globalKeys[] = {
	{"pid_file", CONF_STRING, confPidPath, confSaveString},
	{"socket", CONF_STRING, confSocketPath, confSaveString},
	{"service", CONF_STRING, confService, confSaveString},
	{"textservice", 0, NULL, NULL},
	{"host", CONF_STRING, confHost, confSaveString},
	{"nocheck",CONF_FLAG, &conf_nocheck, confOrFlag},
	{NULL, 0, NULL, NULL}
};

static Key_Entry	irrKeys[] = {
	{"conf_file", CONF_STRING, irrConfFilePath, confSaveString},
	{"inter_valve_delay", CONF_INTEGER, &confInterValveDelay, confSaveInteger},
	{"pumpmaster_delay", CONF_INTEGER, &confPumpmasterDelay, confSaveInteger},
	{"state_file", CONF_STRING, confStateFilePath, confSaveString},
	{NULL, 0, NULL, NULL}
};

static Section_Entry	sectionHeader[] = {
	{"global", globalKeys},
	{"irr", irrKeys},
	{NULL, NULL}
};

/*
*
*
*
* START OF CODE
*
*
*
*/

/* Save a configuration flag */

/*
static int confFlag(char *value, short handling, void *result){
	int *p = (int *) result;
	if(!strcmp(value, "yes"))
		*p = 1;
	else if(!strcmp(value, "no"))
		*p = 0;
	else
		*p = (unsigned short)atoi(value);
	return 0;
}
*/

/* Save a configuration flag, but only allow it to be set, not reset */

static int confOrFlag(char *value, short handling, void *result){
	int *p = (int *) result;

	if(*p == 0){
		if(!strcmp(value, "yes"))
			*p = 1;
		else if(!strcmp(value, "no"))
			*p = 0;
		else
			*p = atoi(value);
	}
	return PASS;
}


/* Save a configuration string option */

static int confSaveString(char *value, short handling, void *result){
	char *dest = (char *) result;

	strcpy(dest, value);
	debug(DEBUG_EXPECTED,"set string: %s",dest);
	return PASS;
}

/* Save a configuration integer option */

static int confSaveInteger(char *value, short handling, void *result){

	*((int *)result) = atoi((char *) value);
	
	debug(DEBUG_EXPECTED,"set integer: %d",*((int *) result));
	return PASS;
}


/* Show the help screen for irr. */

static void irrShowHelp(void) {
	printf("'irr' is a utility that controls sprinkler nodes\n");
	printf(" This program comes with ABSOLUTELY NO WARRANTY\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progname);
	printf("\n");
	printf("  -b, --background        run in the background without a controlling TTY\n");
	printf("  -c, --check-syntax      check syntax of irr.conf file\n");
	printf("  -d, --debug LEVEL       set the debug level, 0 is off, the\n");
	printf("                          compiled in default is %i and the max\n", DEBUGLVL);
	printf("                          level allowed is %i\n", DEBUG_MAX);
	printf("  -h, --help              give help on usage\n");
	printf("  -i, --irrigate program  run an irrigation program\n");
	printf("  -n, --nocheck           bypass hand version and sanity checking\n");
	printf("  -l, --list-valves       list valves defined in irr.conf\n");
	printf("  -p, --pressurize m      enable pumpmaster and pressurize line for m minutes\n");
	printf("  -s, --suspend +n        suspend programmed irrigation for n days\n");
	printf("  -t, --test-valve v d	  test a specific valve, v = valve d = duration\n");
	printf("  -v, --version           display program version\n");
	printf("\n");
	printf("Report bugs to <%s>\n", EMAIL);
	return;
}

/*
* Make a note of the exit request
*/

static void irrExitRequest(int sig){
	termRequest = 1;
}

/*
* Handle HUP as an exit request
*/

static void irrHupRequest(int sig){
	termRequest = 1;
}


/* Show the version info for irr. */

static void irrShowVersion(void) {
	printf("irr (%s) %s\n", PACKAGE, version_string);
}

/*
* Get Daemon info and see if we are compatible
*/

static void checkHandInfo(){

	Client_Command client_command;
	static char *not_comp = "Hand not compatible with irr";


	if(conf_nocheck)
		return;

	client_command.request = HAN_CCMD_DAEMON_INFO;

	if(hanclient_send_command_return_res(&client_command)){
		if(client_command.commstatus == HAN_CSTS_CMD_UNKNOWN)
			fatal(not_comp);
		else
			hanclient_error_check(&client_command);
	}


	if(strcmp(client_command.cmd.info.version, version_string)){
		warn("Version mismatch, irr %s, hand %s", version_string, client_command.cmd.info.version);
	}
	else{
		debug(DEBUG_EXPECTED,"Hand version: %s", client_command.cmd.info.version);
		if(client_command.cmd.info.cmdpktsize != sizeof(struct han_packet)){
			debug(DEBUG_UNEXPECTED, "Hand command structure incompatible");
			fatal(not_comp);
		}
		if(client_command.cmd.info.netscanpktsize != sizeof(struct han_netscan)){
			debug(DEBUG_UNEXPECTED, "Hand netscan structure incompatible");
			fatal(not_comp);
		}
		if(client_command.cmd.info.errstatssize != sizeof(struct err_stats)){
			debug(DEBUG_UNEXPECTED, "Hand error stats structure incompatible");
			fatal(not_comp);
		}
		if(client_command.cmd.info.ppowersize != sizeof(struct ppower_client_command)){
			debug(DEBUG_UNEXPECTED, "Hand ppower client command structure incompatible");
			fatal(not_comp);
		}
	}
}




/*
* Refresh a controller which has an open valve
*/

static int irrRefreshController(Irr_Controller_Listent *controller){


	Client_Command ccmd;

	ccmd.cmd.pkt.nodeaddress = controller->nodeaddress;



	ccmd.request = HAN_CCMD_SENDPKT;
	ccmd.cmd.pkt.nodeaddress = controller->nodeaddress;
	ccmd.cmd.pkt.nodecommand = HAN_CMD_RLYN_GPCY;
	ccmd.cmd.pkt.nodeparams[0] = 0;
	ccmd.cmd.pkt.numnodeparams = 1;
	
	hanclient_send_command(&ccmd);

	debug(DEBUG_EXPECTED, "Controller %s refreshed", controller->name);
	if(ccmd.commstatus){
		debug(DEBUG_UNEXPECTED,"Comm error during refresh, return code = %d\n", ccmd.commstatus);
		return FALSE;
	}
	debug(DEBUG_EXPECTED,"AC Failure occurred: %s",(ccmd.cmd.pkt.nodestatus[0]) ? "YES" : "NO");
	if(ccmd.cmd.pkt.nodestatus[0] > 0)  // Controller rebooted, return this fact.
		return TRUE;   // Controller rebooted, return this fact.
  	return FALSE;
}


/*
* Turn a valve on or off by name
* Return a pointer to the controller data if successful for refresh purposes
*/

static Irr_Controller_Listent *irrCommandValve(char *valvename, unsigned short state){

	Client_Command ccmd;
	Irr_Valve_Listent *valve;

	valve = irrconf_find_valve(valvename);

	if(valve == NULL)
		return NULL;


	ccmd.request = HAN_CCMD_SENDPKT;
	ccmd.cmd.pkt.nodeaddress = valve->controller->nodeaddress;
	ccmd.cmd.pkt.nodecommand = HAN_CMD_RLYN_SRLY;
	ccmd.cmd.pkt.numnodeparams = 2;
	ccmd.cmd.pkt.nodeparams[0] = valve->valve;
	ccmd.cmd.pkt.nodeparams[1] = state ? 0xFF : 0;
	
	hanclient_send_command(&ccmd);

	debug(DEBUG_EXPECTED, "Valve %s(%d) on controller nodeaddress %d %s",valvename, valve->valve, valve->controller->nodeaddress, (state) ? "OPEN":"CLOSED");

	irrRefreshController(valve->controller);

	return valve->controller;
}

/*
* Turn the pumpmaster valve on or off
* Return a pointer to the controller data if successful for refresh purposes
*/

static Irr_Controller_Listent *irrPumpmaster(unsigned short state){

	Client_Command ccmd;
	Irr_Controller_Listent *controller;

	controller = irrconf_pumpmaster();

	if(controller == NULL)
		return NULL;


	ccmd.request = HAN_CCMD_SENDPKT;
	ccmd.cmd.pkt.nodeaddress = controller->nodeaddress;
	ccmd.cmd.pkt.nodecommand = HAN_CMD_RLYN_SRLY;
	ccmd.cmd.pkt.numnodeparams = 2;
	ccmd.cmd.pkt.nodeparams[0] = controller->pumpmaster;
	ccmd.cmd.pkt.nodeparams[1] = state ? 0xFF : 0;
	
	hanclient_send_command(&ccmd);

	debug(DEBUG_EXPECTED, "Pumpmaster valve (%d) on controller %s is %s", controller->pumpmaster, controller->name, (state) ? "OPEN":"CLOSED");

	irrRefreshController(controller);
  
	return controller;
}


static void irrPressurize(int argc, char **argv){

	Irr_Controller_Listent *pmController;
	int duration;
	
	/* Only 1 arg allowed */
	
	if(argc != 1)
		fatal(parmsbad);

	duration = (unsigned short) atoi(argv[0]);
			
	if(duration == 0)
		fatal("Invalid duration for pressurization test");
	
	/* Open pumpmaster valve */
	
	pmController = irrPumpmaster(VALVE_OPEN);


	/* If no pumpmaster valve, then error */

	if(pmController == NULL)
		fatal("No pumpmaster valve defined");
		
	while((termRequest == 0)&&(duration--)){
		irrRefreshController(pmController);
		sleep(60);
		}		
		
	/* Close pumpmaster valve */
	
	irrPumpmaster(VALVE_CLOSED);
}

/* List all the valves in irr.conf */

static void irrListValves(void){

	Irr_Valve_Listent *p;
	int n;
  

	printf("\nUsing config file: %s\n\n", defaultConfFile);

	irrconf_scan(irrConfFilePath);
  
	for(n = 0 ; ; n++){
		if((p = irrconf_index_valve_list(n)) == NULL)
		break;

		if(n == 0){
			printf("VALVE NAME\t\t\t\tCONTROLLER\t\t\t\tVALVE NUMBER\n");
			printf("----------\t\t\t\t----------\t\t\t\t------------\n");
		}

		printf("%-24s\t\t%-24s\t\t%-5i\n",p->name, p->controller->name, (int) p->valve);
	}
	printf("\n");
}


static void irrTestValve(int argc, char **argv){

	Irr_Controller_Listent *pmController, *targetController;
	int duration, i;
	
	/* must have exactly 2 args */
	
	if(argc != 2)
		fatal(parmsbad);



	duration = (unsigned short) atoi(argv[1]);
			
	if(duration == 0)
		fatal("Invalid duration for valve test");
	
	/* Open pumpmaster valve if it exists */

	pmController = irrPumpmaster(VALVE_OPEN);
	

   	if((termRequest == 0)&&(pmController != NULL))
		sleep(confPumpmasterDelay);
	
	/* Open the desired valve */
	
	targetController = irrCommandValve(argv[0], VALVE_OPEN);
	
	if(targetController == NULL){
		irrPumpmaster(VALVE_CLOSED);
		fatal("No such valve: %s",argv[0]);
		}

	/* Wait in multiples of 60 seconds */
	
	for(i = 0; (termRequest == 0) && (i < duration); i++){
	
		sleep(60);
	
		if(termRequest != 0)
			break;
		
		irrRefreshController(targetController);
		if((pmController != NULL)&&(targetController != pmController))
			irrRefreshController(pmController);
	}

	/* Close pumpmaster if we have one */
	
	if(pmController != NULL){
		irrPumpmaster(VALVE_CLOSED);
		if(termRequest == 0)
			sleep(confPumpmasterDelay);
	}
	
	/* Close the desired valve */

	irrCommandValve(argv[0], VALVE_CLOSED);
}

/*
* Run an irrigation program
*/

static void irrIrrigate(int argc, char **argv){

	Irr_Controller_Listent *pmController, *targetController;
	Irr_Prog_Listent *program;
	Irr_Prog_Sequence *seq;
	u16 i, sleeptime;
	char *off_valve_name = NULL;
	char *value;
	u8 acfail;	
	time_t t;
	long sced;
	struct tm *now;

	/* Must have exactly 1 arg */
	
	if(argc != 1)
		fatal(parmsbad);

	t =  time(NULL);
	now = localtime(&t);
	debug(DEBUG_EXPECTED,"tm_year = %d, tm_yday = %d, tm_mon = %d", now->tm_year, now->tm_yday, now->tm_mon); 

	sleeptime = (( (int) irrconf_seasonfactor((unsigned short) now->tm_mon + 1)) * 60) / 100;
	debug(DEBUG_EXPECTED, "sleeptime (season-adjusted 60 sec interval) = %d",sleeptime);

	/* Check to see if we are suspend_until is in effect */
	
	value = statevar_lookup(sv_handle, suspendUntil);
	if(value != NULL){
		sced = atol(value);
		debug(DEBUG_EXPECTED,"sced = %ld", sced); 
		if(((sced / 1000) > (now->tm_year + 1900))||((sced % 1000) > now->tm_yday)){
			debug(DEBUG_EXPECTED,"suspend request still in effect");
			return;
		}
		else
			statevar_remove(sv_handle, suspendUntil);
	}
	
	/* Look up the program */
	
	program = irrconf_find_program(argv[0]);

	if(program == NULL)
		fatal("No such program: %s", argv[0]);

  pmController = irrconf_pumpmaster();
  if(pmController != NULL){
    irrPumpmaster(VALVE_OPEN);
		sleep(confPumpmasterDelay);
  }
	
	/* Start sequencing though the program */
	
	for(seq = program->seq;(termRequest == 0)&&(seq != NULL); seq = seq->next){

    
    
		targetController = irrCommandValve(seq->valve->name, VALVE_OPEN);
    
	
		/* If desired valve doesn't exist, the program's broken */
	
		if(targetController == NULL){
			irrPumpmaster(VALVE_CLOSED);
			panic("No such valve: %s",argv[0]);
		}

		
		/* Wait out the desired duration */
	
		debug(DEBUG_EXPECTED,"Sleep time = %u",sleeptime);
	
		for(i = 0; (termRequest == 0) && (i < seq->duration); i++){


			if(termRequest == 0)
				sleep(sleeptime);

			if(termRequest != 0)
				break;

			acfail = FALSE;
			if((pmController != NULL)&&(targetController != pmController)){
				if(irrRefreshController(pmController))
          			// Recover from a reboot of the pumpmaster controller
				acfail = TRUE;
          			irrPumpmaster(VALVE_OPEN);
          			sleep(confPumpmasterDelay);
			}
      
			if(irrRefreshController(targetController)){
				acfail = TRUE;
				// Recover from a reboot of the valve controller
				if((pmController != NULL)&&(targetController == pmController)){
					acfail = TRUE;
					irrPumpmaster(VALVE_OPEN);
					sleep(confPumpmasterDelay);
				}
			}

			if(acfail){
				debug(DEBUG_ACTION, "Re-enabling zone after AC failure");
				irrCommandValve(seq->valve->name, VALVE_OPEN);   
			}
                      
		}
		/*
		* Time up, turn off the valve, and see what we do next
		* If last valve in program, defer turn off until after
		* pumpmaster is shut off
		*/
	
		if((termRequest == 0)&&(seq->next != NULL)){
			irrCommandValve(seq->valve->name, VALVE_CLOSED);
			sleep(confInterValveDelay);
			}
		else
			off_valve_name = seq->valve->name;		
		
	}

	/*
	* Turn the pumpmaster off first to bleed pressure from the system
	*/
	
	if(pmController != NULL){
		irrPumpmaster(VALVE_CLOSED);
		if(termRequest == 0)
			sleep(confPumpmasterDelay);
	}
	/*
	* Turn off the last valve in the program
	* if it was enabled.
	*/
	
	if(off_valve_name != NULL) 
		irrCommandValve(off_valve_name, VALVE_CLOSED);
}

/*
* Suspend irrigation until some supplied future date 
*
* This is used for example when rainfall has occurred 
*
*/

static void irrSuspend( int argc, char **argv){

	int year, targetYear, targetYearDay, suspendDays = 0, maxYearDay = 364;
	time_t t;
	struct tm *now;
	char value[64];

	/* Get time now */
		
	t =  time(NULL);
	now = localtime(&t);
	
	/* Only 1 argument permitted */
		
	if(argc != 1)
		fatal(parmsbad);
	
	/* must start with a + followed by an integer */
	
	if(*argv[0] == '+'){
		suspendDays = atoi(argv[0]+1);
		if(suspendDays < 0 || suspendDays > 30)
			fatal("Number of days must be between 0 and 30");
	}
	else
		fatal(parmsbad);

	/* get the current year, and set the max day 364/365 in it */
		
	year = 1900 + now->tm_year;
	
	if((year % 4 == 0)&&(year % 400)&&(year % 1000 == 0))
		maxYearDay++; /* Leap year */

	/* Figure out when to cancel the suspension */
	
	targetYearDay = now->tm_yday + suspendDays;
	targetYear = now->tm_year;

	/* Handle a wrap into a new year */

	if(targetYearDay > maxYearDay){
		targetYear++;
		targetYearDay =- maxYearDay;
	}

	/* Convert future date into a string YYYYDDD */
		
	sprintf(value,"%d%03d",	targetYear, targetYearDay);
	
	/* Add a new entry to the list, or replace the existing one */

	statevar_add(sv_handle, suspendUntil, value);
}


/* Execute a command */

static void irrExecuteCommand(int command, int argc, char **argv){

	int retval;
	
	/* Parse the han configuration file */

	confscan(newConfFile != NULL ? newConfFile : defaultConfFile, sectionHeader);

	/* Load any state variables */
	
	sv_handle = statevar_load(confStateFilePath);

	/* Parse the irr configuration file */
	
 	irrconf_scan(irrConfFilePath);

	/* If background mode requested, set it up */

	if(irrBackground){
		switch(command){
			case IRR_IRRIGATE:
			case IRR_TEST_VALVE:
 			case IRR_PRESSURIZE:
				break;

			default:
				fatal("-b/--background not valid with -c, or -s");
		}

		// Fork and exit the parent

		if((retval = fork())){
			if(retval > 0)
				exit(0);  // Exit parent
			else
				fatal_with_reason(errno, "parent fork");
		}


		// The child creates a new session leader
		// This divorces us from the controlling TTY

		if(setsid() == -1)
			fatal_with_reason(errno, "creating session leader with setsid");


		// Fork and exit the session leader, this prohibits
		// reattachment of a controlling TTY.

		if((retval = fork())){
			if(retval > 0)
        			exit(0); // exit session leader
			else
		fatal_with_reason(errno, "session leader fork");
    		}

		// Change to the root of all file systems to
		// prevent mount/unmount problems.

		if(chdir("/"))
			fatal_with_reason(errno, "chdir to /");

		// set the desired umask bits

			umask(022);

		// Close STDIN, STDOUT, and STDERR

		close(0);
		close(1);
		close(2);
	} 
	
	/* Set up the socket type configured */

	hanclient_connect_setup(confPidPath, confSocketPath, confService, confHost);

	checkHandInfo();

	/* Act on the command */
	
	switch(command){
		case IRR_CHECK_SYNTAX:
			break;
	
		case IRR_IRRIGATE:
			irrIrrigate(argc, argv);
			break;
			
		case IRR_TEST_VALVE:
			irrTestValve(argc, argv);
			break;
		
		case IRR_PRESSURIZE:
			irrPressurize(argc, argv);
			break;		

		case IRR_SUSPEND:
			irrSuspend(argc, argv);
			break;

			
		default:
			panic("Unrecognized command %d",command);
			
	}
	statevar_sync(sv_handle);
}

int main(int argc, char *argv[])
{
 	int i, option, command;
	poptContext context;
	char *leftovers[10];


	/* Hook signals */
  
	sigaction(SIGHUP, &hup_sig_action, NULL);
	sigaction(SIGINT, &int_sig_action, NULL);
	sigaction(SIGQUIT, &quit_sig_action, NULL);
	sigaction(SIGTERM, &term_sig_action, NULL);
	

	/* Save the name of the program. */
	progname=argv[0];
	
	/* Parse the arguments. */

	context = poptGetContext(NULL, argc, (const char **) argv, irrOptions, 0);

	/* Handle the commands */

	for(command = 0, option = 0; option >= 0;){
		option = poptGetNextOpt(context);
		if(option > 0){
			if(!command)
				command = option;
			else
				fatal("%s multiple command arguments", commandLineParseErr);
		}
		if(option < -1)
			fatal("%s%s",commandLineParseErr, poptStrerror(option));		
		}

	switch(command) {
		
		/* valid commands which use the network */

		case	IRR_IRRIGATE:
		case	IRR_CHECK_SYNTAX:
		case	IRR_TEST_VALVE:
		case	IRR_PRESSURIZE:
		case	IRR_SUSPEND:
		
			/* Build an array of strings from leftover args */

			for(i = 0 ; (leftovers[i] = (char *) poptGetArg(context)) != NULL; i++);
			/* Execute a command */

			irrExecuteCommand(command, i, leftovers);
			
			break;

    		/* Was it a list valves request */
    
   		case IRR_LIST_VALVES:
      			irrListValves();   
      			break;
			
		/* Was it a version request? */

		case IRR_VERSION:
			irrShowVersion();
			break;
	
		/* Was it a help request? */

		case IRR_HELP:
			irrShowHelp();
			break;
		
		/* It was something weird.. */

		default:
			if(!command)
				fatal("%s No command argument specified", commandLineParseErr);
			else
				panic("Unhandled popt command %d", command);
	}
	
	/* Free up memory used by command line parser */

	poptFreeContext(context);

	/* Normal program Exit */

	exit(0); 
}
