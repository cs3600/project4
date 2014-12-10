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

/*
 * TCP Receiver.
 */
int main() {
    // first, open a UDP socket  
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // next, construct the local port
    struct sockaddr_in out;
    out.sin_family = AF_INET;
    out.sin_port = htons(0);
    out.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *) &out, sizeof(out))) {
        perror("bind");
        exit(1);
    }

    struct sockaddr_in tmp;
    int len = sizeof(tmp);
    if (getsockname(sock, (struct sockaddr *) &tmp, (socklen_t *) &len)) {
        perror("getsockname");
        exit(1);
    }

    mylog("[bound] %d\n", ntohs(tmp.sin_port));

    // wait for incoming packets
    struct sockaddr_in in;
    socklen_t in_len = sizeof(in);

    // construct the socket set
    fd_set socks;

    // construct the timeout
    struct timeval t;
    t.tv_sec = 30;
    t.tv_usec = 0;

    // our receive buffer
    int buf_len = 1500;
    void *buf = malloc(buf_len); 

    // create a buffer to store packets
    char *packet_buf = (char *) calloc(WS * buf_len, sizeof(char));
    // buffer to store packet lengths
    unsigned int *len_buf = (unsigned int*) calloc(WS, sizeof(unsigned int)); 
    // current packet
    unsigned int current_packet = 1;
    // wait to receive, or for a timeout
    while (1) {
        FD_ZERO(&socks);
        FD_SET(sock, &socks);

        if (select(sock + 1, &socks, NULL, NULL, &t)) {
            int received;
            if ((received = recvfrom(sock, buf, buf_len, 0,
            				(struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
                perror("recvfrom");
                free(buf);
                free(packet_buf);
                free(len_buf);
                exit(1);
            }

						// get the header and data from the packet
            header *myheader = get_header(buf);
            char *data = get_data(buf);
            // expected checksum
            unsigned char expected_checksum = get_checksum((char *)buf,
            		get_data(buf), myheader->length); 
            unsigned char actual_checksum = *((unsigned char *) (data - 1));
            // Packet is for me.
            if (myheader->magic == MAGIC) {
            	  // corrupted packet
                if (expected_checksum != actual_checksum) {
                    mylog("[recv corrupted packet]");
                    continue;
                }

				        // sequence packet # matches our current packet index
                if ((int)myheader->sequence == current_packet) {
                    // write data
                    write(1, data, myheader->length);
                    // move onto the next packet
                    current_packet++;
                    // window index
                    size_t pbi = current_packet % WS;
                    // we've got data to write
                    while(len_buf[pbi] > 0) {
                        write(1, &packet_buf[pbi * buf_len], len_buf[pbi]);
                        // mark no more data there
                        len_buf[pbi] = 0;
                        // move onto next packet; update window index 
                        current_packet++;
                        pbi = current_packet % WS;
                    }
                }
                // add new packet to the received packet buffer
                else if ((int)myheader->sequence > current_packet) {
                    // window index
                    int pbi = (int) myheader->sequence % WS;
                    // don't overwrite if there is data there already
                    if (len_buf[pbi] == 0) {
                        len_buf[pbi] = myheader->length;
                        memcpy(&packet_buf[pbi*buf_len], data, myheader->length);
                    }
                }

                mylog("[recv data] %d (%d) %s\n", (int)myheader->sequence, myheader->length, "ACCEPTED (in-order)");
                mylog("[send ack] %d\n", current_packet-1);

                // create packet with header & checksum
                void *packet = malloc(sizeof(header) + sizeof(char));
                // create a header
                header *responseheader = make_header((short)current_packet - 1,
                		0, myheader->eof, 1, ntohl(myheader->time));
                memcpy(packet, responseheader, sizeof(header));
                // get checksum and add after header
                unsigned char *checksum = malloc(sizeof(char));
                *checksum = get_checksum((char *) responseheader, NULL, 0);
                memcpy(((char *) packet) + sizeof(header),
                		(char *)checksum, sizeof(unsigned char));

                // free bufs
                free(responseheader);
                free(checksum);
               
                // send ACK
                if (sendto(sock, packet, sizeof(header) + sizeof(char), 0,
                			  (struct sockaddr *) &in, (socklen_t) sizeof(in)) < 0) {
                    perror("sendto");
                    // free bufs
                    free(buf);
                    free(packet_buf);
                    free(len_buf);
                    free(packet);
                    exit(1);
                }
                
                // eof - we're done
                if (myheader->eof) {
                    mylog("[recv eof]\n");
                    mylog("[completed]\n");
                    // free bufs
                    free(buf);
                    free(packet_buf);
                    free(len_buf);
                    free(packet);
                    exit(0);
                }

                // free buf
                free(packet);
            } else {
                mylog("[recv corrupted packet]\n");
            }
        } else {
            mylog("[error] timeout occurred\n");
            // free bufs
            free(buf);
            free(packet_buf);
            free(len_buf);
            exit(1);
        }
    }

    // free bufs
    free(buf);
    free(packet_buf);
    free(len_buf);
    return 0;
}
