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

/// @brief Send a message to the server
/// @param client_fd the client's socket fd
/// @param network the server's socket address
/// @param text the message
/// @param length the length of the message, in bytes
/// @return OK/ERR
static int send_text(int client_fd, const struct sockaddr_in *network, const char *text, size_t length) 
{
    int res = sendto(client_fd, text, length, 0, (const struct sockaddr *)network, sizeof(*network));
    if (res < 0) {
        perror("sendto");
        return ERR;
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
    char buffer[BUFFER_SIZE];
    for (;;) {
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
            goto FAIL;
        }

        /* stdin is independent: send each available line immediately. */
        if (fds_watched[0].revents & (POLLIN | POLLHUP)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                break;
            }

            res = send_text(client_fd, &network, buffer, strlen(buffer));
            if (res < 0) {
                perror("send_text");
                goto FAIL;
            }
        }

        /* Echo reception is independent and never gates the next send. */
        if (fds_watched[1].revents & POLLIN) {
            ssize_t received = recvfrom(client_fd, buffer, sizeof(buffer), 0, NULL, NULL);
            if (received < 0 && errno == EINTR) {
                continue;
            } else if (received < 0) {
                perror("recvfrom");
                goto FAIL;
            }

            printf("echo: ");
            fwrite(buffer, 1, (size_t)received, stdout);
            if (received == 0 || buffer[received - 1] != '\n') {
                putchar('\n');
            }
            fflush(stdout);
        }
    }

    goto SUCCESS;

FAIL:
    res = EXIT_FAILURE;
SUCCESS:
    close(client_fd);
    return res;
}
