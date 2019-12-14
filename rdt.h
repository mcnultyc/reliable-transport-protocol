#ifndef RDT_H
#define RDT_H

#define EST_RTT(est_rtt, sample_rtt) 0.875 * est_rtt + 0.125 *  sample_rtt
#define DEV_RTT(dev_rtt, est_rtt, sample_rtt) 0.75 * dev_rtt + 0.25 * abs(sample_rtt - est_rtt)
#define TIMEOUT(est_rtt, dev_rtt) est_rtt + 4 * dev_rtt

#define MAX_DATA 1024 /* size of data portion of segment */

enum flags{ 
    DATA = 1 << 0, 
    ACK  = 1 << 1, 
    SYN  = 1 << 2, 
    FIN  = 1 << 3
};

/* header for reliable data transfer */
struct rdt_header{
  uint32_t seq_num;
  uint32_t ack_num; 
  uint32_t flag;
  uint32_t length;
};

/* segment for reliable data transfer */
struct rdt_segment{
  struct rdt_header hdr;
  char data[MAX_DATA];
};
#endif
