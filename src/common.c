#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

ushort calc_checksum(void *data, int length) {
    // my protocol specifies 16 bit checksum
    int word_size = 2;
    ushort *words = (ushort *)data;

    // add each word together
    ulong checksum = 0;
    while (length >= word_size) {
        checksum += *words;
        words++;
        length -= word_size;
    }

    // add leftover byte if any
    if (length > 0) {
        checksum += (*(byte *)words) & 0xFF00;
    }

    // fold to fit word size
    checksum = (checksum >> 16) + (checksum & 0xFFFF);
    checksum = ~checksum;
    
    return (ushort)checksum;
}

byte checksum_is_valid(Packet *packet) {
    ushort claim = packet->header.checksum;
    
    packet->header.checksum = 0;
    ushort actual = calc_checksum(packet, packet_length(packet));
    packet->header.checksum = claim;

    return claim == actual;
}

void print_packet(Packet *packet) {
    printf("[SEQ %d, ACK %d]: %s", packet->header.seq, packet->header.ack, packet->payload);
}

size_t packet_length(Packet *packet) {
    return sizeof(packet->header) + strlen((char *)packet->payload) + 1;
}

int send_packet(int sender_fd, const struct sockaddr_in *peer, Header *header, const char *text) 
{
    int payload_length = strlen(text) + 1;
    if (payload_length > MSS) {
        payload_length = MSS;
    }

    // build packet
    Packet packet = { .header = *header };
    memcpy(packet.payload, text, payload_length);
    packet.payload[payload_length - 1] = '\0'; // ensure final character is '\0'

    // seal it with the checksum
    packet.header.checksum = calc_checksum(&packet, packet_length(&packet));

    // actually send it
    int res = sendto(sender_fd, &packet, packet_length(&packet), 0, (const struct sockaddr *)peer, sizeof(*peer));
    if (res < 0) {
        perror("sendto");
        return ERR;
    }

    return OK;
}

uint increment_seq_ack(uint seq_ack) {
    return (seq_ack + 1) % (WINDOW_LENGTH + 1);
}

void process_data(Packet *packet) {
    printf("received: ");
    print_packet(packet);
    printf("\n");
    // if (length == 0 || packet.payload[payload_len - 1] != '\n') {
    //     putchar('\n');
    // }
    fflush(stdout);
}

int receive_packet(int receiver_fd, void *buffer, struct sockaddr_in *peer, socklen_t *peer_length) {
    ssize_t length = recvfrom(receiver_fd, buffer, MSS, 0, (struct sockaddr *)peer, peer_length);
    if (length < 0) {
        perror("recvfrom");
        return ERR;
    }

    Packet packet = *(Packet *)buffer;

    byte corrupted = !checksum_is_valid(&packet);
    if (corrupted) {
        return ERR_CORRUPTED;
    }

    return length;
}

int order_packet(uint *ack, uint seq, Packet *packet, byte *buffer) {
    if (packet->header.seq > WINDOW_LENGTH) {
        // outside window
        return ERR_OUTSIDE_WINDOW;
    }

    if (packet->header.ack != seq) {
        return ERR_OUT_OF_ORDER;
    }

    buffer += packet->header.seq * MSS;
    size_t payload_length = strlen((char *)packet->payload) + 1;
    memcpy(buffer, packet->payload, payload_length);

    *ack = increment_seq_ack(*ack);
    return OK;
}