#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pcap/pcap.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include "libpcap_common.c"

// #define DEBUG

static volatile int running = 1;
pcap_t *pcap_hdl;

void usage()
{
  printf("Usage: libpcap_test <device>\n");
}

void do_exit()
{
  running = 0;
  pcap_breakloop(pcap_hdl);
}

int main(int argc, char *argv[])
{
  struct packet_event evt;
  int res;

  if (argc != 2) {
    usage();
    exit(1);
  }

  signal(SIGINT, do_exit);

  pcap_hdl = get_capture(argv[1]);  
   
  while (1) {
    res = get_packet_event(pcap_hdl, &evt);
    if (running) {
      if (res) {
        printf("[%lu.%06lu] ", evt.ts.tv_sec, evt.ts.tv_usec);
        switch (evt.type) {
          case PACKET_TYPE_ECHO_REQUEST:
            printf("echo request\n");
            break;
          case PACKET_TYPE_ECHO_REPLY:
            printf("echo reply\n");
            break;
        }
      }
    } else {
      break;
    }
  }

  release_capture(pcap_hdl);

  printf("Done.\n");

  return 0;
}
