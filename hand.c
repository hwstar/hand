
/*
 * HAN daemon.  Handles the han hardware and client requests.
 *
 * Copyright(C) 1999,2002,2003,2011,2012 Stephen Rodgers
 * Portions Copyright (C) 1999  Steven Brown
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
 * Modified from ppowerd.c by Steve Rodgers on 8/7/99 originally written by:
 *
 * Steven Brown
 *
 * Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 * $Id$
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include "tnd.h"
#include "options.h"
#include "error.h"
#include "confscan.h"
#include "hanio.h"
#include "socket.h"
#include "pid.h"
#include "han.h"


/* Local Defines */

#define SHORT_OPTIONS "c:d:hl:nv"



/* Packet protocol constants */

#define STX		0x02			// Denotes start of frame
#define ETX		0x03			// Denotes end of frame
#define SUBST		0x04			// Substitute next character
#define HDC		0x01			// Header control bits for cmd
#define HDCINTRQ	0x02			// Header control bits for interrupt request from node
#define HDCINTRQ16	0x03			// Header control bits for interrupt request from node CRC16
#define HDC16		0x05			// Header control bits for cmd CRC16
#define	HDC_ACK		0xC1			// ACK response
#define	HDC_ACK16	0xC5			// ACK Response CRC16
#define HDC_NAK		0x81			// NAK response
#define	HDC_NAK16	0x85			// NAK response CRC16

#define MAX_PARAMS	MAX_NODE_PARAMS 	// Workaround: Compiler won't see MAX_NODE_PARAMS from config.h in handTransmitPacket!

#define	HC_GIST		0x03			// Get interrupt status command

/* Time out defines */

#define MAX_WRITE_BUSY_TIME		1000000 // 1 second
#define MAX_CMD_RESPONSE_TIME		250000	// 250 milliseconds
#define MAX_NODEID_RESPONSE_TIME	50000	// 50 milliseconds
#define MAX_INTR_RESPONSE_TIME		10000	// 10 milliseconds

/* Enums */

enum {RXP_WAIT_STX=0, RXP_GET_FRAME, RXP_STORE_SUBST};
enum {CONF_STRING=1, CONF_INTEGER, CONF_UNS, CONF_MODE, CONF_UID, CONF_GID};
enum {FD_UNUSED = 0, FD_RS485, FD_UNIX_CMD, FD_INET_CMD, FD_INET6_CMD, FD_INET_TEXT, FD_INET6_TEXT, FD_CONNECTED_TEXT};
enum {IPS_INIT=0, IPS_RCV, IPS_IGN};
enum {NC_INTRX=1, NC_CRC16=2};


/* Structs */


/* Local prototypes. */

static u8 calcCRC(void *buf, int len);
static u16 calcCRC16(void *buf, int len);
static int packetCheck16(void *buf, int size);
static void stuffPacketByte(u8 value, u8 **buffer, u16 *count);
static int handTransmitPacket(hanioStuff *hanio, Han_Packet *packet, int rx_timeout);
static int handSend(hanioStuff *hanio, Han_Packet *packet);
static int handScanNetwork(hanioStuff *hanio, Han_Netscan *netscan);
static int handDoPPower(char *command_string);
static int handDoRawPacket(struct han_raw *rp);
static void fd_setup(void);
static void handReconfig(void);
static void clientCommand(hanioStuff *hanio, Client_Command *client_command);
static int confSaveString(char *value, short handling, void *result);
static int confSaveMode(char *value, short handling, void *result);
static int confSaveUidGid(char *value, short handling, void *result);
static int confSaveUnsigned(char *value, short handling, void *result);
static void hand_handle_child(int sig);
static void hand_handle_brkpipe(int sig);
static void hand_handle_hup(int sig);
static void handle_exit_request(int sig);
static void hand_show_help(void);
static void hand_show_version(void);
static void hand_exit(void);
static int processClientCommand(int user_socket);
static void process_interrupt_packet(u8 *buffer, int len);
static void send_broadcast_enum(void);

/* Global things. */


int debuglvl = 0;

/* Name of the program. */

char *progname;


/* The number of active poll fd's we have open in the pollfd structure. */

/* Locals. */


/* Signal handler tables */

struct sigaction term_sig_action = {{handle_exit_request}, {{0}}, 0, NULL};

struct sigaction quit_sig_action = {{handle_exit_request}, {{0}}, 0, NULL};

struct sigaction int_sig_action =  {{handle_exit_request}, {{0}}, 0, NULL};

struct sigaction hup_sig_action =  {{hand_handle_hup}, {{0}}, 0, NULL};

struct sigaction brkpipe_sig_action ={{hand_handle_brkpipe}, {{0}}, 0, NULL};

struct sigaction child_sig_action ={{hand_handle_child}, {{0}}, 0, NULL};





static unsigned short reconfigRequest = 0;
static unsigned short exitRequest = 0;
static int no_background = 0;

static hanioStuff	*hanio;	
static char conf_file[MAX_CONFIG_STRING] = CONF_FILE_PATH;

/* Variables modifyable by config file keys */

static char conf_tty[MAX_CONFIG_STRING] = CONF_TTY;
static char conf_pid_path[MAX_CONFIG_STRING] = CONF_PID_PATH;
static char conf_log_path[MAX_CONFIG_STRING] = CONF_LOG_PATH;
static char conf_ppower_path[MAX_CONFIG_STRING] = "";
static char conf_daemon_socket_path[MAX_CONFIG_STRING] = "";
static char conf_service[MAX_CONFIG_STRING] = "";
static char conf_textservice[MAX_CONFIG_STRING] = "";
static char conf_bindaddr[MAX_CONFIG_STRING];
static int conf_daemon_socket_uid = 0;
static int conf_daemon_socket_gid = 0;
static int conf_daemon_socket_mode = 0660;
static unsigned conf_max_retries = CONF_MAX_RETRIES;	// Maximum number of retries
static unsigned max_node_addr = MAX_NODE_ADDR;		// Maximum node address
static u8 node_attributes[256];				// Node attribute array





/* Sockets vars used by the daemon. */

static short num_poll_fds = 0;		// Number of active poll fd's
struct pollfd pollfd[POLL_MAX_COUNT];
int fdtype[POLL_MAX_COUNT];
char intreportingena[POLL_MAX_COUNT];

/* Text Commands */

#define NUM_TEXT_CMDS 3
char *text_commands[NUM_TEXT_CMDS] = {"CA","IE","ID"};
enum {TC_CA, TC_IE, TC_ID};
	

/* Commandline options. */

static struct option long_options[] = {

  {"config-file", 1, 0, 'c'},
  {"debug", 1, 0, 'd'},
  {"help", 0, 0, 'h'},
  {"log-file", 1, 0, 'l'},
  {"no-background", 0, 0, 'n'},
  {"version", 0, 0, 'v'},
  {0, 0, 0, 0}
};
 
/* Error Statistics */

Err_Stats error_stats = {0,0,0,0,0};

/* Configuration option tables */

Key_Entry	global_keys[] = {
	{"pid_file", CONF_STRING, conf_pid_path,confSaveString},
	{"socket", CONF_STRING, conf_daemon_socket_path, confSaveString},
	{"service", CONF_STRING, conf_service, confSaveString},
	{"textservice",CONF_STRING, conf_textservice, confSaveString},
	{"host", 0, NULL, NULL},
	{"nocheck", 0, NULL, NULL},
	{NULL, 0, NULL, NULL}
};

Key_Entry	hand_keys[] = {
	{"tty", CONF_STRING, &conf_tty, confSaveString},
	{"unix_socket_mode", CONF_MODE, &conf_daemon_socket_mode, confSaveMode},
	{"unix_socket_uid", CONF_UID, &conf_daemon_socket_uid, confSaveUidGid},
	{"unix_socket_gid", CONF_GID, &conf_daemon_socket_gid, confSaveUidGid},
	{"bindaddr",  CONF_STRING, conf_bindaddr, confSaveString},
	{"retries", CONF_UNS, &conf_max_retries, confSaveUnsigned},
	{"ppower_path",CONF_STRING, conf_ppower_path, confSaveString},
	{"max_node_addr",CONF_UNS, &max_node_addr, confSaveUnsigned},
	{"log_path", CONF_STRING, conf_log_path, confSaveString},
	{NULL, 0, NULL, NULL}
};

Section_Entry	conf_section[] = {
	{"global", global_keys},
	{"hand", hand_keys},
	{NULL, NULL}
};



/* Reap zombie processes */

static void hand_handle_child(int sig){

	while(waitpid(-1, NULL, WNOHANG) > 0);

}



/* Handle broken pipe */

static void hand_handle_brkpipe(int sig){
}


/* Make a note of the hang up request */

static void hand_handle_hup(int sig){
	reconfigRequest = 1;
}


/* Handle exit signals */

static void handle_exit_request(int sig){
	exitRequest = 1;
}


/*
* Save a configuration string option
*/

static int confSaveString(char *value, short handling, void *result){
	char *dest = (char *) result;
	strncpy(dest,value,MAX_CONFIG_STRING - 1);
	dest[MAX_CONFIG_STRING - 1] = 0;
	debug(DEBUG_STATUS,"set string: %s",dest);
	return PASS;
}


/* Save a file or socket mode */

static int confSaveMode(char *value, short handling, void *result){
	int *dest = (int *) result;

	sscanf(value,"%3o",dest);
	debug(DEBUG_STATUS,"set mode: %03o",*dest);
	return PASS;
}

/* Save a user or group ID */

static int confSaveUidGid(char *value, short handling, void *result){
	int *dest = (int *) result;

	*dest = atoi(value);
	debug(DEBUG_STATUS,"set uid/gid: %d",*dest);
	return PASS;
}

/* Save a generic unsigned value */

static int confSaveUnsigned(char *value, short handling, void *result){
	unsigned *dest = (unsigned *) result;

	sscanf(value,"%u",dest);
	debug(DEBUG_STATUS,"set unsigned: %u",*dest);
	return PASS;
}

/*
* Figure out the offset to the address field and return a pointer to it.
*/

static void *munge_ip_address(void *p)
{
	struct sockaddr *q = p;

	if (q->sa_family == AF_INET) 
        	return &((( struct sockaddr_in* ) p )->sin_addr);

	return &(((struct sockaddr_in6* ) p )->sin6_addr);

}


/*
* Add an fd to the poll list
*/

static int add_poll_fd(int sock, int type)
{
	if(num_poll_fds < POLL_MAX_COUNT){
		pollfd[num_poll_fds].fd = sock;
		fdtype[num_poll_fds] = type;
		pollfd[num_poll_fds++].events = POLLIN;
		return PASS;
	}
	return FAIL;
	
}



/*
* Callback from sock_create_listen() for command sockets
*/

static int add_ip_socket_cmd(int sock, void *addr, int family, int socktype)
{
	void *p;
	char addrstr[INET6_ADDRSTRLEN];

	if(debuglvl > 1){
		p = munge_ip_address(addr);
		inet_ntop(family, p, addrstr, sizeof(addrstr));
		debug(DEBUG_EXPECTED, "Command Socket listen ip address: %s", addrstr);
	}

	return add_poll_fd(sock, (family == AF_INET) ? FD_INET_CMD : FD_INET6_CMD);

}

/*
* Callback from sock_create_listen() for text sockets
*/

static int add_ip_socket_text(int sock, void *addr, int family, int socktype)
{
	void *p;
	char addrstr[INET6_ADDRSTRLEN];

	if(debuglvl > 1){
		p = munge_ip_address(addr);
		inet_ntop(family, p, addrstr, sizeof(addrstr));
		debug(DEBUG_EXPECTED, "Monitor Socket listen ip address: %s", addrstr);
	}

	return add_poll_fd(sock, (family == AF_INET) ? FD_INET_TEXT : FD_INET6_TEXT);

}

/*
* Add text socket to the polling list if there is room
*/

static int add_text_socket(int text_socket)
{
	if(!add_poll_fd(text_socket, FD_CONNECTED_TEXT)){
		debug(DEBUG_EXPECTED, "Added text socket to polling list");
	}
	else{
		debug(DEBUG_UNEXPECTED, "Text socket list is full");
		return FAIL;
	}
	return PASS;
}

/*
* Print a message to all active text sockets
*/

static void ts_printf(char *msg, ...)
{
	va_list ap;
	int i;
	
	va_start(ap, msg);

	/* print the message */

	for(i = 0 ; i < num_poll_fds; i++){
		if((fdtype[i] != FD_CONNECTED_TEXT) || (!intreportingena[i]))
			continue;			
		vdprintf(pollfd[i].fd, msg, ap);
	}
	va_end(ap);
}




/* Initialization of file handles  */


static void fd_setup(void){

	int s;

	num_poll_fds = 0;


	/* Open the han RS-485 interface. */
	debug(DEBUG_STATUS, "Opening tty %s", conf_tty);

	if(!(hanio = hanio_open(conf_tty)))
		fatal("Could not open tty %s", conf_tty);

	add_poll_fd(hanio->fd, FD_RS485);


	if(conf_daemon_socket_path[0]){

		/* Free the stale daemon sockets if we have them. */
		(void) unlink(conf_daemon_socket_path);

		/* Create the unix domain daemon socket used for commands */

		debug(DEBUG_STATUS, "Creating unix domain socket '%s'", conf_daemon_socket_path);
		if((s = socket_create(conf_daemon_socket_path, conf_daemon_socket_mode, conf_daemon_socket_uid, conf_daemon_socket_gid) == -1))
			fatal("Could not create unix socket");
		add_poll_fd(s, FD_UNIX_CMD);
	}

	/* Create the ipv4/ipv6 socket if a command service port is defined */

	if(conf_service[0]){
		debug(DEBUG_STATUS, "Adding sockets on port/service %s", conf_service);
	
		if( socket_create_listen(conf_bindaddr[0] ? conf_bindaddr : NULL, conf_service, AF_UNSPEC, SOCK_STREAM, add_ip_socket_cmd)){
			fatal("command socket_create_listen() failed");
		}
	}


	/* If no socket connection type defined, we can't do anything. Might as well quit now */

	if(num_poll_fds < 2)
		fatal("No command socket: (ipv4, ipv6, or unix domain) defined in config file");

	/* Create the ipv4/ipv6 socket if a text service port is defined */

	if(conf_textservice[0]){
		debug(DEBUG_STATUS, "Adding text sockets on port/service %s", conf_textservice);
	
		if( socket_create_listen(conf_bindaddr[0] ? conf_bindaddr : NULL, conf_textservice, AF_UNSPEC, SOCK_STREAM, add_ip_socket_text)){
			fatal("text socket_create_listen() failed");
		}
	}


}

/*
* Close all polled sockets
*/

void close_all_polled_sockets(void)
{
	int i;
	for(i = 1; i < num_poll_fds; i++)
		close(pollfd[i].fd);
}


/* Cleanup and exit. */

void hand_exit(void)
{

	debug(DEBUG_STATUS, "Cleaning up for exit.");


	/* Close the existing TTY */

	hanio_close(hanio);

	/* Close polled sockets. */

	close_all_polled_sockets();


	/* Unlink the two filesystem-bound sockets if they exist. */
	(void) unlink(conf_daemon_socket_path);

	/* Unlink the pid file if we can. */
	(void) unlink(conf_pid_path);

	/* And exit.. */
	exit(0);
}

/* Re-read the config file, and re-initialize the daemon */

static void handReconfig(void){
	int i;

	reconfigRequest = 0;

  	debug(DEBUG_STATUS,"HUP signal received");

  	/* Close, re-open, and truncate the log file, if in debug mode */

	if((debuglvl) && (no_background == 0))
		error_logpath(conf_log_path);

	/* Close the existing TTY */

	hanio_close(hanio);

	/* Close polled sockets. */

	close_all_polled_sockets();

	/* Turn off interrupt monitoring */

	for(i = 0; i < POLL_MAX_COUNT; i++)
		intreportingena[i] = 0;

	/* Unlink the pid file */

	(void) unlink(conf_pid_path);

	/* Scan the config file */

	debug(DEBUG_STATUS,"Re-reading configuration file");

	confscan(conf_file, conf_section);

	/* Create the PID file */

	if(pid_write(conf_pid_path, getpid()) != 0) {
		fatal("Could not re-write pid file '%s'", conf_pid_path);
	}

	fd_setup();

}


/*
* Calculate an 8 bit CRC of polynomial X^8+X^5+X^4+1
*/

static u8 calcCRC(void *buf, int len){
	u8 theBits, fb, crcReg = 0;
	u8 *p = (u8 *) buf;
	u16 i, j;
	
	for(i = 0 ; i < len ; i++)
	{
		theBits = *p++;
		for(j = 0 ; j < 8 ; j++)
		{
			fb = (theBits ^ crcReg) & 1;
			crcReg >>= 1;
			if(fb)
				crcReg ^= 0x8C;
			theBits >>= 1;
		}
	}
	return crcReg;
}


/* Calculate CRC over buffer using polynomial: X^16 + X^12 + X^5 + 1 */

static u16 calcCRC16(void *buf, int len)
{
	u8 i;
	u16 crc = 0;
	u8 *b = (u8 *) buf;

	while(len--){
		crc ^= (((u16) *b++) << 8);
		for ( i = 0 ; i < 8 ; ++i ){
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
          	}
	}
	return crc;
}

/* Check a CRC16 on a packet buffer */

static int packetCheck16(void *buf, int size)
{
	u16	rcrc16, crc16 =  calcCRC16(buf, size - sizeof(u16));

	rcrc16 = *((u16 *)(((u8 *) buf) + (size - sizeof(u16))));

	debug(DEBUG_ACTION, "Rx CRC16: 0x%04X, Calc CRC16 0x%04X", rcrc16, crc16);

	if(crc16 == rcrc16)
		return PASS;
	else
		return FAIL;
		
}



/*
* Stuff a packet byte in the buffer, performing any necessary substitutions
*/

static void stuffPacketByte(u8 value, u8 **buffer, u16 *count){

	if(value <= SUBST){
		**buffer = SUBST;
		(*buffer)++;
		(*count)++;
	}
	
	**buffer = value;
	(*buffer)++;
	(*count)++;
}


/*
* Transmit a packet to the network and wait for a response
*/

static int handTransmitPacket(hanioStuff *hanio, Han_Packet *packet, int rx_timeout) {
	int i,retval;
	unsigned char *p;
	unsigned short done, rxState, rxPacketLen, txPacketLen = 1;
	unsigned char crcBuffer[4 + MAX_PARAMS];
	unsigned char txBuffer[MAX_PARAMS*2 + 12];
	unsigned char rxBuffer[MAX_PARAMS + 6];
	u8 crc16 = node_attributes[packet->nodeaddress] & NC_CRC16;
	u8 ack, nak;
	
	debug(DEBUG_ACTION, "Addressing node 0x%02x, command 0x%02x, Numparms 0x%02x, crc16 = %d", packet->nodeaddress, packet->nodecommand,packet->numnodeparams, crc16);

	/*
	* Build the TX packet
	*/
	
	txBuffer[0] = STX;
	p = txBuffer + 1;
	
	/* Add the header control bits */
	
	stuffPacketByte((crcBuffer[0] = (crc16) ? HDC16 : HDC), &p, &txPacketLen);

	/* Add the node address */
	
	stuffPacketByte((crcBuffer[1] = packet->nodeaddress), &p, &txPacketLen);

	/* Add the command */

	stuffPacketByte((crcBuffer[2] = packet->nodecommand), &p, &txPacketLen);

	/* Add the parameter bytes */

	for(i = 0 ; i < packet->numnodeparams ; i++)
		stuffPacketByte((crcBuffer[3 + i ] = packet->nodeparams[i]), &p, &txPacketLen);

	/* Calculate a CRC and place it in the packet */
	if(crc16){
		u8 crclo, crchi;
		u16 crc = calcCRC16(crcBuffer, 3 + packet->numnodeparams);
		debug(DEBUG_ACTION, "TX CRC16: %04X", crc);

		crclo = (u8) crc;
		crchi = (u8) (crc >> 8);
		stuffPacketByte((crcBuffer[3 + packet->numnodeparams] = crclo), &p, &txPacketLen);
		stuffPacketByte((crcBuffer[4 + packet->numnodeparams] = crchi), &p, &txPacketLen);
	}
	else{		 
		stuffPacketByte((crcBuffer[3 + packet->numnodeparams] = calcCRC(crcBuffer, 3 + packet->numnodeparams)),
					&p, &txPacketLen);
	}
	
	/* Add the end framing character */
	
	*p = ETX;
	txPacketLen++;
	
				
	debug_hexdump(DEBUG_EXPECTED, crcBuffer, packet->numnodeparams + ((crc16)? 5 : 4), "TX Packet: ");
	debug_hexdump(DEBUG_ACTION, txBuffer, txPacketLen, "Stuffed TX Packet: ");

	// Send the TX packet

	retval = hanio_write(hanio, txBuffer, txPacketLen, MAX_WRITE_BUSY_TIME);
	if(retval != txPacketLen){
		debug(DEBUG_UNEXPECTED, "Packet transmit timeout");
		return HAN_CSTS_TX_TIMEOUT;
	}

	debug(DEBUG_ACTION,"TX packet sent.");

	// If broadcast packet, there will be no response

	if(packet->nodeaddress == 0xFF){
		usleep(1000);
		return HAN_CSTS_OK;
	}
	
	// Remove stuffed bytes and copy to rxBuffer looking for ETX
 	
	for(p = rxBuffer, rxState = RXP_WAIT_STX, done = 0, rxPacketLen = 0 ; (done != 1) && (rxPacketLen < MAX_NODE_PARAMS + 6) ;){
		retval = hanio_read(hanio, p, 1, rx_timeout);
		if(retval == 0){
			if(rxPacketLen == 0){
				debug(DEBUG_UNEXPECTED, "Packet receive timeout, no bytes received");
				return HAN_CSTS_RX_TIMEOUT;
			}
			else{
				debug(DEBUG_UNEXPECTED, "Timeout after receiving partial packet, returning: FRAMING ERROR");
				return HAN_CSTS_FRAMING_ERROR;
			}
		}
		 

		switch(rxState){
			
			case RXP_WAIT_STX:
				if(*p != STX){
					debug(DEBUG_UNEXPECTED, "Packet framing error, no STX");
					return HAN_CSTS_FRAMING_ERROR;
				}
				rxPacketLen = 0;
				rxState = RXP_GET_FRAME;
				break;
				
			case RXP_GET_FRAME:
				switch(*p){
					case SUBST:
						rxState = RXP_STORE_SUBST;
						break;
						
					case STX:
						p = rxBuffer;
						rxPacketLen = 0;
						break;
						
					case ETX:
						done = 1;
						break;
					
					default:
						if(*p > SUBST){
							p++;
							rxPacketLen++;
						}
						break;
				}						
				break;
				
			case RXP_STORE_SUBST:
				p++;
				rxPacketLen++;
				rxState = RXP_GET_FRAME;			
				break;
			
			default:
				panic("Unknown state reached in packet state machine %d\n", rxState);
				break;
		}			
	}
	if(!done){
		debug(DEBUG_UNEXPECTED,"Packet framing error, no ETX");
		return HAN_CSTS_FRAMING_ERROR;
	}

	
	// Check the packet

	if(crc16){
		ack = HDC_ACK16;
		nak = HDC_NAK16;
		if(packetCheck16(rxBuffer, rxPacketLen)){
			debug(DEBUG_EXPECTED, "CRC16 error on status packet");
			return HAN_CSTS_CRC_ERROR; 
		}
	}
	else{
		ack = HDC_ACK;
		nak = HDC_NAK;
		if(calcCRC(rxBuffer , rxPacketLen)){
			debug(DEBUG_UNEXPECTED, "CRC8 error on status packet");
			return HAN_CSTS_CRC_ERROR;
		}
	}

	debug(DEBUG_ACTION, "CRC OK on response.");


	if((rxBuffer[0] != ack)&&(rxBuffer[0] != nak)){
		debug_hexdump(DEBUG_UNEXPECTED, rxBuffer, rxPacketLen,  "Unrecognized HDC: ");
		return HAN_CSTS_FORMAT_ERROR;	
	}
	
	// Packet OK, transfer the status bytes
		
	if(rxBuffer[0] == nak){
		debug(DEBUG_UNEXPECTED, "Node returned NAK");
		return HAN_CSTS_NAK_ERROR;
	}

	for(i = 0 ; i < (rxPacketLen - 4) ; i++)
		packet->nodestatus[i] = rxBuffer[ 3 + i];
	
	usleep(1000); // Wait 1ms for tx driver disable on node	
	return HAN_CSTS_OK;
}

/*
* Send a packet and wait for a response, wait the normal response time before
* timing out. Attempt to retry if an RX timeout occurs. Log error counts
* and successful packets
*/

static int handSend(hanioStuff *hanio, Han_Packet *packet) {
	unsigned txCount;
	int retVal = HAN_CSTS_OK;
	int rxTimeout, done;
	
	for(txCount = 0, done = 0, rxTimeout = 0; (done == 0) && (txCount < (conf_max_retries + 1)); txCount++){
		switch((retVal = handTransmitPacket(hanio, packet, MAX_CMD_RESPONSE_TIME))){
			case HAN_CSTS_OK:
				done = 1;
				error_stats.round_trips++;
				break;
				
			case HAN_CSTS_RX_TIMEOUT:
				error_stats.rx_timeouts++;
				break;
			
			case HAN_CSTS_TX_TIMEOUT:
				error_stats.tx_timeouts++;
				done = 1;
				break;
			
			case HAN_CSTS_CRC_ERROR:
				error_stats.crc_errs++;
				break;
			
			case HAN_CSTS_FORMAT_ERROR:
				break;
				
			case HAN_CSTS_NAK_ERROR:
				break;
			
			case HAN_CSTS_FRAMING_ERROR:
				break;

			default:
				panic("Unknown return code from hand_transmit_packet: %d",retVal);
				break;
		}			
	}				
	return retVal;
}											
		

/*
* Scan the network for attached nodes, and report all nodes found
*/

static int handScanNetwork(hanioStuff *hanio, Han_Netscan *netscan){
	unsigned i;
	unsigned j;
	int res = HAN_CSTS_OK;
	static Han_Packet packet;
	
	packet.numnodeparams = 4;
	packet.nodecommand = HAN_CMD_NODEID;

	netscan->numnodesfound = 0;
	
	for(i = 0, j = 0 ; i <= max_node_addr ; i++){
		packet.nodeaddress = (unsigned char) i;

		if((res = handTransmitPacket(hanio, &packet, MAX_NODEID_RESPONSE_TIME) == HAN_CSTS_OK)){
			netscan->nodelist[j].addr = (unsigned char) i;
			netscan->nodelist[j].type = (((unsigned)packet.nodestatus[1]) << 8) + packet.nodestatus[0]; 
			netscan->nodelist[j++].fwlevel = (((unsigned)packet.nodestatus[3]) << 8) + packet.nodestatus[2];
			netscan->numnodesfound++;
		}
		if(res == HAN_CSTS_TX_TIMEOUT) // This one is a real error.
			break;
	}
	if(res != HAN_CSTS_TX_TIMEOUT)
		res = HAN_CSTS_OK;

	return res;
}


// Fork a process, execute ppower client, and send the command string to the client
// for processing


static int handDoPPower(char *command_string)
{
  int pid,status;

  if(!strlen(conf_ppower_path))
    return HAN_CSTS_PPOWER_CONFIG_ERROR; // Path to ppower not defined in han.conf

  pid = fork();
  if(pid == 0) // Child
    exit(execl(conf_ppower_path,"ppower", command_string, NULL));
  else if (pid > 0)  // Parent
    wait(&status);   
  else // Error
    return HAN_CSTS_PPOWER_FORK_ERROR;

  if(WIFEXITED(status))  // Return value should be 0
    return HAN_CSTS_OK;
  else
    return HAN_CSTS_PPOWER_ERROR;
}


/*
* Send a raw packet over the RS-485 connection
*/

static int handDoRawPacket(struct han_raw *rp)
{
	int res;

	if(!rp)
		return HAN_CSTS_INVPARM;

	res = hanio_write(hanio, rp->txbuffer, rp->txlen, rp->txtimeout);
	if(res != rp->txlen)
		return HAN_CSTS_TX_TIMEOUT;

	rp->rxexpectlen = (unsigned char ) hanio_read(hanio, rp->rxbuffer, rp->rxexpectlen, rp->rxtimeout);
	debug(DEBUG_ACTION,"returned length: %d", rp->rxexpectlen);
	return HAN_CSTS_OK;
}


/*
* Figure out what command the client sent to us and try to do something
* with it.
*/

static void clientCommand(hanioStuff *hanio, Client_Command *client_command){
			
	switch(client_command->request){

		case HAN_CCMD_DAEMON_INFO:
			memset(&client_command->cmd.info, 0, sizeof(struct hand_info));
			strncpy(client_command->cmd.info.version, VERSION, 31);
			client_command->cmd.info.handinfosize = sizeof(struct hand_info);
			client_command->cmd.info.cmdpktsize = sizeof(struct han_packet);
			client_command->cmd.info.netscanpktsize = sizeof(struct han_netscan);
			client_command->cmd.info.errstatssize = sizeof(struct err_stats);
			client_command->cmd.info.ppowersize = sizeof(struct ppower_client_command);
			client_command->cmd.info.rawsize = sizeof(struct han_raw);
			client_command->commstatus = HAN_CSTS_OK;
			break;

	
		case HAN_CCMD_SENDPKT:
			/* It is transmit command request */
			/* Send the command to the network. */

			client_command->commstatus = handSend(hanio, &client_command->cmd.pkt);
			break;
		
		case HAN_CCMD_NETSCAN:
			/* It is a scan request */	
			
			client_command->commstatus = handScanNetwork(hanio, &client_command->cmd.scan);
			break;

	
		case HAN_CCMD_NETSTATSCLR:
		case HAN_CCMD_NETSTATS:

			/* It is a request for the net statistics */

			client_command->commstatus = HAN_CSTS_OK;
			memcpy(&client_command->cmd.stats, &error_stats, sizeof(error_stats));
			if(client_command->request == HAN_CCMD_NETSTATSCLR){
				error_stats.round_trips = 0;
				error_stats.rx_timeouts = 0;
				error_stats.tx_timeouts = 0;
				error_stats.crc_errs = 0;
				error_stats.spurious_packets = 0;
			}
			break;

     		 /* It's a request to send a command string to ppower */
      
		case HAN_CCMD_PPOWER_COMMAND:
        		client_command->commstatus = handDoPPower(client_command->cmd.ppower_cmd.command_string);
        		break;

		/* It's a raw packet */
		case HAN_CCMD_RAW_PACKET:
			client_command->commstatus = handDoRawPacket(&client_command->cmd.raw);
			break;

		default:
			/* It was a wacky client command.. */

			client_command->commstatus = HAN_CSTS_CMD_UNKNOWN;
			debug(DEBUG_UNEXPECTED, "Weird client command received, '%d'.", client_command->request);
			break;
	}

}

/* Show the help screen for hand. */
static void hand_show_help(void) {
	printf("'hand' is the daemon that controls the RS-485 interface to the PIC nodes\n");
	printf("\n");
	printf("Usage: %s [OPTION]...\n", progname);
	printf("\n");
	printf("  -c, --config-file PATH  set the path for the config file\n");
	printf("  -d, --debug LEVEL       set the debug level, 0 is off, the\n");
	printf("                          compiled in default is %i and the max\n", DEBUGLVL);
	printf("                          level allowed is %i\n", DEBUG_MAX);
	printf("  -h, --help              give help on usage\n");
	printf("  -l, --log-file PATH     set the path for the log file\n");
	printf("  -n, --no-background     do not fork into the background\n");
	printf("  -v, --version           display program version\n");
	printf("\n");
 	printf("Report bugs to <%s>\n", EMAIL);
	return;
}


/* Show the version info for hand. */
static void hand_show_version(void) {
	printf("hand (%s) %s\n", PACKAGE, VERSION);
}



/*
* Read the client command, execute it, and return the result
* returns 1 if timeout from a read, 0 of ok.
*/

static int processClientCommand(int user_socket){

        Client_Command client_command;

	/* The socket needs to be non-blocking. */
	if(fcntl(user_socket, F_SETFL, O_NONBLOCK) == -1) {
		fatal_with_reason(errno, "Could not set user socket to non-blocking");
	}
			
	/* Read the request from the client. */

	if(!socket_read(user_socket, &client_command, sizeof(Client_Command), USER_READ_TIMEOUT)) {
		debug(DEBUG_UNEXPECTED, "Gave up waiting for user command socket");
				
		/* We timed out, close the socket and continue. */
		if(close(user_socket) != 0) {
			debug(DEBUG_UNEXPECTED, "Problem closing timed-out user command socket: %s", strerror(errno));
		}
		return FAIL;
	}
			
	/* Process the command */

	clientCommand(hanio, &client_command);
	
	/* Send a response to the controlling process */
	
	if(!socket_write(user_socket, &client_command, sizeof(Client_Command), USER_WRITE_TIMEOUT)){
		debug(DEBUG_UNEXPECTED, "Gave up waiting to write HAN command response");
		/* We timed out, close the socket and continue. */
		if(close(user_socket) != 0)
			debug(DEBUG_UNEXPECTED, "Problem closing socket after a time out writing the response info");
	}

		
	/* Close the user's socket. */
	if(close(user_socket) != 0) {
		debug(DEBUG_UNEXPECTED, "Problem closing completed user command socket: %s", strerror(errno));
	}
	debug(DEBUG_STATUS, "User socket closed");
	return PASS;
}	

/*
* Convert two hex digits into an unsigned char
*/

static unsigned char h2touc(char *s, char *err)
{
	unsigned char res;
	int i;

	for(i = 0, res = 0; i < 2; i++){
		res <<= 4;
		if((s[i] >= 0) && (s[i] <= '9'))
			res += (s[i] - '0');
		else if((s[i] >= 'A') || (s[i] <= 'F'))
			res += ((s[i] - 'A') + 0x0a);
		else
			*err |= 1;
	}
	return res;
}




/*
* Parse a text command into a Client Command Structure
*/

static int parse_text_command(char *rs, char *line, int len)
{
	Han_Packet packet;
	int i,res;
	char err = 0;

	memset(&packet, 0, sizeof(Han_Packet));

	if(len & 1){
		debug(DEBUG_UNEXPECTED, "Command string must have an even number of bytes");
		return FAIL;
	}
	if((len < 4) || (len > (4 + (MAX_PARAMS * 2)))){
		debug(DEBUG_UNEXPECTED, "Command string has too few or too many digits");
		return FAIL;
	}
	packet.nodeaddress = h2touc(line, &err);
	packet.nodecommand = h2touc(line+2, &err);
	for(i = 0; i < (len - 4); i++){
		packet.nodeparams[i] = h2touc(line + 4 + (i << 1), &err);
	}
	packet.numnodeparams = ((len - 4) >> 1);
	if(!err){
		debug(DEBUG_EXPECTED, "Address: 0x%02X, Command: 0x%02X Number of node parameters: %i", packet.nodeaddress, packet.nodecommand, packet.numnodeparams);

		if((res =handSend(hanio, &packet)) != HAN_CSTS_OK){
			sprintf(rs, "CE%02X", -res);
			return 0;
		}

		sprintf(rs,"RS%02X%02X",packet.nodeaddress, packet.nodecommand);

		for(i = 0; i < packet.numnodeparams; i++)
			sprintf(rs + ((i + 3) << 1),"%02X", packet.nodestatus[i]);
		return PASS;
	}
	else
		return FAIL;



}
 

/*
* Process a command received from a text socket
*/


static void text_command(int sockindex, char *line, int len)
{
	int i,err = 0, s;
	char rs[64];

	rs[0] = 0;
	
	s = pollfd[sockindex].fd;

	if(len >= 2){
		for(i = 0; i < NUM_TEXT_CMDS; i++){
			if(!strncmp(line, text_commands[i], 2))
				break;
		}
		if(i == NUM_TEXT_CMDS)
			err = 1;
	}
	else
		err = 1;
	
	if(!err){
		switch(i){
			case TC_CA:
				err = parse_text_command(rs, line + 2, len - 2);
				break;
			case TC_IE:
				intreportingena[sockindex] = 1;
				break;
			case TC_ID:
				intreportingena[sockindex] = 0;
				break;
			default:
				err = 1;
				break;
		}
	}
	if(err){
		dprintf(s, "ER\n");
	}
	else if(rs[0])
		dprintf(s, "%s\n", rs);
	else
		dprintf(s, "OK\n");
	return;
}


/*
* Return the poll index for the supplied key
*/

static int fd_index(int key)
{
	int i;
	for(i = 0; i < POLL_MAX_COUNT; i++){
		if(key == fdtype[i]){
			return i;
		}
	}
	return -1;
}

/*
* Send interrupt packets to text sockets
*/

static void process_interrupt_packet(u8 *buffer, int len)
{
	int res;
	Han_Packet packet;

	node_attributes[buffer[1]] |= NC_INTRX;

	if(len == 4)
		node_attributes[buffer[1]] |= NC_CRC16;
	else
		node_attributes[buffer[1]] &= ~NC_CRC16;

	debug(DEBUG_EXPECTED, "Got interrupt packet from address %u", (unsigned) buffer[1]);
	debug_hexdump(DEBUG_EXPECTED, buffer, len,"Packet Bytes: ");

	packet.nodeaddress = buffer[1];
	packet.nodecommand  = HC_GIST;
	packet.nodeparams[0] = packet.nodeparams[1] = packet.nodeparams[2] = 0;
	packet.numnodeparams = 3;
	
	// Retrive IRQ Reason from interruptor
	res = handSend(hanio, &packet);
	if(res == HAN_CSTS_OK)
		debug(DEBUG_EXPECTED, "Interrupt reason code: 0x%02X", packet.nodestatus[0]);
	else
		debug(DEBUG_UNEXPECTED, "Communications status error during interrupt acknowledge, code = %d", res);

	// Send message to listening sockets

	ts_printf("EI%02X%02X%02X%02X\n", packet.nodeaddress, packet.nodestatus[0],packet.nodestatus[1], packet.nodestatus[2]);

	return;
}

/*
* Send a broadcast enum packet over the network
*/

void send_broadcast_enum(void)
{
	Han_Packet packet;

	packet.nodeaddress = 0xFF;
	packet.nodecommand  = BCP_ENUM;
	packet.numnodeparams = 0;

	handTransmitPacket(hanio, &packet, 0);

}



/* Main... */
int main(int argc, char *argv[]) {
	int retval;
	int user_socket;
	int longindex;
	int optchar;
	int i,si;
	
	

	/* Save the name of the program. */
	progname=argv[0];
	
	/* Parse the arguments. */
	while((optchar=getopt_long(argc, argv, SHORT_OPTIONS, long_options, &longindex)) != EOF) {
		
		/* Handle each argument. */
		switch(optchar) {
			
			/* Was it a long option? */
			case 0:
				
				/* Hrmm, something we don't know about? */
				fatal("Unhandled long getopt option '%s'", long_options[longindex].name);
			
			/* If it was an error, exit right here. */
			case '?':
				exit(1);

			case 'c':
				/* Override config file path */

				strncpy(conf_file, optarg, MAX_CONFIG_STRING - 1);
				conf_file[MAX_CONFIG_STRING - 1] = 0;
				debug(DEBUG_EXPECTED,"New config file path: %s",
				conf_file);

				break;
        
			/* Was it a debug level set? */
			case 'd':

				/* Save the value. */
				debuglvl=strtol(optarg, NULL, 10);
				if((debuglvl == LONG_MIN || debuglvl == LONG_MAX) && errno) {
					fatal("Invalid debug level");
				}
				if(debuglvl < 0 || debuglvl > DEBUG_MAX) {
					fatal("Invalid debug level");
				}

				break;
			
			/* Was it a help request? */
			case 'h':
				hand_show_help();
				exit(0);


			case 'l':
				/* Override log file path */

				strncpy(conf_log_path, optarg, MAX_CONFIG_STRING - 1);
				conf_log_path[MAX_CONFIG_STRING - 1] = 0;
				debug(DEBUG_EXPECTED,"New log file path: %s",
				conf_log_path);

				break;

			/* Was it a no-backgrounding request? */
			case 'n':

				/* Mark that we shouldn't background. */
				no_background = 1;

				break;


			/* Was it a version request? */
			case 'v':
				hand_show_version();
				exit(0);
	

			
			/* It was something weird.. */
			default:
				panic("Unhandled getopt return value %d", optchar);
		}
	}
	
	/* If there were any extra arguments, we should complain. */

	if(optind < argc) {
		fatal("Extra argument on commandline, '%s'", argv[optind]);
	}

	
	/* Parse the configuration file */

	confscan(conf_file, conf_section);
	
	if(max_node_addr > 254)
		fatal("config file error, max_node_addr can't be greater than 254");
	
	/* Make sure we are not already running (.pid file check). */
	if(pid_read(conf_pid_path) != -1) {
		fatal("hand is already running");
	}

	fd_setup();

   
	/* Fork into the background. */

	if(!no_background) {
		debug(DEBUG_STATUS, "Forking into background");

    		// If debugging is enabled, and we are daemonized, redirect the debug output to a log file if
    		// the path to the logfile is defined

		if((debuglvl) && (conf_log_path[0]))                          
			error_logpath(conf_log_path);

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
	/* We are now running.  Save the pid and be ready to cleanup on signal. */

	debug(DEBUG_STATUS, "Creating pid file and hooking termination signals.");
	if(pid_write(conf_pid_path, getpid()) != 0) {
		fatal("Could not write pid file '%s'.", conf_pid_path);
	}


	/* Hook signals */
  
	sigaction(SIGHUP, &hup_sig_action, NULL);
	sigaction(SIGINT, &int_sig_action, NULL);
	sigaction(SIGQUIT, &quit_sig_action, NULL);
	sigaction(SIGTERM, &term_sig_action, NULL);
	sigaction(SIGPIPE, &brkpipe_sig_action, NULL);
	sigaction(SIGCHLD, &child_sig_action, NULL);

	/* Send one broadcast enum packet to set up CRC16 transfers on capable nodes */

	send_broadcast_enum();
	
	/* We loop forever handling input and output. */

	for(;;) {
		u8 buffer[256];
		char done, packet_state, subst, sockrm, c;

		/* 
		 * Wait for input to be available. 
		 */
		//debug(DEBUG_STATUS, "Waiting for events.");
		retval=poll(pollfd, num_poll_fds, POLL_MAX_COUNT);
		if(retval == -1) {
			/* If we are interrupted, determine the cause  */
			if(errno == EINTR) {
				if(exitRequest == 1)
          				hand_exit();
        			else if(reconfigRequest == 1)
					 handReconfig();
       		 		else
        	  			debug(DEBUG_UNEXPECTED, "EINTR received in poll without a flag, restarting poll.");
				continue;
			}
			/* Nope, poll broke. */
			fatal_with_reason(errno, "Poll failed");
		}
		if(retval == 0){ /* The poll timed out */
			continue;
		}
		debug(DEBUG_STATUS, "Woken for events");
		

		si = fd_index(FD_RS485);
		if((si >= 0) && (pollfd[si].revents)) {
	
			/* Get the data bytes */

			for(i = 0, packet_state = IPS_INIT, subst = 0, done = 0; !done && (i < 256); ){
				retval = hanio_read(hanio, &c, 1, MAX_INTR_RESPONSE_TIME);
				if(retval < 1)
					break;
				switch(packet_state){
					case IPS_INIT:
						if(c == STX)
							packet_state = IPS_RCV;
						else{
							buffer[i++] = c;
							packet_state = IPS_IGN;
						}
						break;

					case IPS_RCV:
						if(!subst){
							if(c == ETX)
								done = 1;
							else if( c == SUBST)
								subst = 1;
							else
								buffer[i++] = c;
						}
						else{
							subst = 0;
							buffer[i++] = c;
						}
						break;

					case IPS_IGN:
						buffer[i++] = c;
						break;

					default:
						done = 1;
						break;
				}
			}

							
			if(!retval || !done || (i == 3 && buffer[0] != HDCINTRQ) || (i == 4 && buffer[0] != HDCINTRQ16)){
				debug(DEBUG_UNEXPECTED, "Spurious data received on network");

				/* Count the event */

				error_stats.spurious_packets++;
				debug_hexdump(DEBUG_UNEXPECTED, buffer, i,"Spurious bytes: ");
			}
			else{
				if(buffer[0] == HDCINTRQ){
					if(calcCRC(buffer , i))
						debug(DEBUG_UNEXPECTED,"Bad CRC8 on interrupt packet");
					else
						process_interrupt_packet(buffer, i);
				}
				else{
					if(packetCheck16(buffer, i))
						debug(DEBUG_UNEXPECTED,"Bad CRC16 on interrupt packet");
					else
						process_interrupt_packet(buffer, i);
				}		
			}
				
		}
		
		/* 
		 * Was it a new unix domain client connection?
		 */
		si = fd_index(FD_UNIX_CMD);
		if((si >= 0) && (pollfd[si].revents)){

			debug(DEBUG_STATUS, "Accepting unix domain socket connection");
			
			/* Accept the user connection. */
			user_socket=accept(pollfd[si].fd, NULL, NULL);
			if(user_socket == -1) {
				debug(DEBUG_UNEXPECTED, "Could not accept unix domain socket");
				continue;
			}
			if(processClientCommand(user_socket))
				continue;	
		}

		/*
		 * Was it a new ipv4  command socket connection?
		 */
		si = fd_index(FD_INET_CMD);
		if((si >= 0) && (pollfd[si].revents)){
			struct sockaddr_storage peerAddress;
			socklen_t peerAddressSize  = sizeof(struct sockaddr_storage);
			char addrstr[INET6_ADDRSTRLEN];

			
			/* Accept the user connection. */
			user_socket=accept(pollfd[si].fd, (struct sockaddr *) &peerAddress, &peerAddressSize);
			if(user_socket == -1) {
				debug(DEBUG_UNEXPECTED, "Could not do accept on ipv4 command socket");
				continue;
			}

			debug(DEBUG_STATUS, "Accepting ipv4 command socket connection from %s", inet_ntop(peerAddress.ss_family, munge_ip_address(&peerAddress), addrstr, sizeof(addrstr)));
			
			if(processClientCommand(user_socket))
				continue;
		}


		/*
		 * Was it a new ipv6 command socket connection?
		 */
		si = fd_index(FD_INET6_CMD);
		if((si >= 0) && (pollfd[si].revents)){

			struct sockaddr_storage peerAddress;
			socklen_t peerAddressSize = sizeof(struct sockaddr_storage);
			char addrstr[INET6_ADDRSTRLEN];

			/* Accept the user connection. */
			user_socket=accept(pollfd[si].fd, (struct sockaddr *) &peerAddress, &peerAddressSize);
			if(user_socket == -1) {
				debug(DEBUG_UNEXPECTED, "Could not do accept on ipv6 command socket");
				continue;
			}

			debug(DEBUG_STATUS, "Accepting ipv6 command socket connection from %s",  inet_ntop(peerAddress.ss_family, munge_ip_address(&peerAddress), addrstr, sizeof(addrstr)));
			
			if(processClientCommand(user_socket))
				continue;
		}

		/*
		* Service text socket
		*/

		for(i = 0, sockrm = 0; i < num_poll_fds; i++){
			if(fdtype[i] != FD_CONNECTED_TEXT)
				continue;
			else{
				int res;
				if(pollfd[i].revents){// Read event on the text socket?
					if((res = socket_read_line(pollfd[i].fd, (char *) buffer, 80, 1000)) < 0)
						debug(DEBUG_UNEXPECTED,"Read Error on socket: %s", strerror(errno));
					if(res < 1){ // A return value of 0 means the far end disconnected, if negative, then a socket read error occured. Remove the socket in both cases.
						debug(DEBUG_STATUS,"Removing socket index %d from the poll list, res = %d", i, res);
						user_socket = pollfd[i].fd;
						if(close(user_socket) < 0)
							debug(DEBUG_UNEXPECTED,"Close error on text socket");
						if((i + 1) != num_poll_fds){ // Not the last fd in the poll list?
							debug(DEBUG_EXPECTED,"Moving socket array down over the top of the disconnected socket i = %d, num_poll_fds = %d", i, num_poll_fds);
							memmove(&pollfd[i], &pollfd[i + 1], (num_poll_fds - (i + 1)) * sizeof(struct pollfd));
						}
						else{
							debug(DEBUG_EXPECTED,"No memmove required i = %d, num_poll_fds = %d", i, num_poll_fds);
						}
						num_poll_fds--; // We have one less now
						intreportingena[i] = 0; // Int reporting off
						sockrm = 1;
						break;
					}
					else{
						debug(DEBUG_STATUS,"Bytes read: %d\n", res);
						text_command(i, (char *) buffer, res);
						break;
					}
				}
			}
		}
		if(sockrm) // If we removed a text socket, start poll over with shortened list
			continue;			

		/*
		 * Was it a new ipv4  text socket connection?
		 */
		si = fd_index(FD_INET_TEXT);
		if((si >= 0) && (pollfd[si].revents)){
			struct sockaddr_storage peerAddress;
			socklen_t peerAddressSize  = sizeof(struct sockaddr_storage);
			char addrstr[INET6_ADDRSTRLEN];

			
			/* Accept the user connection. */
			user_socket=accept(pollfd[si].fd, (struct sockaddr *) &peerAddress, &peerAddressSize);
			if(user_socket == -1) {
				debug(DEBUG_UNEXPECTED, "Could not do accept on ipv4 text socket");
				continue;
			}

			debug(DEBUG_STATUS, "Accepting ipv4 text socket connection from %s", inet_ntop(peerAddress.ss_family, munge_ip_address(&peerAddress), addrstr, sizeof(addrstr)));

			/* Monitor sockets must be non-blocking */

			if(fcntl(retval, F_SETFL, O_NONBLOCK) == -1) {
				fatal("Could not set text socket to non-blocking.");
			}
	
			/* Add it to the poll list if there's room */

			if(add_text_socket(user_socket))
				close(user_socket);
			else{
				debug(DEBUG_EXPECTED, "num_poll_fds = %d", num_poll_fds);
			}
		}


		/*
		 * Was it a new ipv6 text socket connection?
		 */
		si = fd_index(FD_INET6_TEXT);
		if((si >= 0) && (pollfd[si].revents)){

			struct sockaddr_storage peerAddress;
			socklen_t peerAddressSize  = sizeof(struct sockaddr_storage);
			char addrstr[INET6_ADDRSTRLEN];

			/* Accept the user connection. */
			user_socket=accept(pollfd[si].fd, (struct sockaddr *) &peerAddress, &peerAddressSize);
			if(user_socket == -1) {
				debug(DEBUG_UNEXPECTED, "Could not do accept on ipv6 text socket");
				continue;
			}

			debug(DEBUG_STATUS, "Accepting ipv6 text socket connection from %s",  inet_ntop(peerAddress.ss_family, munge_ip_address(&peerAddress), addrstr, sizeof(addrstr)));
			
			if(add_text_socket(user_socket))
				close(user_socket);
			else{
				debug(DEBUG_EXPECTED, "num_poll_fds = %d", num_poll_fds);
			}

		}

	}
}
