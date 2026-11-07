#ifndef COMMON_H
#define COMMON_H

#define TRUE 1
#define FALSE 0

#define OK 0
#define ERR -1
#define ERR_CORRUPTED -2
#define ERR_OUTSIDE_WINDOW -3
#define ERR_OUT_OF_ORDER -4
#define ERR_STDIN_CLOSED -5

#define WINDOW_LENGTH 1
#define MSS 1024
#define BUFFER_SIZE (WINDOW_LENGTH + 1) * MSS

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char byte;

typedef struct Header {
    uint window_length;
    ushort checksum;
    byte flag_seq;
    byte flag_ack;
    uint seq;
    uint ack;
} Header;

Header make_header(uint window_length, uint *seq, uint *ack); 

typedef struct Packet {
    Header header;
    byte payload[MSS];
} Packet;

/// @brief Does some arbitrary processing of the packet (probably printing the payload) 
void process_data(char *payload);

/// @brief Returns the length of this packet in bytes
size_t packet_length(Packet *packet);

/// @brief Increments a SEQ Number, ACK Number, or similar by wrapping around the window length
/// @param seq_ack the number to be incremented
/// @return the incremented number
uint increment_seq_ack(uint seq_ack);

/// @brief Calculates the checksum of my packet format
/// @param data a pointer to the start of the packet data
/// @param length the total length of the packet
/// @return the checksum as a 16-bit value
ushort calc_checksum(void *data, int length);

/// @brief Validates the checksum of some packet
/// @param packet the packet being validated
/// @return 1 if checksum is valid, 0 otherwise
byte checksum_is_valid(Packet *packet);

/// @brief Checks if the received ACK number is invalid, given the last received ACK and the current SEQ number. 
/// @param received_ack the ACK number to validate 
/// @param last_ack the last received ACK number
/// @param curr_seq the current SEQ number
/// @return 1 if invalid, otherwise 0
byte is_invalid_ack(uint received_ack, uint last_ack, uint curr_seq);

/// @brief Send a message to a peer
/// @param sender_fd the socket fd for sending the message
/// @param network the peer's socket address
/// @param header header information for the packet being sent
/// @param text the payload
/// @return OK/ERR
int send_packet(int sender_fd, const struct sockaddr_in *peer, Header *header, const char *text);

/// @brief Receives a packet from any peer and validates its checksum
/// @param receiver_fd socket fd where packets are expected
/// @param buffer where to receive the packet
/// @param peer where to write the peer's address
/// @param peer_length where to write the length of the peer's address
/// @return Number of bytes received (>= 0) / ERR / ERR_CORRUPTED
int receive_packet(int receiver_fd, void *buffer, struct sockaddr_in *peer, socklen_t *peer_length);

#endif
