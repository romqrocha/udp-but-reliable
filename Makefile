CC := gcc
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -O2

.PHONY: all build clean

all: build

build: network udp_server udp_client

network: network.c
	$(CC) $(CFLAGS) network.c -o network

udp_server: udp_server.c
	$(CC) $(CFLAGS) udp_server.c -o udp_server

udp_client: udp_client.c
	$(CC) $(CFLAGS) udp_client.c -o udp_client

clean:
	rm -f network udp_server udp_client
