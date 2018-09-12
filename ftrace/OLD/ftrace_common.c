#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#include "time_common.h"

#define READ_BUF_SIZE 256
#define NAME_BUF_SIZE 64

// Simply write into the given file and close
void echo_to(const char *file, const char *data)
{
  FILE *fp = fopen(file, "w");
  int res;
  if (fp == NULL) {
    printf("Failed to open file: '%s'\n", file);
    return;
  }
  res = fputs(data, fp);
  if (res == EOF) {
    printf("Failed writing to: '%s'\n", file);
  }
  fclose(fp);
}


// Ugly parse of text string read from pipe
// In the future this will be replaced by binary reads
enum event_types {
  EVENT_TYPE_NONE,
  EVENT_TYPE_ENTER_SENDTO,
  EVENT_TYPE_EXIT_SENDTO,
  EVENT_TYPE_ENTER_RECVMSG,
  EVENT_TYPE_EXIT_RECVMSG,
  EVENT_TYPE_ENTER_SELECT,
  EVENT_TYPE_EXIT_SELECT
};

struct trace_event {
  struct timeval   ts;
  enum event_types type;
};

// Parse a string read from pipe into trace_event struct
void parse_trace_event(char *str, struct trace_event *evt)
{
  int dot_count = 0;
  char *end = NULL;
  int name_len = 0;
  char name_buf[NAME_BUF_SIZE];
  
  evt->ts.tv_sec = 0;
  evt->ts.tv_usec = 0; 
  evt->type = EVENT_TYPE_NONE;
  
  // Look for four dot separator
  while (dot_count < 4 && *str != '\0') {
    if (*str != '.') {
      dot_count = 0;
    } else {
      dot_count++;
    }
    str++;
  } 

  // Get seconds and micro seconds
  evt->ts.tv_sec = strtol(str, &end, 10);
  str = end + 1; // skip the decimal point
  evt->ts.tv_usec = strtol(str, &end, 10);
  str = end + 6; // skip the colon, space, and 'sys_' prefix

  // get the event type string
  end = str;
  while (*end != ' ' && *end != '(' && *end != '\0') {
    end++;
  }
  
  name_len = end - str;
  // silently truncate if longer than buffer
  if (name_len > NAME_BUF_SIZE) {
    name_len = NAME_BUF_SIZE;
  }
  memcpy(name_buf, str, name_len);   
  name_buf[name_len] = '\0';

  if (*end == '(') {
    // Entering a syscall, figure out which
    if (!strcmp(name_buf, "sendto")) {
      evt->type = EVENT_TYPE_ENTER_SENDTO;
    } else if (!strcmp(name_buf, "recvmsg")) {
      evt->type = EVENT_TYPE_ENTER_RECVMSG;
    } else if (!strcmp(name_buf, "select")) {
      evt->type = EVENT_TYPE_ENTER_SELECT;
    }
  } else {
    // Exiting a syscall, figure out which
    if (!strcmp(name_buf, "sendto")) {
      evt->type = EVENT_TYPE_EXIT_SENDTO;
    } else if (!strcmp(name_buf, "recvmsg")) {
      evt->type = EVENT_TYPE_EXIT_RECVMSG;
    } else if (!strcmp(name_buf, "select")) {
      evt->type = EVENT_TYPE_EXIT_SELECT;
    }
  }
}

FILE *get_trace_pipe(const char *debug_fs_path, const char *pid)
{
  chdir(debug_fs_path);
  // assume some things are already set from call to
  // get_ftrace_ts_offset!
  // Specifically: trace_clock, tracing_on, current_tracer
  echo_to("set_event", "syscalls:sys_enter_sendto syscalls:sys_exit_sendto syscalls:sys_enter_recvmsg syscalls:sys_exit_recvmsg");
  echo_to("set_event_pid", pid);

  return fopen("trace_pipe","r");
}

void release_trace_pipe(FILE *tp, const char *debug_fs_path)
{
  fclose(tp);
  chdir(debug_fs_path);
  echo_to("tracing_on", "0");
  echo_to("set_event_pid", "");
  echo_to("set_event", "");
}

void get_trace_event(FILE *tp, struct trace_event *evt)
{
  char buf[READ_BUF_SIZE];
  fgets(buf, READ_BUF_SIZE, tp);
  parse_trace_event(buf, evt);
}

// Attempt to get the offset between ftrace time stamps and system time
// Returns 0 on success, non-zero on error
//
// Issues:
//   Using select as the probe functions because gettimeofday itself
//   is probably implemented in vDSO. Would be best to check if this is
//   the case and if not, just use gettimeofday.
//
//   If anybody else is ready the trace pipe, the fgets calls
//   will block indefinitely! This is a general problem with this mode
//   of accessing the ftrace interface.
//
// In general this seems to be approximately ok.
// Notes: offset seems to grow for consecutive calls ?
//        seems to be a little undershooting.
//
// Believability requirements:
//  echo request must be AFTER enter sendto
//  echo reply must be BEFORE exit recvmsg
//
int get_ftrace_ts_offset(const char *debug_fs_path, struct timeval *offset)
{
  FILE *tp = NULL;
  char pid[128];
  int ntests = 10;
  int i;
  char buf[READ_BUF_SIZE];

  struct timeval timeout;
  struct timeval select_timeout;

  struct timeval system_time;
  struct trace_event evt;

  double sum = 0.0;

  // Get pid into a string for writing to debugfs
  sprintf(pid, "%d", getpid());
  
  // Quick, non-zero timeout for dummy select
  timeout.tv_sec = 0;
  timeout.tv_usec = 500000;
  
  // Set up ftrace
  chdir(debug_fs_path);
  echo_to("trace", "");
  echo_to("trace_pipe", "");
  echo_to("current_tracer", "nop");
  echo_to("set_event", "syscalls:sys_enter_select syscalls:sys_exit_select");
  echo_to("set_event_pid", pid);
  echo_to("trace_clock","global");
  echo_to("tracing_on", "1");

  tp =  fopen("trace_pipe","r");
  if (!tp) {
    return -1;
  }

  // Run dummy selects
  for (i = 0; i < ntests; i++) {
    // Copy timeout for select
    select_timeout = timeout;
    // Get wall clock time
    gettimeofday(&system_time, NULL);
    // Dummy select call to pick up in ftrace
    select(0, NULL, NULL, NULL, &select_timeout);

    // Read until we find the enter select event
    do {
      fgets(buf, READ_BUF_SIZE, tp);
      parse_trace_event(buf, &evt);
    } while (evt.type != EVENT_TYPE_ENTER_SELECT);
    
    // Compute offset = system_time - enter select time
    *offset = system_time;
    tvsub(offset, &evt.ts);

    sum += (double)offset->tv_sec + 
           ((double)offset->tv_usec) / 1000000;
    
    // printf("System time: %lu.%06lu\n",
    //                      system_time.tv_sec,
    //                      system_time.tv_usec);
    // printf("Select time: %lu.%06lu\n",
    //                      evt.ts.tv_sec,
    //                      evt.ts.tv_usec);
    // printf("Offset:      %lu.%06lu\n",
    //                      offset->tv_sec,
    //                      offset->tv_usec);
  }

  // Assuming the variance is only at the microsecond level
  sum /= (double)ntests;
  offset->tv_sec = (long int)sum;
  offset->tv_usec = (long int)((sum - (int)sum) * 1000000);

  // Clean up ftrace
  fclose(tp);
  echo_to("set_event_pid", "");
  echo_to("set_event", "");
  
  return 0;
}
