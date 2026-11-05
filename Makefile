CC := gcc
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -O2

.PHONY: all build clean

all: build
	build/network --listen-port 9000 --server-ip 127.0.0.1 --server-port 9001 --loss 0.10 --delay 0.30 --corrupt 0.5 --max-delay-ms 500 --seed 42

build: network udp_server udp_client

network: src/network.c
	$(CC) $(CFLAGS) -o build/network src/network.c

udp_server: src/udp_server.c src/common.c
	$(CC) $(CFLAGS) -o build/udp_server src/udp_server.c src/common.c

udp_client: src/udp_client.c src/common.c
	$(CC) $(CFLAGS) -o build/udp_client src/udp_client.c src/common.c

clean:
	rm -rf build/*
