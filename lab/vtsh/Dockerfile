FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gdb \
    git \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    linux-libc-dev \
    manpages-dev \
    clang-format \
    strace \
    sudo \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN update-ca-certificates

RUN apt-get update && apt-get install -y --no-install-recommends python3-pytest && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace

ENV CC=gcc CXX=g++
ENV PYTHONUNBUFFERED=1

CMD ["bash"]


