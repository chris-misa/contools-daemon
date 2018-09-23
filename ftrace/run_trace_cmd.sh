#!/bin/bash

#
# Test of ftrace/latency under varying load provided by iperf
#
# Apparently docker installation automatically sets up apparmor
# in newer versions. To jetisonit:
# Check status: sudo aa-status
# sudo systemctl disable apparmor.service --now
# sudo service apparmor teardown
# sudo aa-status
#
# (from https://forums.docker.com/t/can-not-stop-docker-container-permission-denied-error/41142/5)
#
B="----------------"


TARGET_IPV4="10.10.1.2"

PING_ARGS="-D -i 1.0 -s 56"

NATIVE_PING_CMD="${HOME}/contools-daemon/iputils/ping"
CONTAINER_PING_CMD="/iputils/ping"

PING_CONTAINER_IMAGE="chrismisa/contools:ping-ubuntu"
PING_CONTAINER_NAME="ping-container"

PAUSE_CMD="sleep 5"

# PING_PAUSE_CMD="sleep 500"
# PING_PAUSE_CMD="sleep 10"

MONITOR_CMD="trace-cmd record -e net:net_dev_xmit -e net:netif_receive_skb -C global --date"
PARSE_STREAM_CMD="$(pwd)/parse_stream $(pwd)/latency.conf"

DATE_TAG=`date +%Y%m%d%H%M%S`
META_DATA="Metadata"

# declare -a IPERF_ARGS=("1M" "3M" "10M" "32M" "100M" "316M" "1G" "3G" "10G")
# declare -a IPERF_ARGS=("nop" "1M" "10M" "100M" "1G" "10G")
# declare -a IPERF_ARGS=("nop" "500K" "1M" "100M" "1G" "10G")
declare -a IPERF_ARGS=("nop" "3M")

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

for arg in ${IPERF_ARGS[@]}
do
  echo $arg >> file_list
  # Start iperf
  if [ $arg != "nop" ]
  then
    iperf -c $TARGET_IPV4 -d -i 100 -b $arg -t 0 \
      > ${arg}.iperf &
    IPERF_PID=$!
  fi
  #
  # Native pings for control
  #
  echo $B Native control $B
  # Run ping in background
  $NATIVE_PING_CMD $PING_ARGS $TARGET_IPV4 \
    > native_control_${TARGET_IPV4}_${arg}.ping &
  echo "  pinging. . ."
  
  $PAUSE_CMD
  
  PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
  echo "  got ping pid: $PING_PID"
  
  $PING_PAUSE_CMD
  
  kill -INT $PING_PID
  echo "  killed ping"
  
  $PAUSE_CMD
  
  #
  # Container pings with monitoring
  #
  echo $B Container monitored $B
  
  $MONITOR_CMD -o container_monitored_${TARGET_IPV4}_${arg}.dat &
  MONITOR_PID=$!
  echo "  trace-cmd record running with pid: ${MONITOR_PID}"
  
  $PAUSE_CMD
  
  docker exec $PING_CONTAINER_NAME \
    $CONTAINER_PING_CMD $PING_ARGS $TARGET_IPV4 \
    > container_monitored_${TARGET_IPV4}_${arg}.ping &
  echo "  container pinging. . ."
  
  $PAUSE_CMD
  
  PING_PID=`ps -e | grep ping | sed -E 's/ *([0-9]+) .*/\1/'`
  echo "  got container ping pid $PING_PID"
  
  $PING_PAUSE_CMD
  
  kill -INT $PING_PID
  echo "  killed ping"
  
  $PAUSE_CMD
  
  kill -INT $MONITOR_PID
  echo "  killed trace-cmd record"
  
  $PAUSE_CMD

  trace-cmd report container_monitored_${TARGET_IPV4}_${arg}.dat \
    > container_monitored_${TARGET_IPV4}_${arg}.trace

  $PARSE_STREAM_CMD \
    < container_monitored_${TARGET_IPV4}_${arg}.trace \
    > container_monitored_${TARGET_IPV4}_${arg}.latency

  echo "  converted trace to latency"

  if [ $arg != "nop" ]
  then
    kill -INT $IPERF_PID
    echo "  killed iperf"
  fi

  $PAUSE_CMD

done

docker stop $PING_CONTAINER_NAME
docker rm $PING_CONTAINER_NAME
echo $B Stopped container $B

echo Done.
