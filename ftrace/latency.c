//
// Measure network latency between devices
// by reading ftrace events
//
// Must be plugged in to devices and events on those devices
// with an understanding of 1) how devices are routed in the kernel
// and 2) how the targeted measurement tool gathers timestamps.
//
// Basically the input is two 4-tuples, one describing the input path
// and one describing the output path.
//
// Input:
//   in_outer_dev:  The wire-facing device as named in the kernel
//   in_outer_func: The name of the event on this outer device which signifies reception of a packet
//   in_inner_dev:  The inner device as named in kernel (or container's netns)
//   in_inner_func: The event on the inner device which signifies reception of a packet (or location of the timestamping)
//
// Output:
//   out_inner_dev:  The inner device where userspace in the container dumps packets
//   out_inner_func: The event on the inner device which signifies sending of a packet
//   out_outer_dev:  The wire-facing device as named in the kernel
//   out_outer_func: The event signifying sending of a packet from the kernel boundary
//
// These fields should all be filled in in a conf file which is pointed to by the only argument
//

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

char *in_outer_dev = NULL;
char *in_outer_func = NULL;
char *in_inner_dev = NULL;
char *in_inner_func = NULL;

char *out_inner_dev = NULL;
char *out_inner_func = NULL;
char *out_outer_dev = NULL;
char *out_outer_func = NULL;

char *ftrace_set_events = NULL;

void
usage()
{
  fprintf(stdout, "Usage: latency <configuration file>\n");
}

void
do_exit()
{
  running = 0;
}

// Parse the given config file and set globals
// Returns 0 on success, nonzero on error
int
parse_config_file(const char *filepath)
{
  FILE *fp = NULL;
  char buf[CONFIG_LINE_BUFFER];
  char *bufp = NULL,
       *bufp2 = NULL;
  int len;
  char **target = NULL;
  unsigned char complete = 0;

  fp = fopen(filepath, "r");
  if (!fp) {
    fprintf(stderr, "Failed to open config file '%s'\n", filepath);
    return -1;
  }

  while (fgets(buf, CONFIG_LINE_BUFFER, fp) != NULL) {
    bufp = buf;
    while (*bufp != ':' && *bufp != '\0') {
      bufp++;
    }
    if (*bufp == ':') {
      len = bufp - buf;
      if (!strncmp("in_outer_dev", buf, len)) {
        target = &in_outer_dev;
        complete |= 1;
      } else if (!strncmp("in_outer_func", buf, len)) {
        target = &in_outer_func;
        complete |= 1 << 1;
      } else if (!strncmp("in_inner_dev", buf, len)) {
        target = &in_inner_dev;
        complete |= 1 << 2;
      } else if (!strncmp("in_inner_func", buf, len)) {
        target = &in_inner_func;
        complete |= 1 << 3;
      } else if (!strncmp("out_inner_dev", buf, len)) {
        target = &out_inner_dev;
        complete |= 1 << 4;
      } else if (!strncmp("out_inner_func", buf, len)) {
        target = &out_inner_func;
        complete |= 1 << 5;
      } else if (!strncmp("out_outer_dev", buf, len)) {
        target = &out_outer_dev;
        complete |= 1 << 6;
      } else if (!strncmp("out_outer_func", buf, len)) {
        target = &out_outer_func;
        complete |= 1 << 7;
      }

      bufp++;
      bufp2 = bufp;

      while (*bufp2 != '\n' && *bufp2 != '\0') {
        bufp2++;
      }

      len = bufp2 - bufp;

      *target = (char *)malloc(sizeof(char) * (len + 1));
      strncpy(*target, bufp, len);
      (*target)[len] = '\0';
    }
    // Otherwise syntax error, ignore the line
  }

  if (complete != 0xff) {
    fprintf(stderr, "Incomplete config file\n");
    return -2;
  }

  len = strlen(in_outer_func)
      + strlen(in_inner_func)
      + strlen(out_inner_func)
      + strlen(out_outer_func)
      + 4; // add 4: three spaces plus on \0
  ftrace_set_events = (char *)malloc(len);
  *ftrace_set_events = '\0';
  strcat(ftrace_set_events, in_outer_func);
  strcat(ftrace_set_events, " ");
  strcat(ftrace_set_events, in_inner_func);
  strcat(ftrace_set_events, " ");
  strcat(ftrace_set_events, out_inner_func);
  strcat(ftrace_set_events, " ");
  strcat(ftrace_set_events, out_outer_func);

  return 0;
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
void print_timestamp(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("[%lu.%06lu] ",
         (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);
}

int main(int argc, char *argv[])
{
  trace_pipe_t tp;
  int nbytes = 0;

  char buf[TRACE_BUFFER_SIZE];
  struct trace_event evt;

  char send_skbaddr[SKBADDR_BUFFER_SIZE];
  struct timeval start_send_time;
  struct timeval finish_send_time;
  long long unsigned int send_raw_usec = 0;
  long long unsigned int send_sum = 0;
  unsigned int send_num = 0;
  int expect_send = 0;
  int send_num_func = 0;

  char recv_skbaddr[SKBADDR_BUFFER_SIZE];
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

  // Save working directory and push 'unsynced' to file for monitoring
  if (getcwd(synced_indicator, PATH_MAX) == NULL) {
    fprintf(stderr, "Failed to save current directory\n");
    return 1;
  }
  // Break before we run out of buffer here
  if (strlen(synced_indicator) + 15 > PATH_MAX) {
    fprintf(stderr, "synced_indicator path would be too long\n");
    return 1;
  }
  strcat(synced_indicator, "/ftrace_synced");
  echo_to(synced_indicator, "0");



  // Parse config file and dump some details for reference
  parse_config_file(argv[1]);
  fprintf(stdout, "in_outer_dev:   %s\n", in_outer_dev);
  fprintf(stdout, "in_outer_func:  %s\n", in_outer_func);
  fprintf(stdout, "in_inner_dev:   %s\n", in_inner_dev);
  fprintf(stdout, "in_inner_func:  %s\n", in_inner_func);
  fprintf(stdout, "out_inner_dev:  %s\n", out_inner_dev);
  fprintf(stdout, "out_inner_func: %s\n", out_inner_func);
  fprintf(stdout, "out_outer_dev:  %s\n", out_outer_dev);
  fprintf(stdout, "out_outer_func: %s\n", out_outer_func);
  fprintf(stdout, "events: %s\n", ftrace_set_events);
  fprintf(stdout, "trace_clock: %s\n", TRACE_CLOCK);
  
  // Trap interupts
  signal(SIGINT, do_exit);

  // Get ftrace event overhead
  fprintf(stdout, "Getting ftrace event overhead. . .\n");
  usec_per_event = get_event_overhead(TRACING_FS_PATH,
                                      ftrace_set_events,
                                      TRACE_CLOCK,
                                      OVERHEAD_NPROBES);
  fprintf(stdout, "Estimated usec per event: %f\n", usec_per_event);

  echo_to(synced_indicator, "1");

  // Get the trace pipe
  tp = get_trace_pipe(TRACING_FS_PATH, ftrace_set_events, NULL, TRACE_CLOCK);
  if (!tp) {
    fprintf(stderr, "Failed to open trace pipe\n");
    return 1;
  }

  // Main loop
  fprintf(stdout, "Listening for events. . . will report in usec\n");
  while (running) {
    // Read the next line from the trace pipe
    if (read_trace_pipe(buf, TRACE_BUFFER_SIZE, tp) > 0 && running) {

      // If there's data, parse it
      trace_event_parse_str(buf, &evt);

      // Count the reading of this event
      recv_num_func++;
      send_num_func++;

      // Handle events

      if (!strncmp(in_outer_func, evt.func_name, evt.func_name_len)
       && !strncmp(in_outer_dev, evt.dev, evt.dev_len)
       && ping_on_wire) {
        // Got a inbound event on outer dev

        memcpy(recv_skbaddr, evt.skbaddr, evt.skbaddr_len);
        recv_skbaddr[evt.skbaddr_len] = '\0';
        start_recv_time = evt.ts;
        expect_recv = 1;
        recv_num_func = 1;
      } else
      if (!strncmp(in_inner_func, evt.func_name, evt.func_name_len)
       && !strncmp(in_inner_dev, evt.dev, evt.dev_len)
       && !strncmp(recv_skbaddr, evt.skbaddr, evt.skbaddr_len)
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
          print_timestamp();
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
       && !strncmp(out_inner_dev, evt.dev, evt.dev_len)
       && !ping_on_wire) {

        // Got a outbound event on inner dev
        memcpy(send_skbaddr, evt.skbaddr, evt.skbaddr_len);
        send_skbaddr[evt.skbaddr_len] = '\0';
        start_send_time = evt.ts;
        expect_send = 1;
        send_num_func = 1;
      } else
      if (!strncmp(out_outer_func, evt.func_name, evt.func_name_len)
       && !strncmp(out_outer_dev, evt.dev, evt.dev_len)
       && !strncmp(send_skbaddr, evt.skbaddr, evt.skbaddr_len)
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
    }
  }

  release_trace_pipe(tp, TRACING_FS_PATH);

  print_stats(send_sum, send_num, recv_sum, recv_num);

  fprintf(stdout, "Done.\n");

  return 0;
}
