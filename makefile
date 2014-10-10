CC=gcc

CFLAGS = -c -std=c99 -Wall -Wextra -pedantic -g -D_GNU_SOURCE

all: start_mcast mcast

start_mcast: start_mcast.o
	    $(CC) -o start_mcast start_mcast.o

mcast: mcast mcast.o recv_dbg.o
	    $(CC) -o mcast mcast.o recv_dbg.o

clean:
	rm *.o
	rm start_mcast
	rm mcast

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


