# 1. BUILD STAGE
FROM ubuntu:24.04 AS builder

# Install Build Dependencies
RUN apt-get update && apt-get install -y \
    clang \
    libbpf-dev \
    libcurl4-openssl-dev \
    libjson-c-dev \
    gcc \
    make \
    linux-tools-common \
    linux-tools-generic \
    linux-headers-generic \
    libelf-dev \
    zlib1g-dev \
    && ln -sf /usr/lib/linux-tools/*/bpftool /usr/sbin/bpftool

WORKDIR /app

# Copy Source Code
COPY . .

# Build
RUN make main

# 2. RUNTIME STAGE
FROM ubuntu:24.04

# Install Runtime Libraries AND curl (for the entrypoint script)
RUN apt-get update && apt-get install -y \
    libcurl4 \
    libjson-c5 \
    libelf1 \
    libbpf1 \
    zlib1g \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the binary and the entrypoint script
COPY --from=builder /app/main /usr/local/bin/kops-ai
COPY entrypoint.sh /usr/local/bin/

# Make the script executable
RUN chmod +x /usr/local/bin/entrypoint.sh

# Set Default Env Vars
ENV KOPS_MODEL="llama3.1:8b"
ENV KOPS_URL="http://host.docker.internal:11434/api/generate"

# Set the Entrypoint
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

# The CMD passes arguments to the ENTRYPOINT
CMD ["kops-ai"]