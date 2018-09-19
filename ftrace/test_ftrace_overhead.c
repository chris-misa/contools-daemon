#include <stdio.h>

#include "libftrace.h"

#define TRACING_FS_PATH "/sys/kernel/debug/tracing"
#define TRACE_CLOCK "global"

int main()
{
  float oh;

  oh = get_event_overhead(TRACING_FS_PATH, "net:*", TRACE_CLOCK, 10);

  fprintf(stderr, "Got ftrace event overhead: %f usec\n", oh);

  return 0;
}
