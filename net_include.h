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
#define MCAST_IP        225 << 24 | 1 << 16 | 2 << 8 | 101; /* (255.1.2.101)  */
#define BURST_MSG       20
#define WINDOW_SIZE     1500
#define MAX_PACKET_SIZE WINDOW_SIZE * 4 + 16
#define PAYLOAD_SIZE    1200
#define BURST_TOKEN     2
#define RAND_RANGE_MAX        1000000

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
    int         seq;
    int         aru;
    int         recv;
    int         ip_array[10];   // Max 10 machines
    int         rtr[MAX_PACKET_SIZE - 56];
} StartToken;

/* Token: Struct for standard unicasted token */
typedef struct {
    int         type;
    int         seq;
    int         aru;
    int         recv;
    int         done; // Use bitmasks
    int         rtr[MAX_PACKET_SIZE - 16];
} Token;

/* Message: Struct for multicasted message */
typedef struct {
    int         type;
    int         machine;
    int         packet_id;
    int         rand;
    int         payload[PAYLOAD_SIZE];
} Message;
