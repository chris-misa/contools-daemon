#include <stdio.h>

#include "libftrace.h"

#define TRACING_FS_PATH "/sys/kernel/debug/tracing"
#define TRACE_CLOCK "global"

int main()
{
  FILE *tp;
  float oh;

  tp = get_trace_pipe(TRACING_FS_PATH,
                      "net:*",
                      NULL,
                      TRACE_CLOCK);
  oh = get_event_overhead(10, &tp, TRACING_FS_PATH);

  fprintf(stderr, "Got ftrace event overhead: %f usec\n", oh);

  release_trace_pipe(tp, TRACING_FS_PATH); 

  return 0;
}
