//
// Helpful functions for dealing with ftrace system
//
// Using text-based interface as I don't currently have time
// to crack the binary interface and the overheads on packet
// latency seem to be similar anyway.
//
// 2018, Chris Misa
//

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>
#include <signal.h>

#include "libftrace.h"
#include "../time_common.h"

#define PROBE_PORT 34128
#define IOV_BUF_LEN 128
#define TRACE_BUFFER_SIZE 512

#define DEBUG

// Simply write into the given file and close
// Used for controlling ftrace via tracing filesystem
// Returns 1 if the write was successful, otherwise 0
int
echo_to(const char *file, const char *data)
{
  FILE *fp = fopen(file, "w");
  if (fp == NULL) {
    return 0;
  }
  if (fputs(data, fp) == EOF) {
    return 0;
  }
  fclose(fp);
  return 1;
}

// Read the contents of the given file
// Assumes data points to a buffer of size len
// Returns the number of bytes read or 0 on error
size_t
cat_from(const char *file, char *data, size_t len)
{
  FILE *fp = fopen(file, "r");
  if (fp == NULL) {
    return 0;
  }
  return fread((void *)data, 1, len, fp);
}

// Get an open file pointer to the trace_pipe
// and set things up in the tracing filesystem
// If anything goes wrong, returns NULL and resets things
FILE *
get_trace_pipe(const char *debug_fs_path,
               const char *target_events,
	       const char *pid,
	       const char *trace_clock)
{
  FILE *tp = NULL;
  if (chdir(debug_fs_path)) {
    fprintf(stderr, "Failed to get into tracing file path.\n");
    return NULL;
  }
  // If the first write fails, we probably don't have permissions so bail
  if (!echo_to("trace", "")) {
    fprintf(stderr, "Failed to write in tracing fs.\n");
    return NULL;
  }
  echo_to("trace", "");
  echo_to("current_tracer", "nop");
  if (trace_clock) {
    echo_to("trace_clock", trace_clock);
  }
  if (target_events) {
    echo_to("set_event", target_events);
  }
  if (pid) {
    echo_to("set_event_pid", pid);
  }

  echo_to("tracing_on", "1");

  tp = fopen("trace_pipe","r");
  
  if (!tp) {
    fprintf(stderr, "Failed to open trace pipe.\n");
    release_trace_pipe(NULL, debug_fs_path);
    return NULL;
  }
  
  return tp;
}

// Closes the pipe and turns things off in tracing filesystem
void
release_trace_pipe(FILE *tp, const char *debug_fs_path)
{
  if (tp) {
    fclose(tp);
  }
  if (chdir(debug_fs_path)) {
    fprintf(stderr, "Failed to get into tracing file path.\n");
    return;
  }
  echo_to("tracing_on", "0");
  echo_to("set_event_pid", "");
  echo_to("set_event", "");
}

// Skip space characters
void
parse_skip_whitespace(char **str)
{
  while(**str == ' ') {
    (*str)++;
  }
}

// Skip non-space character which we don't care about
void
parse_skip_nonwhitespace(char **str)
{
  while(**str != ' ') {
    (*str)++;
  }
}

// Parse dot-separated time into timeval
void
parse_timestamp(char **str, struct timeval *time)
{
  char *start = *str;
  time->tv_sec = strtoul(start, str, 10);
  start = *str + 1;
  time->tv_usec = strtoul(start, str, 10);
  // Skip trailing colon
  (*str)++;
}

// Parse the given field as a string
// Fields have form 'field_name=result'
void
parse_field(char **str, const char *field_name, char **result, int *result_len)
{
  const char *field_name_ptr = field_name;
  int len = 0;

find_field_name:
  while (*field_name_ptr != '\0' && **str != '\0') {
    if (**str == *field_name_ptr) {
      field_name_ptr++;
    } else {
      field_name_ptr = field_name;
    }
    (*str)++;
  }
  if (**str == '=') {
    (*str)++;
    while ((*str)[len] != '\0' && (*str)[len] != ' ') {
      len++;
    }
    *result = *str;
    *result_len = len;
    (*str) += len;
  } else {
    if (**str != '\0') {
      field_name_ptr = field_name;
      goto find_field_name;
    }
  }
}

// Get the function name assuming it is terminated by a colon
void
parse_function_name(char **str, char **result, int *result_len)
{
  int len = 0;

  while ((*str)[len] != '\0'
      && (*str)[len] != ':'
      && (*str)[len] != '(') {
    len++;
  }
  
  *result = *str;
  *result_len = len;

  (*str) += len;
}

// Parse a string into a newly allocated trace_event struct
// Returns NULL if anything goes wrong
void
trace_event_parse_str(char *str, struct trace_event *evt)
{
  evt->func_name = NULL;
  evt->func_name_len = 0;
  evt->dev = NULL;
  evt->dev_len = 0;
  evt->skbaddr = NULL;
  evt->skbaddr_len = 0;

  parse_skip_whitespace(&str);
  parse_skip_nonwhitespace(&str);           // Command and pid
  parse_skip_whitespace(&str);
  parse_skip_nonwhitespace(&str);           // CPU
  parse_skip_whitespace(&str);
  parse_skip_nonwhitespace(&str);           // Flags
  parse_skip_whitespace(&str);
  parse_timestamp(&str, &evt->ts);          // Time stamp
  parse_skip_whitespace(&str);
  parse_function_name(&str,
                      &evt->func_name,
                      &evt->func_name_len);    // Event type

  // Assume events are from net:* subsystem and have these fields
  parse_field(&str, "dev", &evt->dev, &evt->dev_len); // Device
  parse_field(&str, "skbaddr", &evt->skbaddr, &evt->skbaddr_len); // skb address
}

// Print the given event to stdout for debuging
void
trace_event_print(struct trace_event *evt)
{
  fprintf(stdout, "[%lu.%06lu] ", evt->ts.tv_sec, evt->ts.tv_usec);
  fprintf(stdout, "%s", evt->func_name);
  // Broken by the non-terminicity of these tokens. . .
  // actual will dump the rest of the buffer which is still useful
  // fprintf(stdout, " dev: %s skbaddr: %s\n", evt->dev, evt->skbaddr);
}

// "ping" loopback with UDP to put some packets through the netdev layer
// Calculates and stores the mean RTT in mean_rtt.
// If debug_fs_path is not NULL, writes markers into ftrace for
// synchronization with reading thread.
// Returns 0 on success
int
probe_loopback(int nprobes,
               long unsigned int *mean_rtt,
               char *debug_fs_path)
{
  char payload[] = "This is a probe";
  int payload_len;
  int sockfd;
  struct sockaddr_in target_addr;
  const socklen_t slen = sizeof(struct sockaddr_in);
  struct timeval send_time;
  struct timeval recv_time;

  struct msghdr reply_hdr;
  struct iovec reply_iov;
  ssize_t recv_len;

  char marker_buf[512];

  // counters
  int i;
  long unsigned int rtt_sum = 0;

  // Get payload size
  payload_len = strlen(payload);

  // Get a udp socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    fprintf(stderr, "probe_loopback failed to create probe socket\n");
    return -1;
  }

  // Set up the loopback address
  target_addr.sin_family = AF_INET;
  target_addr.sin_port = htons(PROBE_PORT);
  if (!inet_aton("0.0.0.0", &target_addr.sin_addr)) {
    fprintf(stderr, "probe_loopback failed to set address 0.0.0.0\n");
    return -1;
  }

  // Bind the socket to loopback
  bind(sockfd, (struct sockaddr *)&target_addr, slen);

  // Set up trace marker stuff if requested
  if (debug_fs_path) {
    if (chdir(debug_fs_path)) {
      fprintf(stderr, "Failed to get into tracing file path.\n");
      return NULL;
    }
  }

  // Send a probe and get RTT and possibly number of ftrace events
  for (i = 0; i < nprobes; i++) {

    // Insert send point trace marker
    if (debug_fs_path) {
      sprintf(marker_buf, "send %d", i);
      echo_to("trace_marker", marker_buf);
    }

    // Record send time
    gettimeofday(&send_time, NULL);

    // Send the packet
    if (sendto(sockfd, payload, payload_len, 0, (struct sockaddr *)&target_addr, slen) < 0) {
      fprintf(stderr, "probe_loopback sendto failed\n");
      return -1;
    }

#ifdef DEBUG
    fprintf(stderr, "[%lu.%06lu] Sent probe\n", send_time.tv_sec, send_time.tv_usec);
#endif

    // Set up the msghdr struct for receiving
    reply_iov.iov_base = malloc(IOV_BUF_LEN);
    reply_iov.iov_len = IOV_BUF_LEN;
    reply_hdr.msg_name = NULL;
    reply_hdr.msg_namelen = 0;
    reply_hdr.msg_iov = &reply_iov;
    reply_hdr.msg_iovlen = 1;
    reply_hdr.msg_control = NULL;
    reply_hdr.msg_controllen = 0;
    reply_hdr.msg_flags = 0;

    // Receive the packet
    if ((recv_len = recvmsg(sockfd, &reply_hdr, 0)) < 0) {
      fprintf(stderr, "probe_loopback recvmsg failed: %ld\n", recv_len);
      return -1;
    }

    // Record recv time
    gettimeofday(&recv_time, NULL);

    // Insert recv point trace marker
    if (debug_fs_path) {
      sprintf(marker_buf, "recv %d", i);
      echo_to("trace_marker", marker_buf);
    }
#ifdef DEBUG
    fprintf(stderr, "[%lu.%06lu] Recv probe, %lu bytes\n", recv_time.tv_sec, recv_time.tv_usec, recv_len);
#endif
    

    // Calculate RTT and add to sum
    tvsub(&recv_time, &send_time);
    rtt_sum += recv_time.tv_sec * 1000000 + recv_time.tv_usec;

#ifdef DEBUG
    fprintf(stderr, "RTT: %lu.%06lu sec\n", recv_time.tv_sec, recv_time.tv_usec);
#endif

    // Take a break before doing it again
    sleep(1);
  }

  // Done with the socket
  close(sockfd);

  // Calculate mean RTT
  *mean_rtt = rtt_sum / nprobes;

  // Return success
  return 0;
}

// Entry point for the thread to read ftrace events
// Must be killed, then the number of events is read
// from a global variable.
//
// The idea is to read the trace and count the number of events
// between the `send <n>` and `recv <n>` markers inserted by ping loop

struct count_ftrace_events_args {
  FILE **tp_p;
  int nprobes;
};

static float mean_num_ftrace_events;

void *
count_ftrace_events(void *arg_p)
{
  struct count_ftrace_events_args *args = NULL;
  char buf[TRACE_BUFFER_SIZE];
  FILE *tp = NULL;
  int curProbe = -1;
  int newProbe = -1;
  int event_counter = 0;
  unsigned long int event_sum = 0;
  const char *trace_mark_write = "tracing_mark_write: ";
  int trace_mark_write_len;
  const char *send_mark = "send ";
  int send_mark_len;
  const char *recv_mark = "recv ";
  int recv_mark_len;
  char *str_p;

  args = (struct count_ftrace_events_args *)arg_p;
  tp = *((FILE **)args->tp_p);

  if (!tp) {
    fprintf(stderr, "count_ftrace_events trace pipe arg is NULL\n");
    return NULL;
  }

  trace_mark_write_len = strlen(trace_mark_write);
  send_mark_len = strlen(send_mark);
  recv_mark_len = strlen(recv_mark);

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

  while (1) {
    if (fgets(buf, TRACE_BUFFER_SIZE, tp) != NULL) {
#ifdef DEBUG_TRACE
      fprintf(stderr, "FTRACE: %s", buf);
#endif
      event_counter++;
      if ((str_p = strstr(buf, trace_mark_write)) != NULL ) {
        str_p += trace_mark_write_len;
        if (!strncmp(str_p, send_mark, send_mark_len)) {
          event_counter = 0;
          curProbe = atoi(str_p + send_mark_len);
        } else if (!strncmp(str_p, recv_mark, recv_mark_len)) {
          newProbe = atoi(str_p + recv_mark_len);
          if (curProbe != -1 && curProbe == newProbe) {
            fprintf(stderr, "Got %d events\n", event_counter);
            event_sum += event_counter;
          }
          if (newProbe == args->nprobes - 1) {
            break;
          }
        }
      }
    }
  }
  mean_num_ftrace_events = (float)event_sum / (float)args->nprobes;
  fprintf(stderr, "Got mean events: %f\n", mean_num_ftrace_events);
}

// Estimate ftrace overhead by probing loopback's RTT with and without ftrace events enabled
// Returns the estimated number of microseconds per trace function call
// Returns 0 on failure
// Attempts to leave ftrace system in same state as when launched by
// saving and restoring `set_event`
#define SAVE_BUFFER 512
float
get_event_overhead(int nprobes, FILE **tp_p, const char *debug_fs_path)
{
  long unsigned int untraced_mean_rtt;
  long unsigned int traced_mean_rtt;
  char save_events[SAVE_BUFFER];
  pthread_t event_count_thread;

  struct count_ftrace_events_args count_thread_args;

  // Save trace state
  if (cat_from("set_event", &save_events, SAVE_BUFFER) == 0) {
    fprintf(stderr, "Failed to save events\n");
    return 0;
  }

  // Close trace pipe
  release_trace_pipe(*tp_p, debug_fs_path);

  // Get untraced RTT
  probe_loopback(nprobes, &untraced_mean_rtt, NULL);

#ifdef DEBUG
  fprintf(stderr, "Got untraced rtt: %lu usec\n", untraced_mean_rtt);
#endif
  
  // Re-instate trace pipe
  *tp_p = get_trace_pipe(debug_fs_path, save_events, NULL, NULL);

#ifdef DEBUG
  fprintf(stderr, "Re-started tracing\n");
#endif

  // It seems to take a second for ftrace system to actually start up
  // Wait for it here
  sleep(3);

  // Start counting events
  count_thread_args.tp_p = tp_p;
  count_thread_args.nprobes = nprobes;
  if (pthread_create(&event_count_thread, NULL, count_ftrace_events,
      (void *)&count_thread_args) != 0) {
    fprintf(stderr, "Failed to spawn counting thread\n");
    return 0;
  }
#ifdef DEBUG
  fprintf(stderr, "Started counting thread\n");
#endif

  // Get traced RTT
  probe_loopback(nprobes,
                 &traced_mean_rtt,
                 debug_fs_path);

#ifdef DEBUG
  fprintf(stderr, "Got traced rtt: %lu usec\n",
                  traced_mean_rtt);
#endif

  pthread_join(event_count_thread, NULL);
  fprintf(stderr, "Counting thread exited\n");
/*
  // Kill counting thread
  pthread_cancel(event_count_thread);
#ifdef DEBUG
  fprintf(stderr, "Stopped counting thread\n");
#endif
*/

  // Return difference in rtts divided by number of events captures
  return (float)(traced_mean_rtt - untraced_mean_rtt) / mean_num_ftrace_events;
}
