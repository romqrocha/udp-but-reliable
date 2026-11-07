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

static int retries = 0;
static uint last_ack = 0;

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

// ------------------------------------------------------------------------- //

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

static int on_user_input(Header *header, char *buffer, size_t max_length, int client_fd, 
    const struct sockaddr_in *peer_sock, long *timeouts) 
{
    int res = OK;

    if (fgets(buffer, max_length, stdin) == NULL) {
        return ERR_STDIN_CLOSED;
    }
    if (strlen(buffer) > 0) {
        buffer[strlen(buffer) - 1] = '\0'; // remove '\n'
    }
    

    header->flag_seq = TRUE;
    header->flag_ack = FALSE;
    res = send_packet(client_fd, peer_sock, header, buffer, timeouts);
    if (res < 0) {
        perror("send_packet");
    }

    return res;
}

static int on_packet_received(Header *header, char *recv_buffer, char *send_buffer, int client_fd, long *timeouts) 
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
            retries = 0;
        } else if (is_invalid_ack(packet->header.ack, last_ack, header->seq)) {
            // drop packet
        } else {
            last_ack = packet->header.ack;
            retries = 0;

            uint temp = header->seq;
            header->seq = packet->header.ack;
            header->flag_seq = TRUE;
            header->flag_ack = FALSE;
            res = send_packet(client_fd, &peer, header, send_buffer + (MSS * packet->header.ack), timeouts);
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
        res = send_packet(client_fd, &peer, header, NULL, NULL);
        if (res < 0) {
            perror("send_packet");
        }

        // process if not dropped
        if (buffer_dest) {
            process_data(buffer_dest);
        }
    }

    return res;
}

/// @brief Main client loop
static int main_loop(struct pollfd *fds_watched, int client_fd, const struct sockaddr_in *peer_sock) 
{
    int res = OK;

    long timeouts[WINDOW_LENGTH + 1] = {0};
    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];
    Header curr_header = make_header(WINDOW_LENGTH, NULL, NULL);

    while (1) {
        int next_wait_ms = -1;
        if (timeouts[last_ack] != 0) {
            next_wait_ms = (int)(timeouts[last_ack] - monotonic_ms);
            if (next_wait_ms < 0) {
                next_wait_ms = 0;
            }  
        }

        // clear revents
        for (int i = 0; i < NUM_FDS; i++) {
            fds_watched[i].revents = 0;
        }
        
        // wait for poll event
        int ready = poll(fds_watched, NUM_FDS, next_wait_ms);
        if (ready < 0 && errno == EINTR) {
            continue;
        } else if (ready < 0) {
            perror("poll");
            return ERR;
        }

        if (fds_watched[0].revents & (POLLIN | POLLHUP)) {
            char *buffer_ptr = send_buffer + (MSS * curr_header.seq);
            res = on_user_input(&curr_header, buffer_ptr, MSS, client_fd, peer_sock, timeouts);
            if (res == ERR_STDIN_CLOSED) {
                break;
            }
        }

        if (fds_watched[1].revents & POLLIN) {
            res = on_packet_received(&curr_header, recv_buffer, send_buffer, client_fd, timeouts);
            if (res < 0 && res != ERR_CORRUPTED) {
                printf("Error sending packet %d\n", curr_header.seq);
            }
        }

        if (increment_seq_ack(curr_header.seq) != last_ack) {
            // send any packets you may have pending
        }
        if (curr_header.seq != last_ack && timeouts[last_ack] <= monotonic_ms) {
            if (retries >= MAX_RETRIES) {
                perror("max retries exceeded");
                break;
            } else {
                retries++;
            }

            uint temp = curr_header.seq;
            curr_header.seq = last_ack;
            curr_header.flag_seq = TRUE;
            curr_header.flag_ack = FALSE;
            res = send_packet(client_fd, peer_sock, &curr_header, send_buffer + (MSS * curr_header.seq), timeouts);
            if (res < 0) {
                printf("Error resending packet %d\n", curr_header.seq);
            }
            curr_header.seq = temp;
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
