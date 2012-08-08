/*
 * hanio definitions.
 *
 * Modified from x10.h by Steve Rodgers
 * Credit for x10.h work goes to Steven Brown <swbrown@ucsd.edu>
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

#ifndef HANIO_H
#define HANIO_H

#include <time.h>
#include <unistd.h>

/* The maximum time to wait for an expected byte to be readable. */
#define HANIO_WAIT_READ_USEC_DELAY 5000000

/* The maximum time to wait to be able to write to the x10 hardware. */
#define HANIO_WAIT_WRITE_USEC_DELAY 5000000

/* Typedefs. */
typedef struct haniostuff hanioStuff;

/* Structure to hold hanio info. */
struct haniostuff {
	
	/* File descriptor to the hanio tty. */
	int fd;
};

/* Prototypes. */
hanioStuff *hanio_open(char *han_tty_name);
void hanio_close(hanioStuff *hanio);
int hanio_wait_read(hanioStuff *hanio, int rx_timeout);
int hanio_wait_write(hanioStuff *hanio, int tx_timeout);
int hanio_getchar(hanioStuff *hanio, char *c);
ssize_t hanio_read(hanioStuff *hanio, void *buf, size_t count, int rx_timeout);
ssize_t hanio_write(hanioStuff *hanio, void *buf, size_t count, int tx_timeout);
int hanio_flush_input(hanioStuff *hanio);


#endif
