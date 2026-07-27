# Phase 1: Build stage using Ubuntu and GCC
FROM ubuntu:latest AS builder
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN gcc -Wall -Wextra -std=c99 -pthread client.c ui.c -o client

# Phase 2: Runtime stage on Ubuntu
FROM ubuntu:latest
WORKDIR /app
COPY --from=builder /app/client .
ENV TERM=xterm-256color
CMD ["./client", "server", "8888"]
