#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <errno.h>

#define MCAST_PORT	    10010
#define MCAST_IP        4278256229  // IP: 255.01.02.101
#define BURST_MSG       20
#define WINDOW_SIZE     2048
#define MAX_PACKET_SIZE WINDOW_SIZE * 4 + 12
#define PAYLOAD_SIZE    1200
#define BURST_TOKEN     2
#define PACKET_ID       int
#define PACKET_TYPE     int

/* Packet: Struct for generic packet */
typedef struct {
    PACKET_TYPE type;
    char payload[MAX_PACKET_SIZE- sizeof(PACKET_TYPE)];    
} Packet;

/* McastToken: Struct for multicasted token */
typedef struct {
    PACKET_TYPE type;
    int         seq;
    int         aru;
    int         recv;
    int         ip_array[10];   // Max 10 machines
    int         rtr[MAX_PACKET_SIZE - 52];
} McastToken;

/* Token: Struct for standard unicasted token */
typedef struct {
    PACKET_TYPE type;
    int         seq;
    int         aru;
    int         sndr;
    int         recv;
    int         ip_array[10];   // Max 10 machines
    int         rtr[MAX_PACKET_SIZE - 56];
} Token;

