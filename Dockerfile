FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    gdb \
    git \
    curl \
    wget \
    vim \
    nano \
    iproute2 \
    iputils-ping \
    iptables \
    strace \
    procps \
    psmisc \
    util-linux \
    libcap2-bin \
    libseccomp-dev \
    pkg-config \
    clang \
    llvm \
    libbpf-dev \
    linux-tools-common \
    linux-tools-generic \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace