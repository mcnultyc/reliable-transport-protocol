#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include "rdt.h"  

#define MAX_BUF 1500

enum state{WAIT_SYN, WAIT_DATA, WAIT_FIN_ACK};

int main(int argc, char** argv) { 
  
  if(argc<2) {    
    printf("Usage: ./receiver <port>\n");
    exit(1);
  }

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(sock < 0) {
    perror("Creating socket failed: ");
    exit(1);
  }
  
  struct sockaddr_in addr;  // internet socket address data structure
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(argv[1])); // byte order is significant
  addr.sin_addr.s_addr = INADDR_ANY;
  
  int res = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  if(res < 0) {
    perror("Error binding: ");
    exit(1);
  }
    /* create time for socket timeout */
  struct timeval time;
  time.tv_sec = 30; /* 30 second timeout before shutting down */
  time.tv_usec = 0;
  /* set socket timeout for recv */
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&time, sizeof(time));
  struct sockaddr_in sender_addr;
  char buffer[MAX_BUF];
  memset(buffer,0,sizeof(buffer));
  uint32_t seq_num = 0; 
  uint32_t state = WAIT_SYN;
  while(1) {
    socklen_t length = sizeof(struct sockaddr_in);
    if(recvfrom(sock, buffer, MAX_BUF, 0, 
          (struct sockaddr*)&sender_addr, &length) < 0) {
      /* check if the sender timed out */ 
      if(errno == EAGAIN || errno == EWOULDBLOCK){
        fprintf(stderr, "sender timeout\n");
      }
      else{
        perror("error receiving segment");  
      }
      exit(1);
    } 
    struct rdt_segment *seg = (struct rdt_segment*)buffer;
    if(state == WAIT_SYN){
      if(seg->hdr.flag == SYN){
        /* create header for syn ack */
        struct rdt_header hdr;
        hdr.flag = SYN | ACK;
        hdr.length = sizeof(struct rdt_header);
        /* send syn ack to sender */
        if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&sender_addr, sizeof(sender_addr)) < 0){
          perror("error sending ack");
          exit(1);
        }
      }
      else if(seg->hdr.flag == DATA){
        /* update state to wait for data */
        state = WAIT_DATA;
        /* check for correct seq num */
        if(seg->hdr.seq_num == 0){
          /* create header for ack */
          struct rdt_header hdr;
          hdr.flag = ACK;
          hdr.ack_num = 0;
          hdr.length = sizeof(struct rdt_header);
          /* write data and update seq num */
          int length = seg->hdr.length - sizeof(struct rdt_header);
          write(1, seg->data, length);
          seq_num++;
          /* send ack to sender */
          if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&sender_addr, sizeof(sender_addr)) < 0){
            perror("error sending ack");
            exit(1);
          }
        }
      }
    }
    else if(state == WAIT_DATA){
      /* check is segment contains data */
      if(seg->hdr.flag == DATA){
        /* create default header for ack */
        struct rdt_header hdr;
        hdr.flag = ACK;
        hdr.ack_num = seq_num - 1;
        hdr.length = sizeof(struct rdt_header);
        /* check if corret seq num received */
        if(seg->hdr.seq_num == seq_num){
          /* update header for ack */
          hdr.flag = ACK;
          hdr.ack_num = seq_num;
          /* write data and update seq num */
          int length = seg->hdr.length - sizeof(struct rdt_header);
          write(1, seg->data, length);
          seq_num++;
        }
        /* send ack to sender */
        if(sendto(sock, (void*)&hdr, hdr.length, 0,
            (struct sockaddr*)&sender_addr, sizeof(sender_addr)) < 0){
          perror("error sending ack");
          exit(1);
        }
      }
      else if(seg->hdr.flag == FIN){
        /* update to wait for fin ack */
        state = WAIT_FIN_ACK; 
        /* create header for fin */
        struct rdt_header hdr;
        hdr.flag = FIN;
        hdr.length = sizeof(struct rdt_header);
        /* send fin to sender */
        if(sendto(sock, (void*)&hdr, hdr.length, 0,
          (struct sockaddr*)&sender_addr, sizeof(sender_addr)) < 0){
          perror("error sending ack");
          exit(1);
        } 
      }
      else if(seg->hdr.flag == SYN){
        fprintf(stderr, "invalid syn received\n");
        exit(1);  
      }
    }
    else if(state == WAIT_FIN_ACK){
      if(seg->hdr.flag == FIN){
        /* create header for fin */
        struct rdt_header hdr;
        hdr.flag = FIN;
        hdr.length = sizeof(struct rdt_header);
        /* send fin to sender */
        if(sendto(sock, (void*)&hdr, hdr.length, 0,
          (struct sockaddr*)&sender_addr, sizeof(sender_addr)) < 0){
          perror("error sending ack");
          exit(1);
        }
      }
      /* check that sender recieved ack*/
      if(seg->hdr.flag == FIN | ACK){
        break;
      }
    }
  } 
  shutdown(sock,SHUT_RDWR);
}



