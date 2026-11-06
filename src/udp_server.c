#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#include "common.h"

#define ARG_COUNT 2

/// @brief Handles the parsing of command line arguments
/// @param argc number of arguments entered
/// @param argv the arguments themselves
/// @param listen_port_out where to write the parsed arg
/// @return OK/ERR
static int parse_args(int argc, char **argv, unsigned short *listen_port_out) 
{
    if (argc != ARG_COUNT) {
        return ERR;
    }

    *listen_port_out = (unsigned short)strtoul(argv[1], NULL, 10);

    return OK;
}

/// @brief Prints the usage/help message
/// @param program argv[0]
static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <listen-port>\n", program);
}

/// @brief Initializes the socket's file descriptor and local listening address, and also binds it.
/// @param port the port to listen on
/// @param server_fd where to write the socket fd 
/// @param sockaddr where to write the socket addr
/// @return OK/ERR
static int sock_init_local(unsigned short port, int *server_fd, struct sockaddr_in *sockaddr) 
{
    *server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*server_fd < 0) {
        perror("socket");
        return ERR;
    }

    sockaddr->sin_family = AF_INET; // IPv4
    sockaddr->sin_port = htons(port);
    sockaddr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int res = bind(*server_fd, (struct sockaddr *)sockaddr, sizeof(*sockaddr));
    if (res < 0) {
        perror("bind");
        return ERR;
    }

    return OK;
}

/// @brief Main server loop 
void main_loop(int server_fd) 
{
    int res = OK;

    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];
    uint seq = 0;
    uint ack = 0;

    byte gate[sizeof(Packet)];

    while (1) {
        // wait for data
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        ssize_t length = receive_packet(server_fd, gate, &peer, &peer_length);
        if (length == ERR_CORRUPTED) {
            printf("Corrupted data!\n");
        }

        Packet *packet = (Packet *)gate;
        if (length >= 0) {
            res = order_packet(&ack, seq, packet, (byte *)recv_buffer);
        }
        if (res == ERR_OUTSIDE_WINDOW) {
            printf("SEQ outside window!\n");
        } else if (res == ERR_OUT_OF_ORDER) {
            printf("Out of order ACK!\n");
        }
        
        char *payload;
        if (res < 0 && res == ERR_OUT_OF_ORDER) {
            seq = packet->header.ack;
            payload = send_buffer + (MSS * seq);
        } else if (res < 0 || length < 0) {
            seq = ack;
            payload = "";
        } else {
            process_data(packet);
            payload = (char *)packet->payload;
        }

        Header header = {
            .window_length = WINDOW_LENGTH,
            .seq = seq,
            .ack = ack,
        };
        memcpy(send_buffer + (MSS * header.seq), payload, strlen(payload));
        res = send_packet(server_fd, &peer, &header, payload);
        if (res < 0) {
            perror("sendto");
        } else {
            seq = increment_seq_ack(seq);
        }
    }
}

/// @brief Entry point
int main(int argc, char **argv)
{
    int res;

    // parse args
    unsigned short port;
    res = parse_args(argc, argv, &port);
    if (res < 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // init socket stuff
    int server_fd;
    struct sockaddr_in local = {0};
    res = sock_init_local(port, &server_fd, &local);
    if (res < 0) {
        close(server_fd);
        return EXIT_FAILURE;
    }

    // get ready to listen
    printf("UDP server listening on 127.0.0.1:%u\n", port);
    fflush(stdout);

    main_loop(server_fd);

    return 0;
}
