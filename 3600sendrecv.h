/*
 * CS3600, Spring 2013
 * Project 4 Starter Code
 * (c) 2013 Alan Mislove
 *
 */

#ifndef __3600SENDRECV_H__
#define __3600SENDRECV_H__

#include <stdio.h>
#include <stdarg.h>

// window size
#define WS 2000
// sender timeout seconds
#define SEND_SEC 3
// sender timeout microseconds
#define SEND_USEC 00000
// the # of duplicate ACKs before fast retransmit
#define MAX_DUPS 5
// scale to change sending rate
#define RATE_SCALE 0.33
// multiplicative decrease scale on timeout
#define MULT_DEC 1.0
// scale to use for additive increase
#define ADD_IN 1.0

typedef struct header_t {
  unsigned int magic:14;
  unsigned int ack:1;
  unsigned int eof:1;
  unsigned short length;
  unsigned short sequence;
  unsigned int time;
} header;

unsigned int MAGIC;

void dump_packet(unsigned char *data, int size);
header *make_header(short sequence, int length, int eof, int ack, unsigned int time);
header *get_header(void *data);
char *get_data(void *data);
unsigned char get_checksum(char *datum, char *data, int length);
char *timestamp();
void mylog(char *fmt, ...);

#endif

