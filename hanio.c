/*
 * Handle the RS-485 interface to the hardware nodes
 *
 * Modified from x10.c by Steve Rodgers
 * Credit for x10.c work goes to Steven Brown <swbrown@ucsd.edu>
 *
 * Copyright (C) 1999 Stephen Rodgers 
 *
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
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "error.h"
#include "hanio.h"

/* 
 * Open the serial device. 
 *
 * Description of how to do the serial handling came from some mini serial
 * port programming howto.
 */

hanioStuff *hanio_open(char *han_tty_name) {
	struct termios termios;
	hanioStuff *hanio;

	if((hanio = malloc(sizeof(hanioStuff))) == NULL)
		fatal("Could not malloc memory for hanioStuff");

	/* 
	 * Open the hanio tty device.
	 */
	hanio->fd=open(han_tty_name, O_RDWR | O_NOCTTY | O_NDELAY);
	if(hanio->fd == -1) {
		fatal_with_reason(errno, "Could not open tty '%s'",han_tty_name);
	}
	
	
	/* Set the options on the port. */
	
	/* We don't want to block reads. */
	if(fcntl(hanio->fd, F_SETFL, O_NONBLOCK) == -1) {
		fatal_with_reason(errno, "Could not set hanio to non-blocking");
	}
	
	/* Get the current tty settings. */
	if(tcgetattr(hanio->fd, &termios) != 0) {
		fatal_with_reason(errno, "Could not get tty attributes");
	}
	
	/* Enable receiver. */
	termios.c_cflag |= CLOCAL | CREAD;
	
	/* Set to 8N1. */
	termios.c_cflag &= ~PARENB;
	termios.c_cflag &= ~CSTOPB;
	termios.c_cflag &= ~CSIZE;
	termios.c_cflag |=  CS8;
	
	/* Accept raw data. */
	termios.c_lflag &= ~(ICANON | ECHO | ISIG);
	termios.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONLRET | OFILL);
	termios.c_iflag &= ~(ICRNL | IXON | IXOFF | IMAXBEL);
	
	/* Set the speed of the port. */
	if(cfsetospeed(&termios, B9600) != 0) {
		fatal("Could not set tty output speed.");
	}
	if(cfsetispeed(&termios, B9600) != 0) {
		fatal("Could not set tty input speed.");
	}
	
	/* Save our modified settings back to the tty. */
	if(tcsetattr(hanio->fd, TCSANOW, &termios) != 0) {
		fatal_with_reason(errno, "Could not set tty attributes");
	}
	
	return(hanio);
}

/* Close the TTY port, and free the hanio structure */

void hanio_close(hanioStuff *hanio){
	close(hanio->fd);
	free(hanio);
}

/* 
 * Wait for the hanio hardware to provide us with some data.
 *
 * This function should only be called when we know the hanio should have sent
 * us something.  We don't wait long in here, if it isn't screwed up, it
 * should be sending quite quickly.  We return true if we got a byte and
 * false if we timed out waiting for one.
 */
int hanio_wait_read(hanioStuff *hanio, int rx_timeout) {
	fd_set read_fd_set;
	struct timeval tv;
	int retval;
	
	/* Wait for data to be readable. */
	for(;;) {
		
		/* Make the call to select to wait for reading. */
		FD_ZERO(&read_fd_set);
		FD_SET(hanio->fd, &read_fd_set);
		tv.tv_sec=0;
		tv.tv_usec=rx_timeout;
		retval=select(hanio->fd+1, &read_fd_set, NULL, NULL, &tv);
		
		/* Did select error? */
		if(retval == -1) {
			
			/* If it's an EINTR, go try again. */
			if(errno == EINTR) {
//			debug(DEBUG_EXPECTED, "Signal recieved in read select, restarting.");
				continue;
			}
			
			/* It was something weird. */
			fatal_with_reason(errno, "Error in read select");
		}
		
		/* Was data available? */
		if(retval) {	
			
			/* We got some data, return ok. */
			return TRUE;
		}
		
		/* No data available. */
		else {
			
			/* We didn't get any data, this is a fail. */
			return FALSE;
		}
	}
}


/* 
 * Wait for the hanio hardware to be writable.
 */
int hanio_wait_write(hanioStuff *hanio, int tx_timeout) {
	fd_set write_fd_set;
	struct timeval tv;
	int retval;
	
	/* Wait for data to be writable. */
	for(;;) {
		
		/* Make the call to select to wait for writing. */
		FD_ZERO(&write_fd_set);
		FD_SET(hanio->fd, &write_fd_set);
		tv.tv_sec=0;
		tv.tv_usec=tx_timeout;
		retval=select(hanio->fd+1, NULL, &write_fd_set, NULL, &tv);
		
		/* Did select error? */
		if(retval == -1) {
			
			/* If it's an EINTR, go try again. */
			if(errno == EINTR) {
//			debug(DEBUG_EXPECTED, "Signal recieved in write select, restarting.");
				continue;
			}
			
			/* It was something weird. */
			fatal_with_reason(errno, "Error in write select");
		}
		
		/* Can we write data? */
		if(retval) {	
			
			/* We can write some data, return ok. */
			return TRUE;
		}
		
		/* No data writable. */
		else {
			
			/* We can't write any data, this is a fail. */
			return FALSE;
		}
	}
}


/* 
 * Read data from the hanio hardware.
 *
 * Basically works like read(), but with a select-provided readable check
 * and timeout.
 * 
 * Returns the number of bytes read.  This might be less than what was given
 * if we ran out of time.
 */
ssize_t hanio_read(hanioStuff *hanio, void *buf, size_t count, int rx_timeout) {
	int bytes_read;
	ssize_t retval;
	
	/* Read the request into the buffer. */
	for(bytes_read=0; bytes_read < count;) {
		
		/* Wait for data to be available. */
		if(!hanio_wait_read(hanio, rx_timeout)) {
//			debug(DEBUG_UNEXPECTED, "Gave up waiting for hanio to be readable.");
			return(bytes_read);
		}
		
		/* Get as much of it as we can.  Loop for the rest. */
		retval=read(hanio->fd, (char *) buf + bytes_read, count - bytes_read);
		if(retval == -1) {
			fatal_with_reason(errno, "Failure reading hanio response");
		}
		bytes_read += retval;
//		debug(DEBUG_ACTION, "Read %i bytes, %i remaining.", retval, count - bytes_read);
	}
	
	/* We're all done. */
	return(bytes_read);
}


/* 
 * Write data to the hanio hardware.
 *
 * Basically works like write(), but with a select-provided writeable check
 * and timeout.
 * 
 * Returns the number of bytes written.  This might be less than what was
 * given if we ran out of time.
 */
ssize_t hanio_write(hanioStuff *hanio, void *buf, size_t count, int tx_timeout) {
	int bytes_written;
	ssize_t retval;
	
	/* Write the buffer to the hanio hardware. */
	for(bytes_written=0; bytes_written < count;) {
		
		/* Wait for data to be writeable. */
		if(!hanio_wait_write(hanio, tx_timeout)) {
//			debug(DEBUG_UNEXPECTED, "Gave up waiting for hanio to be writeable.");
			return(bytes_written);
		}
		
		/* Get as much of it as we can.  Loop for the rest. */
		retval=write(hanio->fd, (char *) buf + bytes_written, count - bytes_written);
		if(retval == -1) {
			fatal_with_reason(errno, "Failure writing hanio buffer");
		}
		bytes_written += retval;
//		debug(DEBUG_ACTION, "Wrote %i bytes, %i remaining.", retval, count - bytes_written);
	}
	
	/* We're all done. */
	return(bytes_written);
}

