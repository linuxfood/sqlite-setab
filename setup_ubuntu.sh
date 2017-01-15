#!/bin/bash
set -e

FOLLY_VERSION="2016.12.19.00"
FOLLY_DIR="/usr/local/folly-${FOLLY_VERSION}"

echo "This script configures ubuntu with everything needed to run setab."
echo "It requires that you run it as root. sudo works great for that."
apt update
apt install --yes \
    autoconf \
    autoconf-archive \
    automake \
    binutils-dev \
    cmake \
    g++ \
    git \
    libboost-all-dev \
    libdouble-conversion-dev \
    libdwarf-dev \
    libevent-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libjemalloc-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    libsqlite3-dev \
    libssl-dev \
    libtool \
    libunwind-dev \
    libzmq3-dev \
    make \
    pkg-config \
    wget \
    zlib1g-dev \

wget -O /tmp/folly-${FOLLY_VERSION}.tar.gz https://github.com/facebook/folly/archive/v${FOLLY_VERSION}.tar.gz
cd /tmp
tar xzvf folly-${FOLLY_VERSION}.tar.gz
cd folly-${FOLLY_VERSION}/folly

if [[ -e ${FOLLY_DIR} ]]; then
    echo "Moving aside existing folly directory.."
    mv -v ${FOLLY_DIR} ${FOLLY_DIR}.bak.$(date +%Y-%m-%d)
fi

autoreconf -ivf
./configure --prefix=${FOLLY_DIR}
make install

if [[ -L /usr/local/folly ]]; then
    echo "Removing existing folly symlink."
    rm /usr/local/folly
fi

if [[ -e /usr/local/folly && -d /usr/local/folly ]]; then
    echo "Skipping symlinking operation, /usr/local/folly is a directory."
    exit 1
fi

ln -s ${FOLLY_DIR} /usr/local/folly
