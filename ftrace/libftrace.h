//
// Helpful functions for dealing with ftrace system
//

#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

#ifndef LIBFTRACE_H
#define LIBFTRACE_H

typedef FILE * trace_pipe_t;

int echo_to(const char *file, const char *data);

// Get an open file pointer to the trace_pipe
// and set things up in the tracing filesystem
// If anything goes wrong, returns NULL
trace_pipe_t get_trace_pipe(const char *debug_fs_path,
		                        const char *target_events,
		                        const char *pid,
		                        const char *trace_clock);

// Closes the pipe and turns things off in tracing filesystem
void release_trace_pipe(trace_pipe_t tp, const char *debug_fs_path);

// Reads a line / events from the given trace pipe into dest
// Up to len characters, returns the number of character read
int read_trace_pipe(char *dest, size_t len, trace_pipe_t tp);

// Structure used to hold timestamp and pointers into a parsed buffer
struct trace_event {
  struct timeval ts;
  char *func_name;
  int func_name_len;
  char *dev;
  int dev_len;
  char *skbaddr;
  int skbaddr_len;
};

// Parses the str into a trace_event struct
// The trave_event is a shallow representaiont:
// all strings in the trace_event struct still point to the original buffer.
void trace_event_parse_str(char *str, struct trace_event *evt);

// Print the given event to stdout for debuging
void trace_event_print(struct trace_event *evt);

// Estimate ftrace overhead by probing loopback's RTT with and without ftrace events enabled
// Returns the estimated number of microseconds per trace function call
// All parameters except nprobes forwarded to get_trace_pipe()
// Returns 0 on failure or if non-traced RTT is larger for whatever reason
// Must be called with ftrace system off for result to have any meaning
float get_event_overhead(const char *debug_fs_path,
                         const char *events,
                         const char *clock,
                         int nprobes);

#endif
