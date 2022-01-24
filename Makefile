.POSIX:

CFLAGS = -g -std=c99 -pedantic -Wall -Wextra -Wshadow

include config.mk

.c:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

semln : semln.c
