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

static int DATA_SIZE = 1460;

unsigned int sequence = 0;

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
void *get_next_packet(int sequence, int *len) {
	// get data and data length
  char *data = malloc(DATA_SIZE);
  int data_len = get_next_data(data, DATA_SIZE);

	// no data
  if (data_len == 0) {
    free(data);
    return NULL;
  }

	// create a header consistings of sequence #, packet length,
	// TODO and some other crap
  header *myheader = make_header(sequence, data_len, 0, 0);
  void *packet = malloc(sizeof(header) + data_len);
  memcpy(packet, myheader, sizeof(header));
  memcpy(((char *) packet) + sizeof(header), data, data_len);

  free(data);
  free(myheader);

  *len = sizeof(header) + data_len;

  // return properly constructed packet
  return packet;
}

int send_next_packet(int sock, struct sockaddr_in out) {
	// TODO garbage code for funk
	return 0;
}

void send_final_packet(int sock, struct sockaddr_in out) {
  header *myheader = make_header(sequence+1, 0, 1, 0);
  mylog("[send eof]\n");

  if (sendto(sock, myheader, sizeof(header), 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }
}

/*
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
  t.tv_sec = S_T_S;
  t.tv_usec = S_T_MS;

	// packets acknowledged
  size_t acked = 0;
  // packets created
  size_t created = 1;
  // buffer for packets to send
  void *packet_buf[WINDOW_SIZE] = {0};
  // length of buffered packets
  int packet_len_buf[WINDOW_SIZE] = {0};
  // flag to indicate more packets to create
  unsigned char more_packets = 1;

  // initialize packet buffers
  for (size_t i = 0; i < WINDOW_SIZE; i++) {
  	packet_buf[i] = malloc(1500);
	}

  while (1) {
		// TODO make helper
  	// get packets to send
  	while (more_packets && ((created - acked) < WINDOW_SIZE)) {
  		// modded index
  		size_t pbi = created % WINDOW_SIZE;
  		// create next packet
  		packet_buf[pbi] = 
  			get_next_packet(created, &(packet_len_buf[pbi]));
  		// memcpy(&(packet_buf[pbi]), 
  		//   get_next_packet(created, &(packet_len_buf[pbi]));
  		//   FIXME

			// could not create packet
			if (packet_buf[pbi] == NULL) {
				more_packets = 0;
			}
			// created a new packet successfully
			else {
				created++;
			}
		}

		// send unacknowledged packets in buffer (window)
		// TODO move to helper send next packet
		for (size_t i = acked + 1; i < created; i++) {
			// modded index
			size_t pbi = i % WINDOW_SIZE;
			if (sendto(sock, packet_buf[pbi], packet_len_buf[pbi],
						0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
				perror("sendto");
				exit(1);
			}
			mylog("[send data] %d (%d)\n", i, packet_len_buf[pbi], - sizeof(header));
		}

		// received packets timeout
    FD_ZERO(&socks);
    FD_SET(sock, &socks);
    t.tv_sec = S_T_S;
    t.tv_usec = S_T_MS;

    // keep receiving packets every t seconds or timeout
    while (select(sock + 1, &socks, NULL, NULL, &t)) {
      unsigned char buf[10000];
      int buf_len = sizeof(buf);
      int received;
      if ((received = recvfrom(sock, &buf, buf_len, 0,
      				(struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
        perror("recvfrom");
        exit(1);
      }

      // get received header
      header *myheader = get_header(buf);

      // check if its the right ACK
      if ((myheader->magic == MAGIC) && (myheader->sequence >= acked) && (myheader->ack == 1)) {
        mylog("[recv ack] %d\n", myheader->sequence);
        acked = myheader->sequence;
      }
      else {
        mylog("[recv corrupted ack] %x %d\n", MAGIC, sequence);
      }
    }
    // FIXME handle timout
		//else {
    //mylog("[error] timeout occurred\n");
		//}
		//TODO is this right?
    if (!more_packets && acked == created) {
    	break;
    }
  }
  
  // TODO how to send final packet? robustly
  // what if not ACKED? 
  header *myheader = make_header(created, 0, 1, 0);
	mylog("[sent eof]\n");

	if (sendto(sock, myheader, sizeof(header), 0, (struct sockaddr *) &out,
				(socklen_t) sizeof(out)) < 0) {
		perror("sendto");
		exit(1);
	}

	mylog("[completed]\n");

  return 0;
}
