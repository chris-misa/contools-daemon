#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "ftrace_common.c"

static volatile int running = 1;

void usage()
{
  printf("Usage: ftrace_test <pid>\n");
}

void do_exit()
{
  running = 0;
}

int main(int argc, char *argv[])
{
  const char *tracefp = "/sys/kernel/debug/tracing";
  FILE *trace_pipe;
  struct trace_event evt;
  struct timeval ftrace_offset;

  // this call also sets up trace dir things which we need
  get_ftrace_ts_offset(tracefp, &ftrace_offset);

  if (argc != 2) {
    usage();
    exit(1);
  }

  // Set interupt handler
  signal(SIGINT, do_exit);

  // Set up tracing
  trace_pipe = get_trace_pipe(tracefp, argv[1]);
  if (trace_pipe == NULL) {
    printf("Failed to open trace pipe\n");
    exit(1);
  }
  
  // Read trace pipe until interupt
  while (1) {
    // Read the pipe
    get_trace_event(trace_pipe, &evt);
    // Do some stuff
    if (running) {
      printf("[%lu.%06lu] ", evt.ts.tv_sec, evt.ts.tv_usec);
      switch (evt.type) {
        case EVENT_TYPE_ENTER_SENDTO:
          printf("enter_sendto\n");
          break;
        case EVENT_TYPE_EXIT_SENDTO:
          printf("exit_sendto\n");
          break;
        case EVENT_TYPE_ENTER_RECVMSG:
          printf("enter_recvmsg\n");
          break;
        case EVENT_TYPE_EXIT_RECVMSG:
          printf("exit_recvmsg\n");
          break;
      }
    } else {
      break;
    }
  }

  // Clean up a bit
  release_trace_pipe(trace_pipe, tracefp);

  printf("Done.\n");

  return 0;
}
