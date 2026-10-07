#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <listen-port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    unsigned short port = (unsigned short)strtoul(argv[1], NULL, 10);
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (bind(server_fd, (struct sockaddr *)&local, sizeof(local)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("UDP server listening on 127.0.0.1:%u\n", port);
    fflush(stdout);

    for (;;) {
        unsigned char buffer[BUFFER_SIZE];
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        ssize_t length = recvfrom(server_fd,
                                  buffer,
                                  sizeof(buffer),
                                  0,
                                  (struct sockaddr *)&peer,
                                  &peer_length);
        if (length == -1) {
            perror("recvfrom");
            continue;
        }

        printf("received: ");
        fwrite(buffer, 1, (size_t)length, stdout);
        if (length == 0 || buffer[length - 1] != '\n') {
            putchar('\n');
        }
        fflush(stdout);

        if (sendto(server_fd,
                   buffer,
                   (size_t)length,
                   0,
                   (struct sockaddr *)&peer,
                   peer_length) == -1) {
            perror("sendto");
        }
    }
}
