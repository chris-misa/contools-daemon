#PING="/local/repository/iputils/ping"

PING="sudo docker run --rm chrismisa/contools:ping"

$PING -4 -i 1 10.10.1.2 > /dev/null &
PING_PID=$!

sleep 2

./ftrace_dump > dummy_dump &
DUMP_PID=$!

sleep 10


kill -INT $DUMP_PID

sleep 2

kill -INT $PING_PID

