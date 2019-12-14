/* name: Carlos McNulty
 * netid: cmcnul3
 * github: mcnultyc 
 */

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "rdt.h"

#define MAX_BUF 1500

enum STATES{ WAIT_SYN_ACK, WAIT_ACK, WAIT_FIN}; 


long timeout(struct timespec before, struct timespec after, 
              long *est_msec, long *dev_msec){
  if(!est_msec || !dev_msec){ return 0; }
  /* calculate elapsed time (ms) */
  long time_msec = (after.tv_sec - before.tv_sec) * 1E3 
                  + (after.tv_nsec - before.tv_nsec) / 1E6;
  /* recalculate average and standard deviation */
  *est_msec = EST_RTT(*est_msec, time_msec);
  *dev_msec = DEV_RTT(*dev_msec, *est_msec, time_msec);
  /* recalculate timeout  */
  return (TIMEOUT(*est_msec, *dev_msec))*1.0 / 1E3;
}

int main(int argc, char **argv) {
  if(argc<2){
    printf("Usage: ./sender <ip> <port> <filename>\n");
    exit(1);
  }
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(sock < 0) {
    perror("Creating socket failed: ");
    exit(1);
  }
  /* estimated roundtrip time and deviation in milliseconds */
  long est_rtt_msec = 2; 
  long dev_rtt_msec = 2;
  /* create time for socket timeout */
  struct timeval time;
  time.tv_sec = 2; /* 2 second timeout */ 
  time.tv_usec = 0;
  /* set socket timeout for recv */
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&time, sizeof(time));
  /* create sockaddr_in struc for address */
  struct sockaddr_in addr; 
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(argv[2]));
  inet_pton(AF_INET,argv[1],&addr.sin_addr.s_addr); 
  // create buffer
  char buffer[MAX_BUF];
  memset(buffer, 0, MAX_BUF);
  uint32_t seq_num = 0; 
  /* open file for reading */
  FILE *file=fopen(argv[3],"rb");
  if(!file) {
    perror("unable to open file");
  } 
  /* containers for segments and acks */
  std::vector<struct rdt_segment> segments;
  std::vector<uint16_t> acks;
  int read;
  /* separate file into chunks */
  while((read = fread(buffer, 1, MAX_DATA, file)) > 0){
    struct rdt_segment seg;
    /* store chunk of file into data */
    memcpy(seg.data, buffer, read);
    struct rdt_header hdr;
    hdr.seq_num = seq_num;
    hdr.flag = DATA;
    /* set header size to include length of data */
    hdr.length = read + sizeof(struct rdt_header);
    /* create rdt segment */
    seg.hdr = hdr;
    /* add segment to container */
    segments.push_back(seg);
    ++seq_num;
  } 
  acks.resize(segments.size(), 0);
  seq_num = 0;
  uint32_t state = WAIT_SYN_ACK;
  /* check if all segments have been ack'd by receiver */
  while(1){ 
    if(state == WAIT_SYN_ACK){
      /* create syn header */
      struct rdt_header hdr;
      hdr.flag = SYN;
      hdr.length = sizeof(struct rdt_header);
      /* send syn header */ 
      if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&addr,sizeof(addr)) < 0){
        perror("error sending segment");
        exit(1);
      }
    }
    else if(state == WAIT_ACK){
      struct rdt_segment *seg = &segments[seq_num];
      /* send segment */ 
      if(sendto(sock, (void*)seg, seg->hdr.length, 0,
            (struct sockaddr*)&addr,sizeof(addr)) < 0){
        perror("error sending segment");
        exit(1);
      }
    }
    else if(state == WAIT_FIN){
      /* create header for fin */
      struct rdt_header hdr;
      hdr.flag = FIN;
      hdr.length = sizeof(struct rdt_header);
      /* send fin header */ 
      if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&addr,sizeof(addr)) < 0){
        perror("error sending segment");
        exit(1);
      }
    } 
    struct timespec before, after;  
    /* start timer */
    if(clock_gettime(CLOCK_REALTIME, &before) < 0){
      perror("error reading clock");
      exit(1);
    } 
    /* wait for acknowledgment */
    if(recv(sock, buffer, MAX_BUF, 0) < 0){
      /* check for timeout errors */
      if(errno == EAGAIN || errno == EWOULDBLOCK){  
        /* update timeout */
        time.tv_sec *= 2;
        /* check if receiver timed out */
        if(time.tv_sec >= 60){
          fprintf(stderr, "receiver timed out\n");
          exit(1);
        }
        /* set socket timeout for recv */
        if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&time, sizeof(time)) < 0){
          perror("error setting timeout");
          exit(1);
        }
      }/* other non-timeout errors */
      else{ 
        perror("error receiving ack");
        exit(1);
      }
    }
    else{
      /* end timer */
      if(clock_gettime(CLOCK_REALTIME, &after) < 0){
        perror("error reading clock");
        exit(1);
      }
      /* recalculate timeout  */
      time.tv_sec = timeout(before,after,&est_rtt_msec,&dev_rtt_msec);
      /* update timeout */
      if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&time, sizeof(time)) < 0){
        perror("error setting timeout");
        exit(1);
      }
      struct rdt_header *hdr = (struct rdt_header*)buffer;
      /* check state before processing header */
      if(state == WAIT_SYN_ACK){
        /* check if receiver ack'd syn */
        if(hdr->flag == SYN | ACK){
          /* update state to send segments */
          state = WAIT_ACK;
        }
      }
      else if(state == WAIT_ACK){
        /* check if segment is ack */
        if(hdr->flag == ACK){
          uint32_t ack_num = hdr->ack_num;
          /* check if correct ack num received */
          if(ack_num == seq_num){
            /* update seq num */
            acks[ack_num] = 1;
            seq_num++;
          }
        }
        /* check if all segments have been ack'd */
        if(seq_num == segments.size()){
          /* update state to send fin */
          state = WAIT_FIN;
        }
      }
      else if(state == WAIT_FIN){
        if(hdr->flag == FIN){
          /* create header to ack fin */
          struct rdt_header hdr;
          hdr.flag = FIN | ACK;
          hdr.length = sizeof(struct rdt_header);
          /* send ack fin header */ 
          if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&addr,sizeof(addr)) < 0){
            perror("error sending segment");
            exit(1);
          }
          break;
        }
      }
    }
  }
  return 0;
}



