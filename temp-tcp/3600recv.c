#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "3600sendrecv.h"

/*
 * STARTER CODE FOR RECIEVER.
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
  void* buf = malloc(buf_len);

  // create a buffer to store packets
  char *packet_buf = (char *)
  	calloc(WINDOW_SIZE * buf_len, sizeof(char));
  // buffer to store packet lengths
  size_t *packet_len_buf = (size_t *)
  	calloc(WINDOW_SIZE, sizeof(size_t));
  // TODO make a struct that consists of a char & size_t?
  // so we can have just one buffer?

  // current packet 
  size_t current_packet = 1;

  // wait to receive, or for a timeout
  while (1) {
    FD_ZERO(&socks);
    FD_SET(sock, &socks);

    if (select(sock + 1, &socks, NULL, NULL, &t)) {
      int received;
      if ((received = recvfrom(sock, buf, buf_len, 0, (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
        perror("recvfrom");
        // free buffers
        free(buf);
        free(packet_buf);
        free(packet_len_buf);
        exit(1);
      }

//      dump_packet(buf, received);

			// get the header and data from the packet
      header *myheader = get_header(buf);
      char *data = get_data(buf);
  
      // packet is for me.
      if (myheader->magic == MAGIC) {

				// sequence packet # matches our current packet index
      	if (myheader->sequence == current_packet) {
      		// write data
          write(1, data, myheader->length);
          // move onto the next packet
          // modded packet buffer index
          // FIXME should current_packet be modded?
          current_packet++;
          size_t pbi = current_packet % WINDOW_SIZE;
          
          // we've got some data to write
          while(packet_len_buf[pbi] > 0) {
          	// write out data in packet
          	write(1, &packet_buf[pbi * buf_len], packet_len_buf[pbi]);
          	// no more data
          	packet_len_buf[pbi] = 0;
            // move onto the next packet
            // modded packet buffer index
            // FIXME should current_packet be modded?
            current_packet++;
            pbi = current_packet % WINDOW_SIZE;
					}
				}
				// we've already seen this packet
				else if (myheader->sequence < current_packet) {
					continue;
				}
				// sequence packet # past our current packet index
				// Add it to our buffer; handle reordering
				// TODO is this right?
				else {
					// modded index
					int pbi = myheader->sequence % WINDOW_SIZE;
					// don't overwrite if full
					if (packet_len_buf[pbi] == 0) {
					  packet_len_buf[pbi] = myheader->length;
					  memcpy(&packet_buf[pbi * buf_len], data, myheader->length);
					}
				}

        mylog("[recv data] %d (%d) %s\n", myheader->sequence, myheader->length, "ACCEPTED (in-order)");
        mylog("[send ack] %d\n", current_packet - 1);

				// create and send acknowledgement back to sender
        header *responseheader = make_header(current_packet - 1, 0, myheader->eof, 1);
        if (sendto(sock, responseheader, sizeof(header), 0, (struct sockaddr *) &in, (socklen_t) sizeof(in)) < 0) {
          perror("sendto");
          // free bufs
          free(buf);
          free(packet_buf);
          free(packet_len_buf);
          exit(1);
        }
        if (myheader->eof) {
          mylog("[recv eof]\n");
          mylog("[completed]\n");
          exit(0);
        }
      } else {
        mylog("[recv corrupted packet]\n");
      }
    } else {
      mylog("[error] timeout occurred\n");
      // free bufs
      free(buf);
      free(packet_buf);
      free(packet_len_buf);
      exit(1);
    }
  }

	// free bufs
  free(buf);
  free(packet_buf);
  free(packet_len_buf);
  return 0;
}
