#include "net_include.h"

int main()
{
    struct sockaddr_in send_addr;
    int                mcast_addr;
    unsigned char      ttl_val;
    int                ss;

    mcast_addr = MCAST_IP; /* (225.1.2.101) */

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d\n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(MCAST_PORT);

    Packet start;
    start.type = 5;

    sendto(ss, (char *)&start, sizeof(Packet), 0, 
        (struct sockaddr *)&send_addr, sizeof(send_addr) );

    return 0;
}

