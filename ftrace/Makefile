all: latency parse_stream parse_stream_syscalls libftrace.o tests

tests: ftrace_dump test_ftrace_overhead

latency: latency.c libftrace.h libftrace.o
	gcc -O2 -o latency latency.c libftrace.o -lpthread

parse_stream: parse_stream.c libftrace.h libftrace.o
	gcc -O2 -o parse_stream parse_stream.c libftrace.o -pthread

parse_stream_syscalls: parse_stream_syscalls.c libftrace.h libftrace.o
	gcc -O2 -o parse_stream_syscalls parse_stream_syscalls.c libftrace.o -pthread

libftrace.o: libftrace.h libftrace.c
	gcc -O2 -c -o libftrace.o libftrace.c -lpthread

ftrace_dump: ftrace_dump.c libftrace.c libftrace.h
	gcc -o ftrace_dump ftrace_dump.c libftrace.o -lpthread

test_ftrace_overhead: test_ftrace_overhead.c libftrace.h libftrace.o
	gcc -o test_ftrace_overhead test_ftrace_overhead.c libftrace.o -lpthread

clean:
	rm -f latency libftrace.o ftrace_dump

