#include "net_include.h"
#include "recv_dbg.h"

int main(int argc, char **argv)
{
    struct sockaddr_in name;
    struct sockaddr_in send_addr;

    int                mcast_addr;

    struct ip_mreq     mreq;
    unsigned char      ttl_val;

    int                ss,sr;
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    char               mess_buf[MAX_PACKET_SIZE];
    int                machine_id;
    int                num_packets;
    int                num_machines;
    int                loss_rate;


    /* Parse input arguements */

    /* Need four arguements: number of packets, machine index, 
     * number of machines, and loss_rate_percent */
    if(argc != 5) {
        printf("Usage: mcast <num_of_packets> <machine_index> <num_of_machines>\
 <loss_rate>\n");
        exit(0);
    }

    /* Number of packets */
    num_packets = atoi(argv[1]);

    /* Machine Index */
    machine_id = atoi(argv[2]);

    /* Number of Machines */
    num_machiens = atoi(argv[3]);

    /* Set loss rate */
    loss_rate = atoi(argv[4]);
    recv_dbg_init(loss_rate, machine_id);

    /*Set up Multicasting*/
    mcast_addr = MCAST_IP; // (225.1.2.101)

    sr = socket(AF_INET, SOCK_DGRAM, 0); // socket for receiving
    if (sr<0) {
        perror("Mcast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(MCAST_PORT);

    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) 
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(MCAST_PORT);

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    Packet *start_packet;
    int waiting = 1;
    /* Wait for start_mcast packet*/
    while (waiting == 1)
    {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                recv( sr, mess_buf, sizeof(mess_buf), 0 );
                start_packet = (Packet *)&mess_buf;
                if (start_packet->type == 0) {
                    /* Received start_mcast packet type. Exit while loop.*/
                    printf("BEGINNIG MULTICAST\n");
                    waiting = 0;
                }
            }
        }
    }

    /* Initialize ucast socket */

    int round = 0;
    int has_token = 0;
    McastToken mcast_token;
    if (machine_id == 0) {
        /* TODO: Create first token*/
        has_token = 1;
    }
    for (;;) {
        if (has_token == 1) {
            /* TODO: Burst messages */
            /* TODO: Send out (multicast) mcast_token */
            /* Set has_token to zero because sent out token*/
            has_token = 0;
        }
        /* Select: receive or timeout. */
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                /*  TODO: Check type of packet. If is token, set has_token to 1.*/
            } else {
                /* TIMEOUT*/
            }
        } 
    }

    return 0;

}

