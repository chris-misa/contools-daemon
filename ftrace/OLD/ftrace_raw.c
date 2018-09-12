#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <pthread.h>

#define BUF_SIZE 0x1000

static int exiting = 0;

void usage()
{
  printf("ftrace_test [pid]\n");
}

void stop_running() {
  exiting = 1;
}

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

// Allocate and open a pipe to each cpu
void get_pipe_per_cpu(FILE **pipes, int ncpus, fd_set *fds)
{
  int i;
  char path[128];

  FD_ZERO(fds);
  
  for (i=0; i<ncpus; i++) {
    sprintf(path, "per_cpu/cpu%d/trace_pipe_raw", i);
    pipes[i] = (FILE *)malloc(sizeof(FILE *));
    pipes[i] = fopen(path, "r");
    if (!pipes[i]) {
      fprintf(stderr, "Failed to open %s\n", path);
    }
    FD_SET(fileno(pipes[i]), fds);
  }
}

// Close pipes
void release_pipe_per_cpu(FILE **pipes, int ncpus)
{
  int i;
  
  for (i=0; i<ncpus; i++) {
    if (pipes[i]) {
      fclose(pipes[i]);
    }
  }
}

// Pipe reading thread entrypoint
void *read_pipe(void *pipe)
{
  char buf[BUF_SIZE];
  size_t bytesRead;
  // Loop until the main thread kills us
  while (!exiting) {
    bytesRead = fread(buf, 1, BUF_SIZE, (FILE *)pipe);
    fwrite(buf, 1, bytesRead, stdout);
  }
}

int main(int argc, char *argv[])
{
  const char *tracefp = "/sys/kernel/debug/tracing";
  FILE **trace_pipes = NULL;
  fd_set tpfds, readfds;
  char buf[BUF_SIZE];
  int ncpus;
  int i;
  pthread_t *threads;

  ncpus = get_nprocs();
  trace_pipes = (FILE **)malloc(sizeof(FILE *) * ncpus);
  threads = (pthread_t *)malloc(sizeof(pthread_t) * ncpus);

  // Set exit trap
  signal(SIGINT, stop_running);

  // Move into tracing directory
  chdir(tracefp);

  // Enter desired events
  echo_to("current_tracer", "nop");
  echo_to("set_event", "syscalls:sys_enter_sendto syscalls:sys_exit_sendto syscalls:sys_enter_recvmsg syscalls:sys_exit_recvmsg");
  if (argc == 2) {
    echo_to("set_event_pid", argv[1]);
  } else {
    echo_to("set_event_pid", "");
  }

  echo_to("tracing_on", "1");
  echo_to("trace", "");

  get_pipe_per_cpu(trace_pipes, ncpus, &tpfds);
  
  // Spawn threads
  for (i = 0; i < ncpus; i++) {
    pthread_create(&threads[i], NULL, read_pipe, (void *)trace_pipes[i]);
  }

  // Main loop
  while (!exiting) {
    sleep(1);
  }

  // Kill our workers so we can close the files
  for (i = 0; i < ncpus; i++) {
    pthread_kill(threads[i], SIGINT);
  }
  for (i = 0; i < ncpus; i++) {
    pthread_join(threads[i], NULL);
  }

  release_pipe_per_cpu(trace_pipes, ncpus);

  // Reset ftrace state
  echo_to("tracing_on", "0");
  echo_to("set_event_pid", "");
  echo_to("set_event", "");


  printf("Done\n");

  return 0;
}
