#include <stdio.h>

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
    ushort actual = calc_checksum(packet, sizeof(*packet));
    packet->header.checksum = claim;

    return claim == actual;
}

void print_packet(Packet *packet) {
    printf("[SEQ %d, ACK %d]: %s", packet->header.seq, packet->header.ack, packet->payload);
}