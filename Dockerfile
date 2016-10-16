FROM ubuntu:latest
COPY . /root/sqlite-setab
WORKDIR /root/sqlite-setab

RUN /bin/bash setup_ubuntu.sh