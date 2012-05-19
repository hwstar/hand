/*
 * hantst.c.  Test program for the home automation network (HAN).
 *
 * Copyright (C) 1999,2002,2003 Stephen Rodgers
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
 * Stephen "Steve" Rodgers <hwstar@cox.net>
 *
 * $Id$
 */

#include "tnd.h"
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <popt.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "error.h"
#include "confscan.h"
#include "han.h"
#include "hanclient.h"
#include "pid.h"

/* Local Defines */

#define HANTST_HELP 'h' 
#define HANTST_VERSION 'v'
#define HANTST_SENDPKT 's'
#define HANTST_NETSCAN 'i' 
#define HANTST_GETSTATS 'g'
#define HANTST_PPOWER 'p'

#define CONF_STRING	1
#define CONF_FLAG	2


/* Global variables */

int debuglvl = 0;
char *progname;	// Pointer to argv[0] for error.c
char *version_string = VERSION; 

/* Local prototypes. */

static void getNetworkStats(int argc, char **argv);
static void scanNetwork(int argc, char **argv);
static void buildCommand(int argc, char **argv);
static void doPPower( int argc, char **argv);
static int  confSaveString(char *value, short handling, void *result);
static int  confFlag(char *value, short handling, void *result);
static int  confOrFlag(char *value, short handling, void *result);
static void printByteSequence(unsigned char *p, int len);
static void hantstShowHelp(void);
static void hantstShowVersion(void);
static void hantstExecuteCommand(int command, int argc, char **argv);
static void checkHandInfo();

/* Internal global variables */

static char *newConfFile = NULL;

static char defaultConfFile[MAX_CONFIG_STRING] = CONF_FILE_PATH;
static char confPidPath[MAX_CONFIG_STRING] = "";
static char confSocketPath[MAX_CONFIG_STRING] = "";
static char confService[MAX_CONFIG_STRING] = "";
static char confHost[MAX_CONFIG_STRING] = "";
static int  conf_nocheck = 0; 


static char *commandLineParseErr = "Command line parse error: ";

/* Command line option table */

static struct poptOption hantstOptions[] = {
	{"config-file", 'c', POPT_ARG_STRING, &newConfFile, 0},
	{"debug", 'd', POPT_ARG_INT, &debuglvl, 0},
	{"get-stats",'g',POPT_ARG_NONE, NULL, HANTST_GETSTATS},
	{"help", 'h', POPT_ARG_NONE, NULL, HANTST_HELP },
	{"interrogate",'i',POPT_ARG_NONE, NULL, HANTST_NETSCAN},
	{"nocheck",'n',POPT_ARG_NONE, &conf_nocheck, 0},
	{"ppower",'p',POPT_ARG_NONE, NULL, HANTST_PPOWER},
	{"send-packet",'s', POPT_ARG_NONE, NULL, HANTST_SENDPKT},
	{"version", 'v', POPT_ARG_NONE, NULL, HANTST_VERSION},

	{NULL, '\0', 0, NULL, 0}
};

 
/* Configuration option tables */

static Key_Entry	globalKeys[] = {
	{"pid_file", CONF_STRING, confPidPath, confSaveString},
	{"socket", CONF_STRING, confSocketPath, confSaveString},
	{"service", CONF_STRING, &confService, confSaveString},
	{"textservice", 0, NULL, NULL},
	{"host", CONF_STRING, confHost, confSaveString},
	{"nocheck",CONF_FLAG, &conf_nocheck, confOrFlag},
	{NULL, 0, NULL, NULL}
};

static Section_Entry	sectionHeader[] = {
	{"global", globalKeys},
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

int main(int argc, char *argv[])
{
 	int i, option, command;
	poptContext context;
	char *leftovers[64];


	/* Save the name of the program. */
	progname=argv[0];
	
	/* Parse the arguments. */

	context = poptGetContext(NULL, argc, (const char **) argv, hantstOptions, 0);

	/* Handle the commands */

	for(command = 0, option = 0; option >= 0;){
		option = poptGetNextOpt(context);
		if(option > 0){
			if(!command)
				command = option;
			else
				fatal("%smultiple command arguments", commandLineParseErr);
		}
		if(option < -1)
			fatal("%s%s",commandLineParseErr, poptStrerror(option));		
		}



	switch(command) {
		
		/* valid commands which use the network */
		case HANTST_SENDPKT:
		case HANTST_NETSCAN:
		case HANTST_GETSTATS:
		case HANTST_PPOWER:
		
			/* Build an array of strings from leftover args */

			for(i = 0 ; (leftovers[i] = (char *) poptGetArg(context)) != NULL; i++);
			/* Execute a command */

			hantstExecuteCommand(command, i, leftovers);

			break;
			
		/* Was it a version request? */

		case HANTST_VERSION:
			hantstShowVersion();
			break;
	
		/* Was it a help request? */

		case HANTST_HELP:
			hantstShowHelp();
			break;
		
		/* It was something weird.. */

		default:
			if(!command)
				fatal("%sNo command argument specified", commandLineParseErr);
			else
				panic("Unhandled popt command %d", command);
	}
	
	/* Free up memory used by command line parser */

	poptFreeContext(context);

	/* Normal program Exit */

	exit(0); 
}

/*
* Execute a network command
*/

void hantstExecuteCommand(int command, int argc,  char **argv){


	/* Parse the configuration file */

	confscan(newConfFile != NULL ? newConfFile : defaultConfFile, sectionHeader);

	hanclient_connect_setup(confPidPath, confSocketPath, confService, confHost);

	checkHandInfo();

	/* Select the appropriate command to execute */
	
	switch(command){
		case HANTST_SENDPKT:
			buildCommand(argc, argv);			
			break;
			
		case HANTST_NETSCAN:
			scanNetwork(argc, argv);
			break;	

		case HANTST_GETSTATS:
			getNetworkStats(argc, argv);
			break;

    		case HANTST_PPOWER:
      			doPPower(argc, argv);
      			break;

		default:
			panic("Unknown command %d",command);
	}
}	

/*
* Get Daemon info and see if we are compatible
*/

static void checkHandInfo(){

	Client_Command client_command;
	static char *not_comp = "Hand not compatible with hantst";


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
		warn("Version mismatch, hantst %s, hand %s", version_string, client_command.cmd.info.version);
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
* Return a summary of the network statistics
*/

static void getNetworkStats(int argc, char **argv){

	Client_Command client_command;

	/* Check for 0 or 1 arguments, bail out with an error if there */
	/* are more than one, or its not what we are expecting */
	
	if(argc > 2)
		fatal("%sOnly one argument allowed for -g", commandLineParseErr);
	if(argc == 1){
		if(!strcmp(argv[0],"clr"))
			client_command.request = HAN_CCMD_NETSTATSCLR;
		else
			fatal("%sInvalid argument for -g", commandLineParseErr);
	}
	else
		client_command.request = HAN_CCMD_NETSTATS;

	/* Send the command to the daemon */

	hanclient_send_command(&client_command);
	
	/* Print out the statistics */

	printf("\n");
	printf("Round Trips:\t\t%010u\n", client_command.cmd.stats.round_trips);
	printf("Spurious Packets:\t%010u\n", client_command.cmd.stats.spurious_packets);
	printf("RX Timeouts:\t\t%010u\n", client_command.cmd.stats.rx_timeouts);
	printf("TX Timeouts:\t\t%010u\n", client_command.cmd.stats.tx_timeouts);
	printf("CRC Errors:\t\t%010u\n\n", client_command.cmd.stats.crc_errs);

}
			


/*
* Scan the network for attached nodes
*/

static void scanNetwork(int argc, char **argv){

	unsigned i;
	Client_Command client_command;
	
	if(argc)
		fatal("%sNo arguments allowed for -i",commandLineParseErr);

	/* Set the command */

	client_command.request = HAN_CCMD_NETSCAN;

	/* Send it to the daemon */
	
	hanclient_send_command(&client_command);
	
	/* If nodes found, list them else print no nodes found message */

	if(client_command.cmd.scan.numnodesfound){
		printf("\n");
		for(i = 0 ; i < client_command.cmd.scan.numnodesfound ; i++)
			printf("%02X\t\t%04X\t\t%04X\n",
				(unsigned) client_command.cmd.scan.nodelist[i].addr,
				client_command.cmd.scan.nodelist[i].type,
				client_command.cmd.scan.nodelist[i].fwlevel);
		printf("\n");
	}
	else
		printf("No nodes found on the network\n");		 	 
}


/*
* Send a command string to PPOWER
*/

static void doPPower( int argc, char **argv)
{
  Client_Command client_command;


  // Set the command
  
  client_command.request = HAN_CCMD_PPOWER_COMMAND;

  if(argc != 1)
    fatal("Must supply exactly one argument for ppower command string");

  if(strlen(argv[0]) > 63)
    fatal("PPower command string too long");

  // Set the ppower command string
  
  strncpy(client_command.cmd.ppower_cmd.command_string, argv[0], sizeof(client_command.cmd.ppower_cmd.command_string) - 1);
  client_command.cmd.ppower_cmd.command_string[sizeof(client_command.cmd.ppower_cmd.command_string) -1 ] = 0;	
  // Send the command string to hand for referral to ppower

  hanclient_send_command(&client_command);
  
}



/*
* Build a command packet and send it to the remote node
*/

static void buildCommand(int argc, char **argv){

	int i;
	unsigned j;
	Client_Command client_command;

	/* Must have at least a node address, and a node command */

	if(argc < 2)
		fatal("%sSmallest packet is 2 bytes", commandLineParseErr);

	/* Build the client command block */

	client_command.request = HAN_CCMD_SENDPKT;

	client_command.cmd.pkt.numnodeparams = argc - 2;

	sscanf(argv[0],"%x",&j);
	client_command.cmd.pkt.nodeaddress = (unsigned char) j;	

	sscanf(argv[1],"%x",&j);
	client_command.cmd.pkt.nodecommand = (unsigned char) j;
	
	/* Stuff the parameter bytes in the block, if any */

	if( argc - 2 > MAX_NODE_PARAMS)
		fatal("Too many parameters specified, max is %d",MAX_NODE_PARAMS);

	for(i = 2 ; i < argc ; i++){
		sscanf(argv[i],"%x",&j);
		client_command.cmd.pkt.nodeparams[i - 2] = (unsigned char) j;
		}

	/* Send the command packet to the HAN daemon */

	hanclient_send_command(&client_command);
	
	if(client_command.cmd.pkt.numnodeparams){
		printf("Received: ");
		printByteSequence(client_command.cmd.pkt.nodestatus,
			client_command.cmd.pkt.numnodeparams);
	}
	else
		printf("Command acknowledged by node\n");

}


/* Print a byte sequence */

static void printByteSequence(unsigned char *p, int len)
{
	int i;

	for( i = 0 ; i < len ; i++)
		printf("%02X ",p[i]);
	printf("\n");
}


/* Save a configuration string option */

static int confSaveString(char *value, short handling, void *result){
	char *dest = (char *) result;

	strncpy(dest, value, MAX_CONFIG_STRING - 1 );
	dest[MAX_CONFIG_STRING - 1] = 0;
	debug(DEBUG_EXPECTED,"set string: %s",dest);
	return PASS;
}

/* Save a configuration flag */

static int confFlag(char *value, short handling, void *result){
	int *p = (int *) result;
	if(!strcmp(value, "yes"))
		*p = 1;
	else if(!strcmp(value, "no"))
		*p = 0;
	else
		*p = atoi(value);
	return PASS;
}

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





/* Show the help screen for hantst. */
static void hantstShowHelp(void) {
  printf("'hantst' is a utility that allows commands to be sent to\n");
  printf("the PIC nodes through the han daemon (hand)\n");
  printf("\n");
  printf("Usage: %s [OPTION]...\n", progname);
  printf("\n");
  printf("  -c, --config-file=path  set the path for the config file\n");
  printf("  -d, --debug=LEVEL       set the debug level, 0 is off, the\n");
  printf("                          compiled in default is %i and the max\n", DEBUGLVL);
  printf("                          level allowed is %i\n", DEBUG_MAX);
  printf("  -g  --get-stats [clr]   get network error statistics\n");
  printf("  -h, --help              give help on usage\n");
  printf("  -i, --interrogate       interrogate network for attached nodes\n");
  printf("  -p, --ppower            send a ppower command string\n");
  printf("  -s, --send-packet pkt   send a packet, and wait for a response\n"); 
  printf("  -v, --version           display program version\n");
  printf("\n");
  printf("Report bugs to <%s>\n",EMAIL);
  return;
}


/* Show the version info for hand. */
static void hantstShowVersion(void) {
	printf("hantst (%s) %s\n", PACKAGE, version_string);
}

