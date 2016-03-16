FROM pjameson/buck-folly-watchman:latest

# TODO: Figure out why this doesn't work.
#ARG ZMQ_SHA=96c9e4aabda5c040b29761638706a51f878a8bf0

RUN git clone https://github.com/zeromq/libzmq.git /usr/src/libzmq && \
    cd /usr/src/libzmq && \
    mkdir build && \
    cd build && \
    git checkout 96c9e4aabda5c040b29761638706a51f878a8bf0 && \
    ldconfig && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j2 && \
    make install && \
    make clean && \
    rm -rf /usr/src/libzmq/.git

COPY . /root/sqlite-setab

WORKDIR /root/sqlite-setab

RUN buck build //build:setup_ubuntu
RUN buck build //setab/...
