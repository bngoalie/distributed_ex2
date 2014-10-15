#include "net_include.h"
#include "recv_dbg.h"
#include <math.h>
#include <limits.h>

#define TIMEOUT_USEC 100
#define DEBUG 1
#define MAX_MACHINES 10

struct sockaddr_in initUnicastSend(int);

int main(int argc, char **argv) {
    /*
     *  DECLARATIONS
     */
    struct sockaddr_in name;
    struct sockaddr_in send_addr;
    struct sockaddr_in send_addr_ucast;
    struct sockaddr_in send_addr_ucast_ack;
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
    int                num_packets_sent = 0;
    int                num_machines;
    int                loss_rate;
    Message            window[WINDOW_SIZE];
    int                waiting_for_token_ack = 0;
    int                aru = -1;
    int                prev_aru = INT_MAX;
    int                prev_sent_aru = -1;
    int                lowered_aru = 0;
    int                start_of_window = 0;
    int                prev_recvd_seq = -1;
    FILE               *fw = NULL;
    int                random_number;
    int                packet_id = -1;
    int                token_size = 0;
    struct timeval     timeout;
    int                round = 0;
    struct timeval     start_time;    
    struct timeval     end_time;
    Packet             ack_packet;
    
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

    /* Check number of machines */
    if(num_machines > MAX_MACHINES) {
        perror("mcast: arguments error - too many machines\n");
        exit(0);
    }

    /* Initialize recv_dbg with loss rate */
    recv_dbg_init(loss_rate, machine_id);
    
    send_addr_ucast.sin_addr.s_addr = 0;
    send_addr_ucast_ack.sin_addr.s_addr = 0;
    
    /* Open file writer */
    char file_name[15];
    sprintf(file_name, "%d", machine_id);
    if((fw = fopen(strcat(file_name, ".out"), "w")) == NULL) {
        perror("fopen");
        exit(0);
    }
    
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
    
    /* Socket on which to send */
    uss = socket(AF_INET, SOCK_DGRAM, 0); /* socket for sending (udp) */
    if (uss<0) {
        perror("Ucast: socket");
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
    
    u_char loop = 0;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_LOOP, (void *)&loop, 
        sizeof(loop)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  // Multicast address
    send_addr.sin_port = htons(MCAST_PORT);
    
    for (int idx = 0; idx < WINDOW_SIZE; idx++) {
        window[idx].type = -1;
    }

    /* Set ack packet type */
    ack_packet.type = 4;

    int has_token = 0;
    Token token;
    if (machine_id == 1) {
        /* Set initial start token vals */
        token.done = 0;
        token.seq = -1;
        token.aru = -1;
        token.recv = 1;
        token.type = 1;
        bytes = 64;
        has_token = 1;
        for (int idx = 0; idx < num_machines; idx++) {
            ((StartToken *)&token)->ip_array[idx] = 0;
        }
    }
    token.tok_id = -1;

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
                if (start_packet->type == 5) {  // Received start_mcast packet
                    if (DEBUG) {
                        printf("BEGINNIG MULTICAST\n");
                    }
                    gettimeofday(&start_time, 0); 
                    waiting = 0;
                }
            }
        }
    }

    /*
     *  LOOP
     */ 
    for (;;) {
        if (DEBUG) {
            printf("Entering main loop (infinite):\n");
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_USEC;
        
        if (has_token == 1) {
            if(DEBUG == 1) {
                printf("At round %d\n", (++round));
                printf("\tReceived token with type %d, seq %d, aru %d\n", token.type, token.seq, token.aru);
                if (token.type == 1) {
                   
                    printf("Token has IP addresses: ");
                   
                    for (int idx = 0; idx < num_machines; idx++) {
                        printf("%d ", ((StartToken *)&token)->ip_array[idx]);
                    }
                    printf("\nmachine to ack is at index %d and has ip address %d\n", (machine_id-2+num_machines)%num_machines, ((StartToken *)&token)->ip_array[(machine_id - 2 + num_machines) % num_machines]);
                }
            }
            if (send_addr_ucast_ack.sin_addr.s_addr == 0 && token.type == 1  
                && ((StartToken *)&token)->ip_array[(machine_id - 2 + num_machines) % num_machines] != 0) {
                if (DEBUG) {
                    printf("Set send_addr_ucast_ack with initUnicastSend\n");
                }
                send_addr_ucast_ack = initUnicastSend(
                    ((StartToken *)&token)->ip_array[(machine_id - 2 + num_machines) % num_machines]);
                if (DEBUG) {
                    printf("Ack ip address is now %d \n", send_addr_ucast_ack.sin_addr.s_addr);
                }
            }
            if (send_addr_ucast_ack.sin_addr.s_addr != 0) {
                /* Unicast ack*/
                if(DEBUG) {
                    printf("Sending ack...\n");
                }
                sendto(uss, (char *)&ack_packet, 32, 0, 
                    (struct sockaddr *)&send_addr_ucast_ack, sizeof(send_addr_ucast_ack));
            }
            
            int tmp_prev_aru = prev_sent_aru;
               
            /* Check rtr for packets machine is holding. If have packet, add 
             * to array of packets to send in burst. */
            Message *packets_to_burst[BURST_MSG];
            int packets_to_burst_itr = 0;
            int window_itr = start_of_window;
            int rtr_itr = 0;
            int rtr_size = (token.type == 1 ? bytes - 64 : bytes - 24)/sizeof(int);
            int new_rtr[MAX_PACKET_SIZE - 24];
            int new_rtr_itr = 0;
            
            if(DEBUG) {
            printf("rtr size: %d, window_itr start value: %d, prev_recvd_seq: %d\n", rtr_size, window_itr, prev_recvd_seq);
            }
            while (rtr_itr < rtr_size && window_itr <= prev_recvd_seq) {
                if (DEBUG) {
                    //printf("Enter loop for creating new_rtr and packets_to_burst\n");
                }
                if (window[window_itr % WINDOW_SIZE].type == -1) {
                    new_rtr[new_rtr_itr] = window_itr;
                    if (DEBUG) {
                        printf("window missing id: add to new_rtr id %d\n", new_rtr[new_rtr_itr]);
                    }
                    if (token.rtr[rtr_itr] == window_itr) {
                        rtr_itr++;
                    }
                    window_itr++;
                    new_rtr_itr++;
                } else if (token.rtr[rtr_itr] < window[window_itr % WINDOW_SIZE].packet_id) {
                    new_rtr[new_rtr_itr] = token.rtr[rtr_itr];
                    if (DEBUG) {
                        printf("add to new_rtr id %d\n", new_rtr[new_rtr_itr]);
                    }
                    new_rtr_itr++;
                    rtr_itr++;
                } else if (token.rtr[rtr_itr] == window[window_itr % WINDOW_SIZE].packet_id) {
                    if (packets_to_burst_itr < BURST_MSG) {
                        if (DEBUG) {
                            printf("Adding to packets_to_burst retransmission of packet id %d\n", window[window_itr % WINDOW_SIZE].packet_id);
                        }
                        packets_to_burst[packets_to_burst_itr] = &(window[window_itr % WINDOW_SIZE]);
                        packets_to_burst_itr++;
                    } else {
                        new_rtr[new_rtr_itr] = token.rtr[rtr_itr];
                        if (DEBUG) {
                            printf("Cannot add packet to packets_to_burst; add to new_rtr (id %d)\n", new_rtr[new_rtr_itr]);
                        }
                        new_rtr_itr++;
                    }
                    window_itr++;
                    rtr_itr++;
                } else {
                    if (DEBUG) {
                        //printf("increment window_itr\n");
                    }
                    window_itr++;
                }
            }
            
            while (rtr_itr < rtr_size) {
                new_rtr[new_rtr_itr] = token.rtr[rtr_itr];
                if (DEBUG) {
                    printf("Adding to new_rtr (id %d)\n", new_rtr[new_rtr_itr]);
                }
                new_rtr_itr++;
                rtr_itr++;
            }
            if (DEBUG) {
                printf("window_itr: %d, prev_recvd_seq: %d\n", window_itr, prev_recvd_seq);
            }
            while (window_itr <= prev_recvd_seq) {
                if (DEBUG) {
                    //printf("try adding locally missing to new_rtr, entered loop\n");
                }
                if (window[window_itr % WINDOW_SIZE].type == -1) {
                    new_rtr[new_rtr_itr] = window_itr;
                    if (DEBUG) {
                        printf("Adding to new_rtr (id %d)\n", new_rtr[new_rtr_itr]);
                    }
                    new_rtr_itr++;
                }
                window_itr++;
            }
            if (DEBUG) {
                //printf("done processing received token's rtr\n");
            } 
            /* TODO: while passing an mcast token, consider a smaller burst for 
             * messages */
            packet_id = token.seq;
            while (packets_to_burst_itr < BURST_MSG 
                    && num_packets_sent+1 <= num_packets
                    && window[(packet_id+1) % WINDOW_SIZE].type == -1) {
                packet_id++;
                if (packet_id == aru + 1){
                    aru++;
                }
                 /* This is not a truly uniform distribution of random numbers */
                random_number = (rand() % RAND_RANGE_MAX) + 1;
                window[packet_id % WINDOW_SIZE].type = 3;
                window[packet_id % WINDOW_SIZE].packet_id = packet_id;
                window[packet_id % WINDOW_SIZE].machine = machine_id;
                window[packet_id % WINDOW_SIZE].rand = random_number;
                packets_to_burst[packets_to_burst_itr] = &(window[packet_id % WINDOW_SIZE]);
                if(DEBUG) {
                    printf("Adding new packet to packets_to_burst (id %d)\n", packet_id);
                }
                num_packets_sent++;
                packets_to_burst_itr++;
            }

            int burst_count = 0;
            /*send first half of packets*/
            while (burst_count < packets_to_burst_itr/4) {
                /* Multicast Message */  
                sendto(ss, (char *)packets_to_burst[burst_count], sizeof(Message),
                       0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                burst_count++;
            } 
            /* If token is a StartToken and this machine has yet to establish 
             * who to unicast to. */
            if (DEBUG)
                printf("IP address debug: Current send addr = %d, token type = %d, ip array val = %d\n",
                            send_addr_ucast.sin_addr.s_addr, token.type,
                            ((StartToken *)&token)->ip_array[machine_id % num_machines]);
            if (send_addr_ucast.sin_addr.s_addr == 0 && token.type == 1  
                && ((StartToken *)&token)->ip_array[machine_id % num_machines] != 0) {
                send_addr_ucast = initUnicastSend(
                    ((StartToken *)&token)->ip_array[machine_id % num_machines]);
                if (DEBUG)
                    printf("Set target unicast IP address to %d\n", send_addr_ucast.sin_addr.s_addr);
            }
            
            /* If the StartToken being passed around has yet to establish this 
             * machine's IP address, set it. */
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
            } else if (machine_id == num_machines - 1) {
                // Implicitly determined that already received start packet in a
                // prior round by not satsifying condition in if block above.
                token.type = 2;
            }
            
            /* Increment token id */
            token.tok_id++;

            /* Set initial token size (without rtr)*/
            token_size = token.type == 1 ? 64 : 24 ;
            
            /* Set new token's rtr to precomputed new_rtr */
            memcpy(token.rtr, new_rtr, new_rtr_itr * 4);
            /* Adjust size for new rtr */
            token_size += new_rtr_itr * 4;
            
            /* Set intended receiver. */
            token.recv = (machine_id%10) + 1;
                   
            if (DEBUG) {
                printf("\ttoken.aru: %d, local aru: %d, prev_aru: %d, prev_sent_aru: %d\n", token.aru, aru, prev_aru, prev_sent_aru);
            } 
            /* Update aru value if appropriate */
            int tmp_aru = token.aru;
            
            if (DEBUG)
                printf("\tlowered_aru: %d\n", lowered_aru);

            if (token.aru != prev_sent_aru) // If somebody else changed (lowered) the aru, clear local lowered flag
                lowered_aru = 0;

            if (aru < token.aru) {      // Local aru is less than token aru
                if (DEBUG) {
                    printf("\tlocal aru %d is lower than token.aru %d\n", aru, token.aru);
                }
                token.aru = aru;        // Lower token aru to local aru
                lowered_aru = 1;
            } else if (token.seq == token.aru) {    // Token aru is equal to seq
                token.aru = packet_id;              // Increment both equally
                aru = packet_id;
                lowered_aru = 0;
                if (DEBUG) {
                    printf("\ttoken.seq == aru, %d. Set token and local aru equal to largest packet_id sending this round.\n", token.seq);
                }
            } else if (lowered_aru == 1 && prev_sent_aru == token.aru) {
                if(token.aru != aru) {  // If our aru has increased 
                    if (DEBUG) {
                        printf("\tlowered aru previous round, token aru remained same, local aru is larger, set token.aru %d to local aru %d\n", token.aru, aru);
                    }
                    token.aru = aru;    // Update aru
                    //lowered_aru = 0; WE DON'T DO THIS BECAUSE WE MIGHT NEED TO KEEP RAISING IT
                } // (Implicit else) keep lowered_aru set.
            } /*else if (token.aru > prev_sent_aru && aru > token.aru) {  // If somebody else raised the aru, but local aru
                token.aru = aru;                                        // is still higher, set token aru to local aru
                lowered_aru = 0;
            }*/
            else {
                if (DEBUG) {
                    printf("\tdo nothing with token.aru %d\n", token.aru);
                }
                lowered_aru = 0;        // Do nothing, clear lowered flag
            }
            prev_aru = tmp_aru;
            prev_sent_aru = token.aru;
           
            /* Remember this token's seq number */
            prev_recvd_seq = token.seq;
            /* Set seq number to id of the last packet that will be sent */
            token.seq = packet_id;
            if (DEBUG) {
                printf("\ttoken to be sent has seq %d and aru %d, prev_recvd_seq is now %d \n", token.seq, token.aru, prev_recvd_seq);
            }            
  
            /* Set done field if finished sending packets */
            if (num_packets_sent >= num_packets && token.type == 2) {
                /* If bursting will send last packet, set appropriate bit in 
                 * done field.*/
                if (DEBUG) {
                    printf("old done: %d. or with %d. results: %d\n", token.done, (1 << (machine_id-1)), token.done | (1 << (machine_id-1)));
                }
                token.done = (token.done | (1 << (machine_id-1)));
            }
            
            /* Send token using appropriate socket */
            if (send_addr_ucast.sin_addr.s_addr == 0) {
                /* Multicast Token */
                if(DEBUG == 1) {
                    printf("multicast token\n");
                }
                for (int x = 0; x < TOKEN_BURST; x++) {
                    sendto(ss, (char *)&token, token_size, 0, 
                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                }
            } else {
                if(DEBUG == 1) {
                    int send_ip = send_addr_ucast.sin_addr.s_addr;
                    printf("unicast token with type %d, seq %d, tok_id%d\n",token.type, token.seq, token.tok_id);
                    printf("Target IP address is: %d.%d.%d.%d\n", (send_ip & 0xff000000) >>24,
                          (send_ip & 0x00ff0000)>>16,
                          (send_ip & 0x0000ff00)>>8,
                          (send_ip & 0x000000ff) );

                }
                /* Unicast Token */
                for (int x = 0; x < TOKEN_BURST; x++) {
                    sendto(uss, (char *)&token, token_size, 0, 
                      (struct sockaddr *)&send_addr_ucast, sizeof(send_addr_ucast));
                }
            }

            /* Send rest (second half) of messages */
            while (burst_count < packets_to_burst_itr) {
                /* Multicast Message */  
                sendto(ss, (char *)packets_to_burst[burst_count], sizeof(Message),
                       0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                burst_count++;
            }
            
            /* Deliver (write) packets received by all */
            if (tmp_prev_aru <= prev_aru) {
                if(DEBUG == 1) {
                    printf("Attempting to deliver packets up to & including %d\n", tmp_prev_aru);
                }                   
                while (window[start_of_window % WINDOW_SIZE].type != -1 
                    && window[start_of_window % WINDOW_SIZE].packet_id <= tmp_prev_aru) {
                    if(DEBUG == 1) {
                         printf("start_of_window: %d\n", start_of_window);
                         printf("packet_id: %d\n", window[start_of_window % WINDOW_SIZE].packet_id);
                         printf("tmp_prev_aru: %d\n", tmp_prev_aru);
                    }
                    /* Deliver window[start_of_window] */
                    Message *tmp_msg = &(window[start_of_window % WINDOW_SIZE]);
                    fprintf(fw, "%2d, %8d, %8d\n", tmp_msg->machine, 
                        tmp_msg->packet_id, tmp_msg->rand);
                    window[start_of_window % WINDOW_SIZE].type = -1;
                    window[start_of_window % WINDOW_SIZE].packet_id = INT_MAX;
                    start_of_window++;
                }
                if (DEBUG) {
                    printf("token.done: %d\n", token.done);
                }
                if (tmp_prev_aru == token.seq && prev_aru == token.seq
                    && pow(2.0, (double)num_machines) - 1 == token.done) {
                    /* If we believe everyone will deliver all messages, 
                     * and everyone is done sending, burst last token */
                    burst_count = 0;
                    if (DEBUG) {
                        printf("\t\t BURST END\n");
                    }
                    while (burst_count < 12) {   
                         /* Multicast Message */  
                         sendto(uss, (char *)&token, token_size,
                            0, (struct sockaddr *)&send_addr_ucast, sizeof(send_addr_ucast) );
                         burst_count++;
                         if (fw != NULL) {
                             fclose(fw);
                             fw = NULL;
                             gettimeofday(&end_time, 0);
                             int total_time =
                                (end_time.tv_sec*1e6 + end_time.tv_usec)
                                - (start_time.tv_sec*1e6 + start_time.tv_usec);
                             printf("total time %d\n", total_time);
                             return 0;
                         }
                    }
                }
                if (DEBUG) {
                    printf("exit attempt to deliver\n");
                }
            }
            
            /* Set has_token to zero because sent out token*/
            has_token = 0;
            /* We now wait for the ack */
            waiting_for_token_ack = 1;
        }
        /* Select: receive or timeout. */
        temp_mask = mask;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                /* Received multicasted packet. Can be message or StartToken */
                /* Check type of packet. If is token, set has_token to 1.*/
                bytes = recv_dbg(sr, mess_buf, MAX_PACKET_SIZE, 0);
                if (bytes > 0) {
                    if (DEBUG) {
                        printf("received multicast packet of type %d\n", ((Packet *)mess_buf)->type);
                        if (((Packet *)mess_buf)->type == 1) {
                            printf("Token id: %d\n", ((Token *)mess_buf)->tok_id);
                        }
                    }
                    if (((Packet *)mess_buf)->type == 1 
                         && ((Token *)mess_buf)->recv == machine_id
                         && token.tok_id < ((Token *)mess_buf)->tok_id) {
                         if(DEBUG == 1) {
                             printf("received token\n");
                         }
                         /* Received packet is a<token */
                         /* Use token if we have not received this token before. */
                         /* Copy received token into local placeholder for token. */
                         memcpy(&token, mess_buf, bytes);
                         has_token = 1;
                     } else if (((Packet *)mess_buf)->type == 3) {
                         /* Check if is implicit token ack */
                         Message *tmp_msg = (Message *)&mess_buf;
                         if(DEBUG == 1) {
                             printf("received message of id %d\n", tmp_msg->packet_id);
                         }
                         if (tmp_msg->packet_id > packet_id) {
                            if (DEBUG) {
                                printf("received implicit ack\n");
                            }   
                            waiting_for_token_ack = 0;
                         }
                         if (tmp_msg->packet_id >= start_of_window) {
                             window[tmp_msg->packet_id % WINDOW_SIZE].type = tmp_msg->type;
                             window[tmp_msg->packet_id % WINDOW_SIZE].packet_id = tmp_msg->packet_id;
                             window[tmp_msg->packet_id % WINDOW_SIZE].machine = tmp_msg->machine;
                             window[tmp_msg->packet_id % WINDOW_SIZE].rand = tmp_msg->rand;
                             /* update aru */
                             /* While we have received the next packet_id above the current aru
                              * increment the aru (set to that packet_id). */
                             while (window[(aru+1) % WINDOW_SIZE].type != -1 
                                 && window[(aru+1) % WINDOW_SIZE].packet_id == aru+1) {
                                 aru++;
                             }
                        }
                    }
                }
            } else if ( FD_ISSET( usr, &temp_mask) ) {
                /* Is unicasted packet */
                bytes = recv_dbg(usr, mess_buf, MAX_PACKET_SIZE, 0);
                if (bytes > 0) {
                    if (DEBUG) {
                        printf("received unicast packet of type %d\n", ((Packet *)mess_buf)->type);
                        if ((((Packet *)mess_buf)->type == 1) || (((Packet *)mess_buf)->type == 2)) {
                            printf("Token id: %d\n", ((Token *)mess_buf)->tok_id);
                        }
                    }               
                    if ((((Packet *)mess_buf)->type == 1 
                        || ((Packet *)mess_buf)->type == 2)
                        && token.tok_id < ((Token *)mess_buf)->tok_id) {
                        if(DEBUG == 1) {
                             printf("received token with seq %d\n", ((Token *)mess_buf)->seq);
                        }
                        /* Received packet is a token */
                        /* Use token if we have not received this token before. */
                        /* Copy received token into local placeholder for token. */
                        memcpy(&token, mess_buf, bytes);
                        has_token = 1;
                    } else if (((Packet *)mess_buf)->type == 4) {
                        /* This is an ack packet. We are no longer waiting for it. */
                        if (DEBUG) {
                            printf("received explicit ack\n");
                        }
                        waiting_for_token_ack = 0;
                    }
                }
            } 
        } else  {
            /* TIMEOUT*/
            if (DEBUG == 1) {
                printf("Timeout\n");
            }
            /* Check if waiting_for_token_ack. If so, resend token*/
            if (waiting_for_token_ack == 1) {
                /* Resend token */
                if (DEBUG)
                    printf("Retransmitting token with ID %d\n", token.tok_id);
                /* Send token using appropriate socket */
                if (send_addr_ucast.sin_addr.s_addr == 0) {
                    /* Multicast Token */  
                    sendto(ss, (char *)&token, token_size, 0, 
                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                } else {
                    /* Unicast Token */
                    sendto(uss, (char *)&token, token_size, 0, 
                    (struct sockaddr *)&send_addr_ucast, sizeof(send_addr_ucast));
                }
            }
        } 
    }
    
    if (fw != NULL) {
        fclose(fw);
        fw = NULL;
    }

    return 0;
}

/*
 *  Initialize the unicast send sockets
 */
struct sockaddr_in initUnicastSend(int next_ip)
{
    struct sockaddr_in send_addr;

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = next_ip;
    send_addr.sin_port = htons(MCAST_PORT + 1);
    
    return send_addr;
}
