#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_PACKET_SIZE 65507
#define MAX_PENDING_PACKETS 64

struct pending_packet {
    bool used;
    size_t length;
    unsigned char data[MAX_PACKET_SIZE];
    struct sockaddr_in destination;
    int64_t send_at_ms;
    const char *direction;
};

static struct pending_packet pending[MAX_PENDING_PACKETS];
static int udp_fd = -1;
static double loss_rate;
static double delay_rate;
static double corrupt_rate;
static int max_delay_ms = 500;

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --listen-port PORT --server-ip IPV4 --server-port PORT [options]\n"
            "\n"
            "Required:\n"
            "  --listen-port PORT      port used by the client\n"
            "  --server-ip IPV4        real server IPv4 address\n"
            "  --server-port PORT      real server UDP port\n"
            "\n"
            "Network conditions (defaults shown):\n"
            "  --loss P                drop probability                 (0)\n"
            "  --delay P               random-delay probability         (0)\n"
            "  --corrupt P             payload-corruption probability   (0)\n"
            "  --max-delay-ms MS       maximum simulated delay          (500)\n"
            "  --seed N                repeatable random seed            (current time)\n"
            "  --help                  show this help\n"
            "\n"
            "P must be in [0, 1], and MS must be in [0, 60000].\n",
            program);
}

static int64_t now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static double random_unit(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

static void corrupt_packet(unsigned char *data,
                           size_t length,
                           const char *direction)
{
    size_t byte_index = (size_t)rand() % length;
    unsigned int bit_index = (unsigned int)rand() % 8U;

    data[byte_index] ^= (unsigned char)(1U << bit_index);
    printf("CORRUPT     %-4s %5zu bytes, byte=%zu bit=%u\n",
           direction,
           length,
           byte_index,
           bit_index);
    fflush(stdout);
}

static bool same_endpoint(const struct sockaddr_in *a,
                          const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static unsigned short parse_port(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value == 0 || value > 65535) {
        fprintf(stderr, "Invalid port: %s\n", text);
        exit(EXIT_FAILURE);
    }

    return (unsigned short)value;
}

static double parse_probability(const char *name, const char *text)
{
    char *end = NULL;
    double value = strtod(text, &end);

    if (*text == '\0' || *end != '\0' || value < 0.0 || value > 1.0) {
        fprintf(stderr, "%s must be in [0, 1]: %s\n", name, text);
        exit(EXIT_FAILURE);
    }

    return value;
}

static int parse_milliseconds(const char *name, const char *text)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value < 0 || value > 60000) {
        fprintf(stderr, "%s must be in [0, 60000]: %s\n", name, text);
        exit(EXIT_FAILURE);
    }

    return (int)value;
}

static unsigned int parse_seed(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value > UINT32_MAX) {
        fprintf(stderr, "Invalid seed: %s\n", text);
        exit(EXIT_FAILURE);
    }

    return (unsigned int)value;
}

static const char *option_value(int argc,
                                char **argv,
                                int *index,
                                const char *option)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "%s requires a value\n", option);
        exit(EXIT_FAILURE);
    }

    ++*index;
    return argv[*index];
}

static void print_endpoint(const struct sockaddr_in *address,
                           char *output,
                           size_t output_size)
{
    char ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) == NULL) {
        snprintf(ip, sizeof(ip), "?");
    }

    snprintf(output, output_size, "%s:%u", ip, ntohs(address->sin_port));
}

static void send_packet(const unsigned char *data,
                        size_t length,
                        const struct sockaddr_in *destination,
                        const char *direction,
                        const char *action)
{
    ssize_t sent = sendto(udp_fd,
                          data,
                          length,
                          0,
                          (const struct sockaddr *)destination,
                          sizeof(*destination));

    if (sent == -1) {
        perror("sendto");
        return;
    }

    char target[64];
    print_endpoint(destination, target, sizeof(target));
    printf("%-17s %-4s %5zu bytes -> %s\n",
           action,
           direction,
           length,
           target);
    fflush(stdout);
}

static bool queue_packet(const unsigned char *data,
                         size_t length,
                         const struct sockaddr_in *destination,
                         int delay_ms,
                         const char *direction,
                         const char *action)
{
    for (size_t i = 0; i < MAX_PENDING_PACKETS; ++i) {
        if (pending[i].used) {
            continue;
        }

        pending[i].used = true;
        pending[i].length = length;
        memcpy(pending[i].data, data, length);
        pending[i].destination = *destination;
        pending[i].send_at_ms = now_ms() + delay_ms;
        pending[i].direction = direction;

        printf("%-17s %-4s %5zu bytes for %d ms\n",
               action,
               direction,
               length,
               delay_ms);
        fflush(stdout);
        return true;
    }

    printf("QUEUE-DROP  %-4s %5zu bytes, pending queue is full\n",
           direction,
           length);
    fflush(stdout);
    return false;
}

static void send_due_packets(void)
{
    int64_t now = now_ms();

    for (size_t i = 0; i < MAX_PENDING_PACKETS; ++i) {
        if (!pending[i].used || pending[i].send_at_ms > now) {
            continue;
        }

        send_packet(pending[i].data,
                    pending[i].length,
                    &pending[i].destination,
                    pending[i].direction,
                    "FORWARD");
        pending[i].used = false;
    }
}

static int next_timeout_ms(void)
{
    int64_t now = now_ms();
    int64_t nearest = -1;

    for (size_t i = 0; i < MAX_PENDING_PACKETS; ++i) {
        if (!pending[i].used) {
            continue;
        }

        int64_t remaining = pending[i].send_at_ms - now;
        if (remaining < 0) {
            remaining = 0;
        }
        if (nearest == -1 || remaining < nearest) {
            nearest = remaining;
        }
    }

    return nearest > INT32_MAX ? INT32_MAX : (int)nearest;
}

int main(int argc, char **argv)
{
    unsigned short listen_port = 0;
    unsigned short server_port = 0;
    const char *server_ip = NULL;
    bool have_listen_port = false;
    bool have_server_port = false;
    unsigned int seed = (unsigned int)time(NULL);

    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];

        if (strcmp(option, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(option, "--listen-port") == 0) {
            listen_port = parse_port(option_value(argc, argv, &i, option));
            have_listen_port = true;
        } else if (strcmp(option, "--server-ip") == 0) {
            server_ip = option_value(argc, argv, &i, option);
        } else if (strcmp(option, "--server-port") == 0) {
            server_port = parse_port(option_value(argc, argv, &i, option));
            have_server_port = true;
        } else if (strcmp(option, "--loss") == 0) {
            loss_rate = parse_probability("loss", option_value(argc, argv, &i, option));
        } else if (strcmp(option, "--delay") == 0) {
            delay_rate = parse_probability("delay", option_value(argc, argv, &i, option));
        } else if (strcmp(option, "--corrupt") == 0) {
            corrupt_rate = parse_probability("corrupt", option_value(argc, argv, &i, option));
        } else if (strcmp(option, "--max-delay-ms") == 0) {
            max_delay_ms = parse_milliseconds(
                "max-delay-ms", option_value(argc, argv, &i, option));
        } else if (strcmp(option, "--seed") == 0) {
            seed = parse_seed(option_value(argc, argv, &i, option));
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", option);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!have_listen_port || server_ip == NULL || !have_server_port) {
        fprintf(stderr,
                "--listen-port, --server-ip, and --server-port are required\n\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (delay_rate > 0.0 && max_delay_ms == 0) {
        fprintf(stderr,
                "max-delay-ms must be greater than 0 when delay is enabled\n");
        return EXIT_FAILURE;
    }
    srand(seed);

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
    };
    if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 server address: %s\n", server_ip);
        return EXIT_FAILURE;
    }

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(listen_port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (bind(udp_fd, (struct sockaddr *)&local, sizeof(local)) == -1) {
        perror("bind");
        close(udp_fd);
        return EXIT_FAILURE;
    }

    printf("network.c listening on 127.0.0.1:%u\n", listen_port);
    printf("forwarding to %s:%u\n", server_ip, server_port);
    printf("loss=%.2f delay=%.2f corrupt=%.2f max-delay=%dms seed=%u\n",
           loss_rate,
           delay_rate,
           corrupt_rate,
           max_delay_ms,
           seed);
    fflush(stdout);

    struct sockaddr_in client = {0};
    bool have_client = false;

    for (;;) {
        send_due_packets();

        struct pollfd watched = {
            .fd = udp_fd,
            .events = POLLIN,
            .revents = 0,
        };
        int ready = poll(&watched, 1, next_timeout_ms());
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (ready == 0) {
            continue;
        }

        unsigned char data[MAX_PACKET_SIZE];
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        ssize_t received = recvfrom(udp_fd,
                                    data,
                                    sizeof(data),
                                    0,
                                    (struct sockaddr *)&source,
                                    &source_length);
        if (received == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            continue;
        }

        const struct sockaddr_in *destination;
        const char *direction;
        if (same_endpoint(&source, &server)) {
            if (!have_client) {
                printf("DROP        S->C %5zd bytes, client is not known yet\n",
                       received);
                fflush(stdout);
                continue;
            }
            destination = &client;
            direction = "S->C";
        } else {
            client = source;
            have_client = true;
            destination = &server;
            direction = "C->S";
        }

        if (random_unit() < loss_rate) {
            printf("LOSS-DROP   %-4s %5zd bytes\n", direction, received);
            fflush(stdout);
            continue;
        }

        if (received > 0 && random_unit() < corrupt_rate) {
            corrupt_packet(data, (size_t)received, direction);
        }

        int packet_delay_ms = 0;
        if (random_unit() < delay_rate) {
            packet_delay_ms = 1 + rand() % max_delay_ms;
        }

        if (packet_delay_ms == 0) {
            send_packet(data,
                        (size_t)received,
                        destination,
                        direction,
                        "FORWARD");
        } else {
            queue_packet(data,
                         (size_t)received,
                         destination,
                         packet_delay_ms,
                         direction,
                         "DELAY");
        }
    }

    close(udp_fd);
    return EXIT_FAILURE;
}
