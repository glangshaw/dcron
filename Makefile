# Makefile for dillon's cron and crontab
#

PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
BINDIR = $(PREFIX)/bin
SBINDIR = $(PREFIX)/sbin

CRONTAB_GROUP = users

CFLAGS = -O2 --std=c99 -pedantic -Wpedantic -Wall -Wextra
CPPFLAGS = -D_DEFAULT_SOURCE

CROND_OBJS = crond.o subs.o database.o job.o
CRONTAB_OBJS = crontab.o subs.o

all:  crond crontab

crond:	$(CROND_OBJS)

crontab:  $(CRONTAB_OBJS)

clean:  cleano
	rm -f crond crontab

cleano:
	rm -f *.o

install: crond crontab
	install -D -o root -g root -m 0755 crond $(DESTDIR)$(SBINDIR)/crond
	install -D -o root -g $(CRONTAB_GROUP) -m 4750 crontab $(DESTDIR)$(BINDIR)/crontab
	install -D -o root -g root -m 0644 crontab.1 $(DESTDIR)$(MANDIR)/man1/crontab.1
	install -D -o root -g root -m 0644 crond.8 $(DESTDIR)$(MANDIR)/man8/crond.8

include depend.mk
