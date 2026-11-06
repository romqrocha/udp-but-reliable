#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

#define ERR -1
#define OK 0

#define ARG_COUNT 3
#define NUM_FDS 2

typedef struct Args {
    struct in_addr network_ip;
    unsigned short network_port;
} Args;

/// @brief Prints the usage/help message
/// @param program argv[0]
static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <network-ip> <network-port>\n", program);
}

/// @brief Handles parsing of command line arguments
/// @param argc number of arguments entered
/// @param argv the arguments themselves
/// @param args_out where to output the parsed arguments
/// @return OK/ERR
static int parse_args(int argc, char **argv, Args *args_out) 
{
    if (argc != ARG_COUNT) {
        return ERR;
    }

    int res = inet_pton(AF_INET, argv[1], &args_out->network_ip);
    if (res == 0) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return ERR;
    } else if (res < 0) {
        perror("inet_pton");
        return ERR;
    }

    args_out->network_port = (unsigned short)strtoul(argv[2], NULL, 10);

    return OK;
}

/// @brief Initializes the client's socket file descriptor and the peer's socket address
/// @param args the command line arguments
/// @param sockaddr where to write the peer's socket addr
/// @param client_fd where to write the client's socket fd
/// @return OK/ERR
static int sock_init(Args args, struct sockaddr_in *sockaddr, int *client_fd) 
{
    *client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*client_fd < 0) {
        perror("socket");
        return ERR;
    }

    sockaddr->sin_family = AF_INET; // IPv4
    sockaddr->sin_port = htons(args.network_port);
    sockaddr->sin_addr = args.network_ip;

    return OK;
}

/// @brief Main client loop
static int main_loop(struct pollfd *fds_watched, int client_fd, const struct sockaddr_in *peer_sock) 
{
    int res = OK;

    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];
    uint seq = 0;
    uint ack = 0;

    byte gate[sizeof(Packet)];

    while (1) {
        // clear revents
        for (int i = 0; i < NUM_FDS; i++) {
            fds_watched[i].revents = 0;
        }
        
        // wait for poll event
        int ready = poll(fds_watched, NUM_FDS, -1);
        if (ready < 0 && errno == EINTR) {
            continue;
        } else if (ready < 0) {
            perror("poll");
            return ERR;
        }

        /* stdin is independent: send each available line immediately. */
        if (fds_watched[0].revents & (POLLIN | POLLHUP)) {
            if (fgets(send_buffer + (MSS * seq), MSS, stdin) == NULL) {
                break;
            }

            Header header = {
                .window_length = WINDOW_LENGTH,
                .seq = seq, 
                .ack = ack,
            };
            res = send_packet(client_fd, peer_sock, &header, send_buffer + (MSS * seq));
            if (res < 0) {
                perror("send_packet");
                return ERR;
            }
            seq = increment_seq_ack(seq);
        }

        /* Echo reception is independent and never gates the next send. */
        if (fds_watched[1].revents & POLLIN) {
            struct sockaddr_in peer;
            socklen_t peer_length = sizeof(peer);
            ssize_t length = receive_packet(client_fd, gate, &peer, &peer_length);
            if (length == ERR_CORRUPTED) {
                printf("Received corrupted data!\n");
            }

            Packet *packet = (Packet *)gate;
            if (length >= 0) {
                res = order_packet(&ack, seq, packet, (byte *)recv_buffer);
            }

            char *payload;
            if (res < 0 && res == ERR_OUT_OF_ORDER) {
                seq = packet->header.ack;
                payload = send_buffer + (MSS * seq);
                printf("Resending SEQ %d\n", seq);
            } else if (res < 0 || length < 0) {
                payload = "";
                seq = ack;
                printf("Requesting server's SEQ %d again!\n", ack);
            } else {
                process_data(packet);
                continue;
            }

            Header header = {
                .window_length = WINDOW_LENGTH,
                .seq = seq,
                .ack = ack,
            };
            res = send_packet(client_fd, &peer, &header, payload);
            if (res < 0) {
                perror("sendto");
            } else {
                seq = increment_seq_ack(seq);
            }
        }
    }

    return OK;
}

/// @brief Entry point
int main(int argc, char **argv)
{
    int res = OK;

    // parse args
    Args args = {0};
    res = parse_args(argc, argv, &args);
    if (res < 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // init socket stuff
    struct sockaddr_in network = {0};
    int client_fd = -1;
    res = sock_init(args, &network, &client_fd);
    if (res < 0) {
        return EXIT_FAILURE;
    }

    // init poll structure
    struct pollfd fds_watched[NUM_FDS] = {
        { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
        { .fd = client_fd, .events = POLLIN, .revents = 0 },
    };

    // get ready for input 
    setvbuf(stdin, NULL, _IONBF, 0);
    printf("Type lines quickly, or paste several lines. Echoes print as they arrive.\n");
    printf("Ctrl+D exits.\n");

    // main loop
    res = main_loop(fds_watched, client_fd, &network);
    if (res != OK) {
        res = EXIT_FAILURE;
    }

    close(client_fd);
    return res;
}
