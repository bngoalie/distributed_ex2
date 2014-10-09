CC=gcc

CFLAGS = -c -std=c99 -Wall -pedantic -g

all: ncp rcv t_ncp t_rcv

ncp: ncp.o sendto_dbg.o
	    $(CC) -o ncp ncp.o sendto_dbg.o

rcv: rcv.o sendto_dbg.o
	    $(CC) -o rcv rcv.o sendto_dbg.o

t_ncp: t_ncp.o
	    $(CC) -o t_ncp t_ncp.o

t_rcv: t_rcv.o
	    $(CC) -o t_rcv t_rcv.o

clean:
	rm *.o
	rm ncp
	rm rcv
	rm t_ncp
	rm t_rcv

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


