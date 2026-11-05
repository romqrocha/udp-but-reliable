#ifndef COMMON_H
#define COMMON_H

#define OK 0
#define ERR -1

#define WINDOW_LENGTH 1
#define MSS 1024
#define BUFFER_SIZE (WINDOW_LENGTH + 1) * MSS

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char byte;

typedef struct Header {
    ushort checksum;
    uint window_length;
    uint seq;
    uint ack;
} Header;

typedef struct Packet {
    Header header;
    byte payload[MSS];
} Packet;

/// @brief Calculates the checksum of my packet format
/// @param data a pointer to the start of the packet data
/// @param length the total length of the packet
/// @return the checksum as a 16-bit value
ushort calc_checksum(void *data, int length);

/// @brief Validates the checksum of some packet
/// @param packet the packet being validated
/// @return 1 if checksum is valid, 0 otherwise 
byte checksum_is_valid(Packet *packet);

/// @brief Prints the packet in readable format
/// @param packet the packet being printed
void print_packet(Packet *packet);

#endif
