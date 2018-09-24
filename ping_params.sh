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

# PING_ARGS="-D -i 1.0 -s 56"
# Argument sequence is an associative array
# between file suffixes and argument strings
declare -A ARG_SEQ=(
  ["i0.5s120_0.ping"]="-c 10 -i 0.5 -s 120"
  ["i1.0s120_0.ping"]="-c 10 -i 1.0 -s 120"
)

NATIVE_PING_CMD="${HOME}/contools-daemon/iputils/ping"
CONTAINER_PING_CMD="/iputils/ping"

PING_CONTAINER_IMAGE="chrismisa/contools:ping-ubuntu"
PING_CONTAINER_NAME="ping-container"

PAUSE_CMD="sleep 5"

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

for arg in ${!ARG_SEQ[@]}
do
  echo $arg >> file_list
  PING_ARGS=${ARG_SEQ[$i]}
  #
  # Native pings for control
  #
  echo $B Native control $B
  # Run ping in background
  $NATIVE_PING_CMD $PING_ARGS $TARGET_IPV4 \
    > native_control_${TARGET_IPV4}_${arg}.ping
  
  $PAUSE_CMD

  #
  # Container pings
  #
  echo $B Container control $B
  
  docker exec $PING_CONTAINER_NAME \
    $CONTAINER_PING_CMD $PING_ARGS $TARGET_IPV4 \
    > container_monitored_${TARGET_IPV4}_${arg}.ping
  echo "  container pinging. . ."

  $PAUSE_CMD

done

docker stop $PING_CONTAINER_NAME
docker rm $PING_CONTAINER_NAME
echo $B Stopped container $B

echo Done.
