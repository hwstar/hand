# Makefile

PACKAGE = han
VERSION = 0.0.10
CONTACT = <hwstar@rodgers.sdcoxmail.com>

CC = gcc
CFLAGS = -O2 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'

# Install paths for built executables

BINDIR = /usr/local/sbin

DAEMONDIR = /usr/local/sbin

#Libraries

HANTSTLIBS = -lpopt

IRRLIBS = -lpopt

# Object file lists

HANOBJS = hand.o hanio.o socket.o pid.o confscan.o error.o

HANTSTOBJS = hantst.o confscan.o hanclient.o socket.o pid.o error.o

IRROBJS = irr.o confscan.o irrconfscan.o statevar.o hanclient.o socket.o pid.o error.o

#Dependencies

all: hand hantst irr

hand.o: Makefile options.h error.h confscan.h hanio.h socket.h pid.h han.h tnd.h

hanio.o: Makefile error.h hanio.h tnd.h

error.o: Makefile error.h options.h tnd.h

socket.o: Makefile error.h socket.h tnd.h

pid.o: Makefile pid.h tnd.h

confscan.o: Makefile error.h confscan.h tnd.h

hanclient.o: Makefile error.h socket.h pid.h han.h hanclient.h options.h tnd.h

statevar.o: Makefile error.h statevar.h tnd.h

irrconfscan.o: Makefile irrconfscan.h error.h tnd.h

irr.o: Makefile options.h error.h socket.h confscan.h han.h irrconfscan.h statevar.h hanclient.h pid.h tnd.h

hantst.o: Makefile options.h error.h confscan.h han.h hanclient.h pid.h tnd.h

#Rules

hand: $(HANOBJS)
	$(CC) $(CFLAGS) -o hand $(HANOBJS)

hantst: $(HANTSTOBJS)
	$(CC) $(CFLAGS) -o hantst $(HANTSTOBJS) $(HANTSTLIBS) 

irr:  $(IRROBJS)
	$(CC) $(CFLAGS) -o irr $(IRROBJS) $(IRRLIBS)
  
clean:
	-rm -f hand hantst irr *.o core

install:
	cp hand $(DAEMONDIR)
	cp irr $(BINDIR)
	cp hantst $(BINDIR)


dist:
	-rm -f $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION).tar
	-rm -rf $(PACKAGE)-$(VERSION)
	mkdir $(PACKAGE)-$(VERSION)
	mkdir $(PACKAGE)-$(VERSION)/examples
	cp README COPYING Makefile *.c *.h $(PACKAGE)-$(VERSION)
	cp examples/* $(PACKAGE)-$(VERSION)/examples
	chmod 644 $(PACKAGE)-$(VERSION)/*
	chmod 775 $(PACKAGE)-$(VERSION)/examples
	chmod 664 $(PACKAGE)-$(VERSION)/examples/*
	tar cf $(PACKAGE)-$(VERSION).tar ./$(PACKAGE)-$(VERSION)
	gzip -9 $(PACKAGE)-$(VERSION).tar
	rm -rf $(PACKAGE)-$(VERSION)
