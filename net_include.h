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
#define UNICAST_PORT    10011
#define MCAST_IP        225 << 24 | 1 << 16 | 2 << 8 | 101 /* (255.1.2.101)  */
#define BURST_MSG       130
#define WINDOW_SIZE     7000
#define MAX_PACKET_SIZE 1400
#define PAYLOAD_SIZE    1200
#define TOKEN_BURST     3
#define RAND_RANGE_MAX  1000000
#define TIMEOUT_USEC    200
#define DEBUG           0 
#define MAX_MACHINES    10
#define NUMERATOR       1
#define DENOMINATOR    4 

/* Packet types:
 * 0:   Start multicast
 * 1:   Start token
 * 2:   Token
 * 3:   Multicast message
 * 4:   Ack Packet
 */

/* Packet: Struct for generic packet */
typedef struct {
    int type;
    char payload[MAX_PACKET_SIZE- sizeof(int)];    
} Packet;

/* StartToken: Struct for starting token */
typedef struct {
    int         type;
    int         tok_id;
    int         seq;
    int         aru;
    int         recv;
    int         done;
    int         ip_array[10];   // Max 10 machines
    int         rtr[MAX_PACKET_SIZE - 64];
} StartToken;

/* Token: Struct for standard unicasted token */
typedef struct {
    int         type;
    int         tok_id;
    int         seq;
    int         aru;
    int         recv;
    int         done; // Use bitmasks
    int         rtr[MAX_PACKET_SIZE - 24];
} Token;

/* Message: Struct for multicasted message */
typedef struct {
    int         type;
    int         machine;
    int         packet_id;
    int         rand;
    char        payload[PAYLOAD_SIZE];
} Message;
