#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

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

    // main loop
    for (;;) {
        unsigned char buffer[BUFFER_SIZE];
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        ssize_t length = recvfrom(server_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&peer, &peer_length);
        if (length < 0) {
            perror("recvfrom");
            continue;
        }

        printf("received: ");
        fwrite(buffer, 1, (size_t)length, stdout);
        if (length == 0 || buffer[length - 1] != '\n') {
            putchar('\n');
        }
        fflush(stdout);

        res = sendto(server_fd, buffer, (size_t)length, 0, (struct sockaddr *)&peer, peer_length);
        if (res < 0) {
            perror("sendto");
        }
    }
}
