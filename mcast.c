#include "net_include.h"
#include "recv_dbg.h"

struct sockaddr_in initUnicastSend(int* uss, int next_ip);
int main(int argc, char **argv)
{
    /*
     *  DECLARATIONS
     */
    struct sockaddr_in name;
    struct sockaddr_in send_addr;
    struct sockaddr_in send_addr_ucast;
    int                mcast_addr;
    struct ip_mreq     mreq;
    unsigned char      ttl_val;
    int                uss,usr; // Unicast sockets
    int                ss,sr;   // Multicast sockets
    fd_set             mask;
    fd_set             dummy_mask,temp_mask;
    int                bytes;
    int                num;
    char               mess_buf[MAX_PACKET_SIZE];
    int                machine_id;
    int                num_packets;
    int                num_machines;
    int                loss_rate;


    /* 
     *  INITIAL SETUP 
     */

    /* Need four arguements: number of packets, machine index, 
     * number of machines, and loss_rate_percent */
    if(argc != 5) {
        printf("Usage: mcast <num_of_packets> <machine_index>\
            <num_of_machines> <loss_rate>\n");
        exit(0);
    }

    num_packets = atoi(argv[1]);            // Number of packets
    machine_id = atoi(argv[2]);             // Machine index
    num_machines = atoi(argv[3]);           // Number of machines
    loss_rate = atoi(argv[4]);              // Loss rate

    /* Initialize recv_dbg with loss rate */
    recv_dbg_init(loss_rate, machine_id);
    
    /* Set up Unicast Receive (send has to wait) */
    usr = socket(AF_INET, SOCK_DGRAM, 0);   // Socket for receiving unicast
    if (usr<0) {
        perror("mcast: (unicast) socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(MCAST_PORT+1);

    /* socket on which to receive*/
    if (bind(usr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Ucast: bind");
        exit(1);
    }

    /* Set up Multicasting */
    mcast_addr = MCAST_IP;                  // (225.1.2.101)

    sr = socket(AF_INET, SOCK_DGRAM, 0);    // Socket for receiving
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

    ss = socket(AF_INET, SOCK_DGRAM, 0);    // Socket for sending
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
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  // Multicast address
    send_addr.sin_port = htons(MCAST_PORT);
    
    int round = 0;
    int has_token = 0;
    Token token;
    if (machine_id == 0) {
        /* Set initial start token vals */
        token.seq = -1;
        token.aru = -1;
        token.recv = 1;
        has_token = 1;
    }
    
    /* TODO: Initialize ucast socket */
    

    /* Set masks for I/O Multiplexing */
    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    FD_SET( usr, &mask );
    Packet *start_packet;
    int waiting = 1;

    /*
     *  WAIT TO BEGIN
     */ 
    while (waiting == 1) {
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                recv( sr, mess_buf, sizeof(mess_buf), 0 );
                start_packet = (Packet *)&mess_buf;
                if (start_packet->type == 0) {  // Received start_mcast packet
                    printf("BEGINNIG MULTICAST\n");
                    waiting = 0;
                }
            }
        }
    }

    /*
     *  LOOP
     */ 
    for (;;) {
        if (has_token == 1) {
            int packet_id = token.seq + 1;
            /* TODO: Burst messages */
            /* TODO: while passing an mcast token, consider a smaller burst for messages */
            Message send_msg;
            send_msg.type = 4;
            send_msg.machine = machine_id;
            

            int burst_count = 0;
            /*Send first half of packets*/
            while (burst_count < BURST_MSG/2) {
                
                burst_count++;
            } 
            /* TODO: Send out multicast/unicast token */
            if (uss == 0 && token.type == 1  
                && ((StartToken *)&token)->ip_array[machine_id % num_machines] != 0) {
                send_address_ucast = initUnicastSend(&uss, 
                    ((StartToken *)&token)->ip_array[machine_id % num_machines]);
            }
            if (token.type == 1 
                && ((StartToken *)&token)->ip_array[machine_id-1] == 0) {
                /*If the start token has not set this machine's ip address yet. */
                char my_name[80] = {'\0'};
                struct hostent  h_ent;
                struct hostent  *p_h_ent;
                int             my_ip;

                gethostname(my_name, 80);
                p_h_ent = gethostbyname(my_name);
                if ( p_h_ent == NULL ) {
                    printf("myip: gethostbyname error.\n");
                    exit(1);
                }

                memcpy( &h_ent, p_h_ent, sizeof(h_ent));
                memcpy( &my_ip, h_ent.h_addr_list[0], sizeof(my_ip) );

                ((StartToken *)&token)->ip_array[machine_id-1] = my_ip;
            }
            /* TODO: Update token seq, aru, etc. appropriately*/
            int token_size = token.type == 1 ? 52 : 12 ;
            /* TODO: Adjust size of token for retransmission requests*/
            token.recv = (machine_id%10) + 1;
            if (uss == 0) {
                /* Multicast Token */  
                sendto(ss, (char *)&token, token_size, 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            } else {
                /* Unicast Token */
                sendto(uss, (char *)&token, token_size, 0, 
                  (struct sockaddr *)&send_addr_ucast, sizeof(send_addr_ucast) );

            }

            /* Send rest of messages*/
            while (burst_count < BURST_MSG) {
                
                burst_count++;
            }
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

/*
 *  Initialize the unicast send sockets
 */
struct sockaddr_in initUnicastSend(int* uss, int next_ip)
{
    struct sockaddr_in send_addr;

    /* Socket on which to send */
    *uss = socket(AF_INET, SOCK_DGRAM, 0); /* socket for sending (udp) */
    if (*uss<0) {
        perror("Ucast: socket");
        exit(1);
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = next_ip;
    send_addr.sin_port = htons(MCAST_PORT + 1);
}
