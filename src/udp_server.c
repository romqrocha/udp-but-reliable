#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include "common.h"

#define ARG_COUNT 2

static uint last_ack = 0;

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

static int on_packet_received(Header *header, char *recv_buffer, char *send_buffer, int client_fd) 
{
    int res = OK;

    byte gate[sizeof(Packet)];
    Packet *packet = (Packet *)gate;
    struct sockaddr_in peer;
    socklen_t peer_length = sizeof(peer);

    ssize_t length = receive_packet(client_fd, gate, &peer, &peer_length);
    if (length < 0) {
        // drop packet
        // we'll request again on a timeout
        return length;
    }

    if (packet->header.flag_ack) {
        if (packet->header.ack == header->seq) {
            last_ack = packet->header.ack;
            // no need for a timeout on the packet with seq=packet->header.ack
        } else if (is_invalid_ack(packet->header.ack, last_ack, header->seq)) {
            // drop packet
        } else {
            last_ack = packet->header.ack;
            
            uint temp = header->seq;
            header->seq = packet->header.ack;
            header->flag_seq = TRUE;
            header->flag_ack = FALSE;
            res = send_packet(client_fd, &peer, header, send_buffer + (MSS * packet->header.ack));
            if (res < 0) {
                perror("send_packet");
            }
            header->seq = temp;
        }
    }

    if (packet->header.flag_seq) {
        // copy into buffer or drop
        char *buffer_dest;
        if (packet->header.seq == header->ack) {
            buffer_dest = recv_buffer + (MSS * packet->header.seq);

            size_t payload_length = strlen((char *)packet->payload) + 1;
            memcpy(buffer_dest, packet->payload, payload_length);
            
            header->ack = increment_seq_ack(header->ack);
        } else {
            buffer_dest = NULL;
        }

        // acknowledge
        header->flag_seq = FALSE;
        header->flag_ack = TRUE;
        res = send_packet(client_fd, &peer, header, NULL);
        if (res < 0) {
            perror("send_packet");
        }

        // process if not dropped
        if (buffer_dest) {
            process_data(buffer_dest);

            // reply
            size_t payload_length = strlen((char *)packet->payload) + 1;
            memcpy(send_buffer + (MSS * header->seq), packet->payload, payload_length);

            header->flag_seq = TRUE;
            header->flag_ack = FALSE;
            res = send_packet(client_fd, &peer, header, (char *)packet->payload);
            if (res < 0) {
                perror("send_packet");
                return ERR;
            }            
        }
    }

    // char *payload;
    // if (res < 0 && res == ERR_OUT_OF_ORDER) {
    //     seq = packet->header.ack;
    //     payload = send_buffer + (MSS * seq);
    //     printf("Resending SEQ %d\n", seq);
    // } else if (res < 0 || length < 0) {
    //     payload = "";
    //     seq = ack;
    //     printf("Requesting server's SEQ %d again!\n", ack);
    // } else {
    //     process_data(packet);
    //     return OK;
    // }

    // res = send_packet(client_fd, &peer, &header, payload);

    return res;
}

/// @brief Main server loop 
void main_loop(struct pollfd *watched_fd, int server_fd) 
{
    int res = OK;

    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];
    Header curr_header = make_header(WINDOW_LENGTH, NULL, NULL);

    while (1) {
        // clear revents
        watched_fd->revents = 0;

        // wait for poll event
        int ready = poll(watched_fd, 1, -1);
        if (ready < 0 && errno == EINTR) {
            continue;
        } else if (ready < 0) {
            perror("poll");
            break;
        }

        if (watched_fd->revents & POLLIN) {
            res = on_packet_received(&curr_header, recv_buffer, send_buffer, server_fd);
            if (res < 0) {
                printf("Error sending packet %d\n", curr_header.seq);
            }
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

    struct pollfd watched_fd = { .fd = server_fd, .events = POLLIN, .revents = 0 };

    // get ready to listen
    printf("UDP server listening on 127.0.0.1:%u\n", port);
    fflush(stdout);

    main_loop(&watched_fd, server_fd);

    return 0;
}
