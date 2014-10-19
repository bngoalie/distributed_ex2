#include "net_include.h"
#include "recv_dbg.h"
#include <math.h>
#include <limits.h>

struct sockaddr_in initUnicastSend(int);

int main(int argc, char **argv) {
    /*
     *  Declarations
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
     *  Initial program setup
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

    /* Bind to receiving socket */
    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    /* Join multicast group */
    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) 
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    /* Set up sending socket */
    ss = socket(AF_INET, SOCK_DGRAM, 0);
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

    /* Set up initial token values */
    int has_token = 0;
    Token token;
    if (machine_id == 1) {
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
     *  Wait for the start_mcast packet
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

    // --------------------------------------------------------------------------------------------//

    /*
     *  Main loop
     */ 
    for (;;) {
        if (DEBUG) {
            printf("Entering main loop (infinite):\n");
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_USEC; // set timeout
        
        /* If we've received a new token, process it */
        if (has_token == 1) {
            if(DEBUG == 1) {
                printf("At round %d\n", (++round));
                printf("\tReceived token with type %d, seq %d, aru %d\n", token.type, token.seq, token.aru);
                if (token.type == 1) {
                   
                    printf("Token has IP addresses: ");
                    for (int idx = 0; idx < num_machines; idx++) {
                        printf("%d ", ((StartToken *)&token)->ip_array[idx]);
                    }
                    printf("\nmachine to ack is at index %d and has ip address %d\n", 
                        (machine_id-2+num_machines)%num_machines, 
                        ((StartToken *)&token)->ip_array[(machine_id - 2 + num_machines) % num_machines]);
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
               
            /* Check rtr for packets machine is holding. If have packet, add 
             * to array of packets to send in burst. */
            Message *packets_to_burst[BURST_MSG + MAX_PACKET_SIZE - 24];    // Burst size plus max rtr 
            int tmp_prev_aru = prev_sent_aru;
            int packets_to_burst_itr = 0;
            int window_itr = start_of_window;
            int rtr_itr = 0;
            int rtr_size = (token.type == 1 ? bytes - 64 : bytes - 24)/sizeof(int);
            int rtr_max = (token.type == 1 ? MAX_PACKET_SIZE - 64 : MAX_PACKET_SIZE - 24);
            int new_rtr[MAX_PACKET_SIZE - 24];
            int new_rtr_itr = 0;
            int *token_rtr = token.rtr;
            if (token.type == 1) {
                token_rtr = ((StartToken *)&token)->rtr;
            }
            
            /*
             * Iterate through the window and the retransmission request array to create 
             * a new retransmission request array and an array of messages to burst:
             */ 
            if(DEBUG) {
            printf("rtr size: %d, window_itr start value: %d, prev_recvd_seq: %d\n", rtr_size, window_itr, prev_recvd_seq);
            }
            while (rtr_itr < rtr_size && window_itr <= prev_recvd_seq) {
                if (window[window_itr % WINDOW_SIZE].type == -1) {  // expected packet is missing from window
                    if (new_rtr_itr < rtr_max) {       // have not yet exceeded rtr limit
                        new_rtr[new_rtr_itr] = window_itr;          // add retransmission request
                        if (DEBUG) {
                            printf("window missing id: add to new_rtr id %d\n", new_rtr[new_rtr_itr]);
                        }
                        new_rtr_itr++;  // increment new rtr iterator
                    }
                    if (token_rtr[rtr_itr] == window_itr) { // packet was also in rtr list
                        rtr_itr++;                          // increment old rtr iterator
                    }
                    window_itr++;   // increment window iterator even if not added to new rtr
                } else if (token_rtr[rtr_itr] < window[window_itr % WINDOW_SIZE].packet_id) { // rtr id is lower than window id
                    if (new_rtr_itr < rtr_max) {      // have not yet exceeded rtr limit
                        new_rtr[new_rtr_itr] = token_rtr[rtr_itr]; // add rtr to new rtr array (we don't have it)
                        if (DEBUG) {
                            printf("add to new_rtr id %d\n", new_rtr[new_rtr_itr]);
                        }
                        new_rtr_itr++;  // increment new rtr iterator
                    }
                    rtr_itr++;  // increment old rtr iterator even if not added to new rtr
                } else if (token_rtr[rtr_itr] == window[window_itr % WINDOW_SIZE].packet_id) { // rtr packet is in window
                    if (DEBUG) {
                        printf("Adding to packets_to_burst retransmission of packet id %d\n", 
                            window[window_itr % WINDOW_SIZE].packet_id);
                    }
                    packets_to_burst[packets_to_burst_itr] = &(window[window_itr % WINDOW_SIZE]); // add to burst array
                    packets_to_burst_itr++; // increment burst iterator
                    
                    window_itr++; // increment iterators
                    rtr_itr++;
                } else {          // rtr id is ahead of window
                    window_itr++; // increment window iterator only
                }
            }
            if (DEBUG) {
                printf("done with combined while loop, rtr_itr: %d, new_rtr_itr: %d, window_itr: %d\n", 
                    rtr_itr, new_rtr_itr, window_itr);
            }
            
            /* Iterate through remaining retransmission requests (if any) */
            while (rtr_itr < rtr_size && new_rtr_itr < rtr_max) {
                new_rtr[new_rtr_itr] = token_rtr[rtr_itr];
                if (DEBUG) {
                    printf("Adding to new_rtr (id %d)\n", new_rtr[new_rtr_itr]);
                }
                new_rtr_itr++;
                rtr_itr++;
            }
            if (DEBUG) {
                printf("window_itr: %d, prev_recvd_seq: %d\n", window_itr, prev_recvd_seq);
            }

            /* Iterate through remaining window packets (if any) */
            while (window_itr <= prev_recvd_seq && new_rtr_itr < rtr_max) {
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
            
            /* Pepare new packets to burst  */
            int rt_burst_count = packets_to_burst_itr;
            packet_id = token.seq;
            while ((packets_to_burst_itr - rt_burst_count) < BURST_MSG 
                    && num_packets_sent+1 <= num_packets
                    && window[(packet_id+1) % WINDOW_SIZE].type == -1) {
                packet_id++;
                if (packet_id == aru + 1){
                    aru++;
                }
                /* NOTE: this is not a truly random distribution between 1 and 1 million.
                 * However, it should be sufficient for our needs. */
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

            /* Burst the first group of packets (pre-token) */
            int burst_count = 0;
            while (burst_count < packets_to_burst_itr/4) {
                /* Multicast Message */  
                sendto(ss, (char *)packets_to_burst[burst_count], sizeof(Message),
                       0, (struct sockaddr *)&send_addr, sizeof(send_addr) );
                burst_count++;
            } 

            /* Establish a target unicast IP if this process has not yet done so */
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
            
            /* Add this machines IP to the token if this process has not yet done so
             * (Note this only applies for the StartToken during round 1 */
            if (token.type == 1 
                && ((StartToken *)&token)->ip_array[machine_id-1] == 0) {
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
                if (DEBUG) {
                    printf("set this machines index in token ip_array to %d\n", my_ip);
                }
                ((StartToken *)&token)->ip_array[machine_id-1] = my_ip;
            } else if (machine_id == num_machines - 1) {
                // Implicitly determined that already received start packet in a
                // prior round by not satsifying condition in if block above.
                token.type = 2;
            }
            
            /* Increment token id */
            token.tok_id++;

            /* Set initial token size (without rtr)*/
            token_size = token.type == 1 ? 64 : 24;
            
            /* Set new token's rtr to precomputed new_rtr */
            if (token.type == 1) {
                memcpy(((StartToken *)&token)->rtr, new_rtr, new_rtr_itr * 4);
            } else {
                memcpy(token.rtr, new_rtr, new_rtr_itr * 4);
            }
            /* Adjust size for new rtr */
            token_size += new_rtr_itr * 4;
            
            /* Set intended receiver. */
            token.recv = (machine_id%10) + 1;
                   
            if (DEBUG) { // debug: display information on token to be sent
                printf("\ttoken.aru: %d, local aru: %d, prev_aru: %d, prev_sent_aru: %d\n",
                     token.aru, aru, prev_aru, prev_sent_aru);
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
                /*aru = packet_id;*/
                lowered_aru = 0;
                if (DEBUG) {
                    printf("\ttoken.seq == aru, %d. Set token / local aru to largest packet_id in next burst.\n", token.seq);
                }
            } else if (lowered_aru == 1 && prev_sent_aru == token.aru) {
                if(token.aru != aru) {  // If our aru has increased 
                    if (DEBUG) {
                        printf("\tlowered aru previously, token aru unchanged, update token.aru %d to local aru %d\n", token.aru, aru);
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
                    printf("\tDo nothing with token.aru %d\n", token.aru);
                }
                lowered_aru = 0;    // Do nothing, clear lowered flag
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
                if(DEBUG == 1) { // debug: print info on token and unicast target
                    int send_ip = send_addr_ucast.sin_addr.s_addr;
                    printf("unicast token with type %d, seq %d, tok_id%d\n",token.type, token.seq, token.tok_id);
                    printf("Target IP address is: %d.%d.%d.%d\n", (send_ip & 0xff000000) >>24,
                          (send_ip & 0x00ff0000)>>16,
                          (send_ip & 0x0000ff00)>>8,
                          (send_ip & 0x000000ff) );
                    if (token.type == 1) {
                        printf("token ip_array has value %d for this machine\n", ((StartToken *)&token)->ip_array[machine_id-1]);
                    }

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

                if(DEBUG == 1) { // debug: print info on packets being delivered
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

                if (DEBUG) { // debug: print done bitmask state
                    printf("token.done: %d\n", token.done);
                }

                /* If we believe everyone will deliver all messages, 
                 * and everyone is done sending, burst last token */
                if (tmp_prev_aru == token.seq && prev_aru == token.seq
                    && pow(2.0, (double)num_machines) - 1 == token.done) {

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
                            fclose(fw); // TODO: Why do we close the file writer twice? (see below)
                            fw = NULL;
                            gettimeofday(&end_time, 0);
                            int total_time =
                                (end_time.tv_sec*1e6 + end_time.tv_usec)
                                - (start_time.tv_sec*1e6 + start_time.tv_usec);
                            printf("Total measured time: %d ms\n", (int)(total_time/1e3));
                            printf("Throughput: %f mbps\n\n", (token.seq + 1) * 1216 * 8.0 / total_time);
                            return 0;
                        }
                    }
                }
                if (DEBUG) {
                    printf("Done with delivery attempt\n");
                }
            }
            
            /* Set has_token to zero because sent out token */
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
    
    /* Close the file writer */
    if (fw != NULL) {
        fclose(fw);
        fw = NULL;
    }

    return 0;
}

/*
 *  Initialize the unicast send sockets TODO: Might as well put this in the main function
 */
struct sockaddr_in initUnicastSend(int next_ip)
{
    struct sockaddr_in send_addr;

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = next_ip;
    send_addr.sin_port = htons(MCAST_PORT + 1);
    
    return send_addr;
}
