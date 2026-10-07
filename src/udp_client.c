#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s <network-ip> <network-port>\n", program);
}

static int send_text(int client_fd,
                     const struct sockaddr_in *network,
                     const char *text,
                     size_t length)
{
    if (sendto(client_fd,
               text,
               length,
               0,
               (const struct sockaddr *)network,
               sizeof(*network)) == -1) {
        perror("sendto");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    unsigned short port = (unsigned short)strtoul(argv[2], NULL, 10);
    struct sockaddr_in network = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    if (inet_pton(AF_INET, argv[1], &network.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    setvbuf(stdin, NULL, _IONBF, 0);
    printf("Type lines quickly, or paste several lines. Echoes print as they arrive.\n");
    printf("Ctrl+D exits.\n");

    struct pollfd watched[2] = {
        {
            .fd = STDIN_FILENO,
            .events = POLLIN,
            .revents = 0,
        },
        {
            .fd = client_fd,
            .events = POLLIN,
            .revents = 0,
        },
    };
    char buffer[BUFFER_SIZE];
    for (;;) {
        watched[0].revents = 0;
        watched[1].revents = 0;

        int ready = poll(watched, 2, -1);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            close(client_fd);
            return EXIT_FAILURE;
        }

        /* stdin is independent: send each available line immediately. */
        if (watched[0].revents & (POLLIN | POLLHUP)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                break;
            }

            size_t length = strlen(buffer);
            if (send_text(client_fd, &network, buffer, length) == -1) {
                close(client_fd);
                return EXIT_FAILURE;
            }
        }

        /* Echo reception is independent and never gates the next send. */
        if (watched[1].revents & POLLIN) {
            ssize_t received = recvfrom(client_fd,
                                        buffer,
                                        sizeof(buffer),
                                        0,
                                        NULL,
                                        NULL);
            if (received == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recvfrom");
                close(client_fd);
                return EXIT_FAILURE;
            }

            printf("echo: ");
            fwrite(buffer, 1, (size_t)received, stdout);
            if (received == 0 || buffer[received - 1] != '\n') {
                putchar('\n');
            }
            fflush(stdout);
        }
    }

    close(client_fd);
    return EXIT_SUCCESS;
}
