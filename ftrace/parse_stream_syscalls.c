//
// Measure network latency between devices
// by reading ftrace events from stdin
// and appling a specific heuristics.
//
// Note: unlike parse_stream.c and latency.c,
// this latency program is not 'pluggable'
// but rather the events watched are hard coded
//
// Note: This method needs to know the pid of
// the target ping process to find the
// outgoing sendto syscalls.
//
//

// #define SHOW_SEND_RECV

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "libftrace.h"
#include "../time_common.h"

#define CONFIG_LINE_BUFFER 1024
#define TRACE_BUFFER_SIZE 0x1000
#define SKBADDR_BUFFER_SIZE 256

#define TRACING_FS_PATH "/sys/kernel/debug/tracing"
#define TRACE_CLOCK "global"

// Number of probes used to get ftrace overhead
#define OVERHEAD_NPROBES 10

// Discard latencies above this threshold as outliers
#define MAX_RAW_LATENCY 1000

// Max file path for saving current directory
#ifndef PATH_MAX
#define PATH_MAX 512
#endif

static volatile int running = 1;

char *in_outer_dev = "eno1d1";
char *in_outer_func = "netif_receive_skb";
char *in_inner_func = "sys_exit_recvmsg";

char *out_inner_func = "sys_enter_sendto";
char *out_outer_dev = "eno1d1";
char *out_outer_func = "net_dev_xmit";

char *ftrace_set_events = NULL;

void
usage()
{
  fprintf(stdout, "Usage: latency <ping pid>\n");
}

void
do_exit()
{
  running = 0;
}

void
print_stats(long long unsigned int send_sum,
                 unsigned int send_num,
                 long long unsigned int recv_sum,
                 unsigned int recv_num)
{
  long long unsigned int send_mean;
  long long unsigned int recv_mean;

  if (send_num) {
    send_mean = send_sum / send_num;
  } else {
    send_mean = 0;
  }

  if (recv_num) {
    recv_mean = recv_sum / recv_num;
  } else {
    recv_mean = 0;
  }

  fprintf(stdout, "\nLatency stats:\n");
  fprintf(stdout, "send mean: %llu usec\n", send_mean);
  fprintf(stdout, "recv mean: %llu usec\n", recv_mean);
  fprintf(stdout, "rtt  mean: %llu usec\n", send_mean + recv_mean);
}


/*
 * Print timestamp
 * (Lifted from iputils/ping_common.c)
 */
void print_timestamp(struct timeval *tv)
{
  printf("[%lu.%06lu] ",
         (unsigned long)tv->tv_sec, (unsigned long)tv->tv_usec);
}

int main(int argc, char *argv[])
{
  int nbytes = 0;

  char buf[TRACE_BUFFER_SIZE];
  struct trace_event evt;

  int ping_pid;
  struct timeval start_send_time;
  struct timeval finish_send_time;
  long long unsigned int send_raw_usec = 0;
  long long unsigned int send_sum = 0;
  unsigned int send_num = 0;
  int expect_send = 0;
  int send_num_func = 0;

  int recv_len;
  struct timeval start_recv_time;
  struct timeval finish_recv_time;
  long long unsigned int recv_raw_usec = 0;
  long long unsigned int recv_sum = 0;
  unsigned int recv_num = 0;
  int expect_recv = 0;
  int recv_num_func = 0;

  float usec_per_event = 0.0;

  float send_events_overhead = 0.0;
  float send_adj_latency = 0.0;
  float recv_events_overhead = 0.0;
  float recv_adj_latency = 0.0;

  int ping_on_wire = 0;

  char synced_indicator[PATH_MAX];


  if (argc != 2) {
    usage();
    return 1;
  }
  ping_pid = strtol(argv[1], NULL, 10);

  fprintf(stdout, "in_outer_dev:   %s\n", in_outer_dev);
  fprintf(stdout, "in_outer_func:  %s\n", in_outer_func);
  fprintf(stdout, "in_inner_func:  %s\n", in_inner_func);
  fprintf(stdout, "out_inner_func: %s\n", out_inner_func);
  fprintf(stdout, "out_outer_dev:  %s\n", out_outer_dev);
  fprintf(stdout, "out_outer_func: %s\n", out_outer_func);
  fprintf(stdout, "ping_pid: %d\n", ping_pid);
  
  /*
  // Get ftrace event overhead
  fprintf(stdout, "Getting ftrace event overhead. . .\n");
  usec_per_event = get_event_overhead(TRACING_FS_PATH,
                                      ftrace_set_events,
                                      TRACE_CLOCK,
                                      OVERHEAD_NPROBES);
  fprintf(stdout, "Estimated usec per event: %f\n", usec_per_event);
  */

  // Main loop
  fprintf(stdout, "Listening for events. . . will report in usec\n");
  while (1) {
    if (fgets(buf, TRACE_BUFFER_SIZE, stdin) != NULL) {

      // If there's data, parse it
      trace_event_parse_report(buf, &evt);

      // fprintf(stdout, "Got event => pid: %d, len: %d, event: %s\n", evt.pid, evt.len, evt.func_name);

      // Count the reading of this event
      recv_num_func++;
      send_num_func++;

      // Handle events

      if (!strncmp(in_outer_func, evt.func_name, evt.func_name_len)
       && !strncmp(in_outer_dev, evt.dev, evt.dev_len)
       && ping_on_wire) {
        // Got a inbound event on outer dev

	recv_len = evt.len;
        start_recv_time = evt.ts;
        expect_recv = 1;
        recv_num_func = 1;
      } else
      if (!strncmp(in_inner_func, evt.func_name, evt.func_name_len)
       && recv_len == evt.len
       && expect_recv
       && ping_on_wire) {
        // Got a inbound event on inner dev, the skbaddr matches, and it was expected

        finish_recv_time = evt.ts;
        tvsub(&finish_recv_time, &start_recv_time);
        if (finish_recv_time.tv_usec < MAX_RAW_LATENCY) {

          // Process received packet info
          recv_raw_usec = finish_recv_time.tv_sec * 1000000 + finish_recv_time.tv_usec;
          recv_events_overhead = (float)recv_num_func * usec_per_event;
          recv_adj_latency = (float)recv_raw_usec - recv_events_overhead;

#ifdef SHOW_SEND_RECV
          fprintf(stdout, "recv raw_latency: %llu, num_events: %d, events_overhead: %f, adj_latency: %f\n",
                  recv_raw_usec,
                  recv_num_func,
                  recv_events_overhead,
                  recv_adj_latency);
#endif
          print_timestamp(&evt.ts);
          fprintf(stdout, "rtt raw_latency: %llu, events_overhead: %f, adj_latency: %f\n",
                  recv_raw_usec + send_raw_usec,
                  recv_events_overhead + send_events_overhead,
                  recv_adj_latency + send_adj_latency);
                  
          recv_sum += finish_recv_time.tv_sec * 1000000
                    + finish_recv_time.tv_usec;
          recv_num++;
          ping_on_wire = 0;
        } else {

          // Discard received packet as outlier
          fprintf(stdout, "discarded recv: %lu\n",
                  finish_recv_time.tv_sec * 1000000 + finish_recv_time.tv_usec);
          ping_on_wire = 0;
        }
        expect_recv = 0;

      } else
      if (!strncmp(out_inner_func, evt.func_name, evt.func_name_len)
       && evt.pid == ping_pid
       && !ping_on_wire) {

        start_send_time = evt.ts;
        expect_send = 1;
        send_num_func = 1;
      } else
      if (!strncmp(out_outer_func, evt.func_name, evt.func_name_len)
       && !strncmp(out_outer_dev, evt.dev, evt.dev_len)
       && evt.pid == ping_pid
       && expect_send
       && !ping_on_wire) {
        // Got a outbound event on outer dev, the skbaddr matches, and we were expecting it

        finish_send_time = evt.ts;
        tvsub(&finish_send_time, &start_send_time);
        if (finish_send_time.tv_usec < MAX_RAW_LATENCY) {

          // Process send packet info
          send_raw_usec = finish_send_time.tv_sec * 1000000 + finish_send_time.tv_usec;
          send_events_overhead = (float)send_num_func * usec_per_event;
          send_adj_latency = (float)send_raw_usec - send_events_overhead;
#ifdef SHOW_SEND_RECV
          fprintf(stdout, "send latency: %llu, num_events: %d, events_overhead: %f, adj_latency: %f\n",
                  send_raw_usec,
                  send_num_func,
                  send_events_overhead,
                  send_adj_latency);
#endif
          send_sum += finish_send_time.tv_sec * 1000000
                    + finish_send_time.tv_usec;
          send_num++;
          ping_on_wire = 1;
        } else {

          // Discard send packet info as outlier
          fprintf(stdout, "discarded send: %lu\n",
                  finish_send_time.tv_sec * 1000000 + finish_send_time.tv_usec);
        }
        expect_send = 0;
      }
    } else {
      break;
    }
  }

  print_stats(send_sum, send_num, recv_sum, recv_num);

  fprintf(stdout, "Done.\n");

  return 0;
}
