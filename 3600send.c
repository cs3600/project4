#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "3600sendrecv.h"

static int DATA_SIZE = 1460;

// # of packets created 
// TODO doesn't wrap around ... 
unsigned short created = 1;
// flag to check for more packets to be created
char create_more_packets = 1;
// packets acknowledged
unsigned short acked = 0;
// # duplicate ACKS seen
int dups = 0;
// did we do fast retransmit?
int fast_retransmit = 0;
// are we in slow start?
int slow_start = 1;
// initial window size (congestion)
int cwnd = 2000;
// how much to decrease window size by on timeout
// TODO MIAD might not be implemented properly
int add_increase = 1;
// # acks counted for congestion avoidance
int ca_acks = 0;
// buffer for packets to send
void *packet_buf[WS] = {0};
// buffer for packet lengths of packets to send
int len_buf[WS] = {0};
// start time 
struct timeval start;
// current time
struct timeval current;
// used to track difference in current and start times
unsigned int elapsed = 0;
// timeout configuration values as global variables
unsigned int timeout_sec = SEND_SEC;
unsigned int timeout_usec = SEND_USEC;

void usage() {
    printf("Usage: 3600send host:port\n");
    exit(1);
}

/**
 * Reads the next block of data from stdin
 */
int get_next_data(char *data, int size) {
    return read(0, data, size);
}

/**
 * Builds and returns the next packet, or NULL
 * if no more data is available.
 */
void *get_next_packet(int sequence, int *len, unsigned int time) {
	  // get data and data length
    char *data = malloc(DATA_SIZE);
    int data_len = get_next_data(data, DATA_SIZE);
		// no data
    if (data_len == 0) {
        free(data);
        return NULL;
    }
	  // create a header consistings of sequence #, packet length,
	  // packet data (used for checksum), and time
    header *myheader = make_header((short)sequence, data_len, 0, 0, time);
    void *packet = malloc(sizeof(header) + sizeof(char) + data_len);
    memcpy(packet, myheader, sizeof(header));
    // create a checksum
    unsigned char *checksum = malloc(sizeof(unsigned char));
    *checksum = get_checksum((char *)myheader, data, data_len);
    // add after the header
    memcpy(((char *) packet) + sizeof(header),
    		(char *) checksum, sizeof(unsigned char));
    // append the data to the packet
    memcpy(((char *) packet) + sizeof(header) + sizeof(char),
    		data, data_len);
    // free unused buffers
    free(data);
    free(myheader);
    free(checksum);
    // update the packet size
    *len = sizeof(header) + sizeof(char) + data_len;
    // return completed packet
    return packet;
}


/**
 * Update the elapsed time.
 */
void update_elapsed() {
    gettimeofday(&current, NULL);
    elapsed = (unsigned int) (current.tv_sec * 1000000 + current.tv_usec) - 
    	      (start.tv_sec * 100000 + start.tv_usec);
}

/**
 * Update timeouts according to round trip times.
 * Effectively adjusts the sending rate.
 */
void update_timeouts_rrt(unsigned int time) {
	  // net to host time
    time = ntohl(time);
    // update current and elapsed time
    update_elapsed();
    // determine round trip time
    unsigned int rtt = elapsed - time;
    // update timeouts 
    timeout_sec = (timeout_sec * RATE_SCALE) + 
    	      (1 - RATE_SCALE) * (rtt / 1000000);
    timeout_usec =  (timeout_usec * RATE_SCALE) +
    	      ((unsigned int) ((1 - RATE_SCALE) * (rtt))) % 1000000;
}

// TODO resolve the send packet / final packet code
/**
 * Builds and sends the final EOF packet. Free all sent packets.
 */
void send_final_packet(int sock, struct sockaddr_in out) {
    // update current and elapsed time
    update_elapsed();
    // header w/ EOF
    header *myheader = make_header(created, 0, 1, 0, elapsed);
    void *packet = malloc(sizeof(header) + sizeof(char));
    memcpy(packet, myheader, sizeof(header));
    // create a checksum
    unsigned char *checksum = malloc(sizeof(char));
    *checksum = get_checksum((char *) myheader, NULL, 0);
    // add after the header
    memcpy(((char *) packet) +sizeof(header), (char *)checksum, sizeof(unsigned char));
    // send packet
    mylog("[send eof]\n");
    if (sendto(sock, packet, sizeof(header)+sizeof(char), 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
        perror("sendto");
        exit(1);
    }
    // free buffers and sent packets
    free(myheader);
    free(checksum);
    free(packet);
    for (size_t i = 0; i < WS; i++) {
        free(packet_buf[i]);
    }
}

/**
 * Initialize the send packet buffer and timeouts.
 */
void init() {
    // initialized send buffer packets 
    for (size_t i = 0; i < WS; i++) {
        packet_buf[i] = calloc(1, 1500);
    }
    // get start and current times
    gettimeofday(&start, NULL);
    gettimeofday(&current, NULL);
}

/**
 * Create all the packets from the data.
 */
void create_packets() {
    // get packets to send
    while (create_more_packets && created - acked <= cwnd) {
        // update current and elapsed 
        update_elapsed();
        // window index
        size_t pbi = created % WS;
        // create next packet
        packet_buf[pbi] = 
                get_next_packet(created, &(len_buf[pbi]), elapsed); 
        // no more packets need to be created 
        if (packet_buf[pbi] == NULL) {
            create_more_packets = 0;
            break;
        }
        // we created another packet to send
        created++;
    }
}

/**
 * Send unacknowledged packets in the send buffer.
 */
void send_unacked_packets(int sock, struct sockaddr_in out) {
    for (size_t i = acked + 1; i < created && i <= acked + cwnd; i++) {
        // window index
        size_t pbi = i % WS;
        // send the packet
        if (sendto(sock, packet_buf[pbi], len_buf[pbi], 0,
            		(struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
            perror("sendto");
            exit(1);
        }
        mylog("[send data] %d (%d)\n", i, len_buf[pbi] - sizeof(header));
        // fast retransmitted
        if (fast_retransmit) {
            // send twice to combat drops
            // TODO don't handle drops intelligently
            if (sendto(sock, packet_buf[pbi], len_buf[pbi], 0,
            	      (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
                perror("sendto");
                exit(1);
            }
            if (sendto(sock, packet_buf[pbi], len_buf[pbi], 0,
            		    (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
                perror("sendto");
                exit(1);
            }
            // congestion; wait to send rest
            break;
        }
    }
}

/**
 * TCP Sender.
 */
int main(int argc, char *argv[]) {
    // extract the host IP and port
    if ((argc != 2) || (strstr(argv[1], ":") == NULL)) {
        usage();
    }
    char *tmp = (char *) malloc(strlen(argv[1])+1);
    strcpy(tmp, argv[1]);
    char *ip_s = strtok(tmp, ":");
    char *port_s = strtok(NULL, ":");
    // first, open a UDP socket  
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    // next, construct the local port
    struct sockaddr_in out;
    out.sin_family = AF_INET;
    out.sin_port = htons(atoi(port_s));
    out.sin_addr.s_addr = inet_addr(ip_s);
    // socket for received packets
    struct sockaddr_in in;
    socklen_t in_len = sizeof(in);
    // construct the socket set
    fd_set socks;
    // construct the timeout
    struct timeval t;
    t.tv_sec = SEND_SEC;
    t.tv_usec = SEND_USEC;
    // init buffer and timeouts
    init();

		// get all the data
    while (1) {
    	  // create all the packets from data
    	  create_packets();

        // send un-ACKed packets in window
        send_unacked_packets(sock, out);
        
        // set sockets and timeouts for receiving ACKs
        FD_ZERO(&socks);
        FD_SET(sock, &socks);
        t.tv_sec = timeout_sec * MULT_DEC;
        t.tv_usec = timeout_usec * MULT_DEC;

        // keep receiving packets until timeout 
        while (select(sock + 1, &socks, NULL, NULL, &t)) {
            unsigned char buf[10000];
            int buf_len = sizeof(buf);
            int received;
            if ((received = recvfrom(sock, &buf, buf_len, 0,
            		    (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
                perror("recvfrom");
                exit(1);
            }
            
            // get header
            header *myheader = get_header(buf);
            // already fast retransmitted & still dup ACKS
            if (fast_retransmit && myheader->sequence == acked) {
            	  // update the timeouts with the round trip time
                update_timeouts_rrt(myheader->time);
                // skip this packet
                continue;
            }

            // get calculated checksum
            unsigned char expected_checksum =
            	  get_checksum((char *)buf, NULL, 0); 
            // get actual checksum
            unsigned char actual_checksum = 
            	*((unsigned char *) (buf + sizeof(header)));

            // 'likely' uncorrupted packet meant for us; packet is ACK
            if ((myheader->magic == MAGIC) 
            		    && expected_checksum == actual_checksum 
            		    && (myheader->sequence >= acked) 
            		    && (myheader->ack == 1)) {
                mylog("[recv ack] %d\n", myheader->sequence);
                // duplicate ACK
                if (acked == myheader->sequence) {
                    dups++;
                }
                // new ACK
                else {
                	  // slow start, add to cwnd for each ACK
                    if (slow_start) {
                        cwnd++;
                    }
                    // congestion avoidance, count ACKs
                    else {
                        ca_acks++;
                    }
                    // we got the new ACK; we are allowed to
                    // fast retransmit for the next drop
                    fast_retransmit = 0;
                    // reset the duplicate ACK count
                    dups = 0;
                }
                // more than 3 duplicate ACKS; do fast retransmit
                if (dups >= MAX_DUPS) {
                    fast_retransmit = 1;
                    // we don't care about the dups now that we did FR
                    dups = 0;
                    // we are now in congestion avoidance
                    slow_start = 0;
                    // additive increase
                    add_increase++;
                    // update timeouts via round trip time
                    update_timeouts_rrt(myheader->time);
                    break;
                }
                // update last acked sequence #
                acked = myheader->sequence;
                // update timeouts via round trip time
                update_timeouts_rrt(myheader->time);
                // we got all the packets back
                if (acked + 1 == created) {
                    break;
                }
            }
            // got a corrupted ACK... 
            else {
                mylog("[recv corrupted ack] %x %d\n", MAGIC, created);
            }

        } 
        // update congestion window
        cwnd += (int) (ADD_IN * (float) ca_acks / (float) cwnd);
        // reset count of ACKS for congestion avoidance
        ca_acks = 0;
        // timeout - move to congestion avoidance
        if (t.tv_sec <= 0 && t.tv_usec <= 0) {
            slow_start = 0;
            // decrease the window size
            if (cwnd > 1) {
                cwnd = cwnd * add_increase / (add_increase + 1);
            }
            // double timeouts.
            timeout_sec = (timeout_usec * 2) / 1000000;
            timeout_usec = (timeout_usec * 2) % 1000000;
        } 
        // no more data to send, and all packets created
        if (!create_more_packets && acked+1 == created) { 
            break;
        }
    }
    // send the final packet
    send_final_packet(sock, out);

    mylog("[completed]\n");
    return 0;
}
