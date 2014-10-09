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

#define PORT	        10010
/* somehow define mcast IP: 255.1.2.101 */
#define MAX_PACKET_SIZE 1400
#define PACKET_ID int
#define PACKET_TYPE int
#define PAYLOAD_SIZE    MAX_PACKET_SIZE-sizeof(PACKET_TYPE)-sizeof(PACKET_ID)
#define BURST_MAX       20
#define MEG             1048576
#define MEG50           50*MEG

/* Packet: Struct for generic packet */
typedef struct {
    PACKET_TYPE type;
    char payload[MAX_PACKET_SIZE- sizeof(PACKET_TYPE)];    
} Packet;

