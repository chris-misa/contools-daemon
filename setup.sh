#!/bin/bash

#
# Set up nodes in multitarget 1 experiment
#

# Copy in config files
mkdir -p /etc/docker
cp config/daemon.json /etc/docker/
cp config/ndppd.conf /etc/

# Get dependencies for the iputils build
apt-get update
apt-get install -y libcap-dev libidn2-0-dev nettle-dev

# Get and build iputils
git clone https://github.com/iputils/iputils.git
pushd iputils
make
popd

# Set up docker
apt-get install -y \
  apt-transport-https \
  ca-certificates \
  curl \
  software-properties-common

curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add -

add-apt-repository \
  "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) \
  stable"

apt-get update
apt-get install -y docker-ce

make
