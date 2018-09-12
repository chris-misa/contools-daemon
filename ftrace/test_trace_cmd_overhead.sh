#!/bin/bash

#
# Test of ftrace_test overhead
# as implemented in trace-cmd
#
B="----------------"


TARGET_IPV4="10.10.1.2"

PING_ARGS="-i 1.0 -s 56"

NATIVE_PING_CMD="$(pwd)/iputils/ping"
# NATIVE_PING_CMD="${HOME}/Dep/iputils/ping"
CONTAINER_PING_CMD="/iputils/ping"

PING_CONTAINER_IMAGE="chrismisa/contools:ping"
PING_CONTAINER_NAME="ping-container"

PAUSE_CMD="sleep 5"

PING_PAUSE_CMD="sleep 10"

MONITOR_CMD="trace-cmd record -e net:*"

DATE_TAG=`date +%Y%m%d%H%M%S`
META_DATA="Metadata"

mkdir $DATE_TAG
cd $DATE_TAG

# Get some basic meta-data
echo "uname -a -> $(uname -a)" >> $META_DATA
echo "docker -v -> $(docker -v)" >> $META_DATA
echo "lsb_release -a -> $(lsb_release -a)" >> $META_DATA
echo "sudo lshw -> $(sudo lshw)" >> $META_DATA

# Start ping container as service
docker run -itd \
  --name=$PING_CONTAINER_NAME \
  --entrypoint=/bin/bash \
  $PING_CONTAINER_IMAGE
echo $B Started $PING_CONTAINER_NAME $B

$PAUSE_CMD

#
# Native pings for control
#
echo $B Native control $B
# Run ping in background
$NATIVE_PING_CMD $PING_ARGS $TARGET_IPV4 \
  > native_control_${TARGET_IPV4}.ping &
echo "  pinging. . ."

$PAUSE_CMD

PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
echo "  got ping pid: $PING_PID"

$PING_PAUSE_CMD

kill -INT $PING_PID
echo "  killed ping"

$PAUSE_CMD

#
# Container pings for control
#
echo $B Container control $B
docker exec $PING_CONTAINER_NAME \
  $CONTAINER_PING_CMD $PING_ARGS $TARGET_IPV4 \
  > container_control_${TARGET_IPV4}.ping &
echo "  pinging. . ."

$PAUSE_CMD

PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
echo "  got ping pid: $PING_PID"

$PING_PAUSE_CMD

kill -INT $PING_PID
echo "  killed ping"

$PAUSE_CMD

#
# Native pings with monitoring
#
echo $B Native monitored $B

$NATIVE_PING_CMD $PING_ARGS $TARGET_IPV4 \
  > native_monitored_${TARGET_IPV4}.ping &
echo "  native pinging. . ."

$PAUSE_CMD

PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
echo "  got native ping pid $PING_PID"

$PAUSE_CMD

$MONITOR_CMD -P $PING_PID \
  -o native_monitored_${TARGET_IPV4}.dat &
MONITOR_PID=$!
echo "  monitor running with pid: ${MONITOR_PID}"

$PING_PAUSE_CMD

kill -INT $MONITOR_PID
echo "  killed monitor"

$PAUSE_CMD

kill -INT $PING_PID
echo "  killed ping"

$PAUSE_CMD

# Container pings with monitoring
echo $B Container monitored $B

docker exec $PING_CONTAINER_NAME \
  $CONTAINER_PING_CMD $PING_ARGS $TARGET_IPV4 \
  > container_monitored_${TARGET_IPV4}.ping &
echo "  container pinging. . ."

$PAUSE_CMD

PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
echo "  got container ping pid $PING_PID"

$PAUSE_CMD

$MONITOR_CMD -P $PING_PID \
  -o container_monitored_${TARGET_IPV4}.dat &
MONITOR_PID=$!
echo "  monitor running with pid: ${MONITOR_PID}"

$PING_PAUSE_CMD

kill -INT $MONITOR_PID
echo "  killed monitor"

$PAUSE_CMD

kill -INT $PING_PID
echo "  killed ping"

$PAUSE_CMD

docker stop $PING_CONTAINER_NAME
docker rm $PING_CONTAINER_NAME
echo $B Stopped container $B

echo Done.
