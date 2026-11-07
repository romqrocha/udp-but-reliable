#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

Header make_header(uint window_length, uint *seq, uint *ack)
{
    Header header = {
        .window_length = window_length,
        .flag_seq = seq != NULL,
        .seq = seq != NULL ? *seq : 0,
        .flag_ack = ack != NULL,
        .ack = ack != NULL ? *ack : 0,
        // checksum must be calculated later
    };
    return header;
}

ushort calc_checksum(void *data, int length)
{
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

size_t packet_length(Packet *packet) {
    return sizeof(packet->header) + strlen((char *)packet->payload) + 1;
}

uint increment_seq_ack(uint seq_ack) {
    return (seq_ack + 1) % (WINDOW_LENGTH + 1);
}

void process_data(char *payload) {
    (void)payload;
    // printf("received: %s\n", payload);
    // // if (length == 0 || packet.payload[payload_len - 1] != '\n') {
    // //     putchar('\n');
    // // }
    // fflush(stdout);
}

int send_packet(int sender_fd, const struct sockaddr_in *peer, Header *header, const char *text) 
{
    if (text == NULL) {
        text = "";
    }

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

    // [DEBUG] //
    printf("Sending: ");
    if (header->flag_seq) printf("[SEQ %d] ", header->seq);
    if (header->flag_ack) printf("[ACK %d] ", header->ack);
    printf("%s\n", (char *)packet.payload);
    // [DEBUG] //

    // actually send it
    int res = sendto(sender_fd, &packet, packet_length(&packet), 0, (const struct sockaddr *)peer, sizeof(*peer));
    if (res < 0) {
        perror("sendto");
        return ERR;
    } else if (header->flag_seq) {
        header->seq = increment_seq_ack(header->seq);
    }

    return OK;
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
        // [DEBUG] //
        printf("Received corrupted packet!\n");
        // [DEBUG] //
        
        return ERR_CORRUPTED;
    }

    // [DEBUG] //
    printf("Received: ");
    if (packet.header.flag_seq) printf("[SEQ %d] ", packet.header.seq);
    if (packet.header.flag_ack) printf("[ACK %d] ", packet.header.ack);
    printf("%s\n", (char *)packet.payload);
    // [DEBUG] //

    return length;
}

byte is_invalid_ack(uint received_ack, uint last_ack, uint curr_seq) {
    return (
        ((last_ack < curr_seq) && (received_ack < last_ack || received_ack > curr_seq)) ||
        ((last_ack > curr_seq) && (received_ack < last_ack && received_ack > curr_seq))
    );
}