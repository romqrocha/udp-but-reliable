CC := gcc
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -O2

.PHONY: all build clean

all: build

build: network udp_server udp_client

network: src/network.c
	$(CC) $(CFLAGS) src/network.c -o build/network

udp_server: src/udp_server.c
	$(CC) $(CFLAGS) src/udp_server.c -o build/udp_server

udp_client: src/udp_client.c
	$(CC) $(CFLAGS) src/udp_client.c -o build/udp_client

clean:
	rm -rf build/*
