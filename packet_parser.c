#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>

#include "packet_parser.h"
#include "checksum.h"

#define IPV4_STR_LEN 16
#define NUM_OCTETS 4
#define CHECKSUM_LENGTH 2

enum icmp_packet_format {
    icmp_protocol = 9,
    icmp_src_start = 12,
    icmp_dst_start = 16,
    icmp_msg_type = 20,
    icmp_checksum = 22,
};

/* Debug utility to print the message contents */
void packet_dump(char *message, int msg_size)
{
    int i = 0;
    printf("\n Dumping Message (Len = %ld) \n ----------------- \n", strlen(message));
    for (i = 0; i <= msg_size; i++) {
        printf("%x ", (int)message[i]);
        if (i % 16 == 0) {
            printf("\n");
        }
    }

}

/* Check if the packet given corresponds to ICMP or not */
bool is_protocol_icmp(char *buffer)
{
    if (!buffer) {
        printf("\n Empty buffer passed");
        return false;
    }

    if ((uint8_t)buffer[icmp_protocol] == IPPROTO_ICMP) {
        return true;
    }
    return false;
}

/* Check if the packet given corresponds to echo or response */
bool is_icmp_echo(char buffer[])
{
    if ((uint8_t)buffer[icmp_msg_type] == ICMP_ECHO) {
        return true;
    }
    return false;
}

/* Form an ICMP echo reply */
void form_echo_reply(char *message, size_t msg_size)
{
    int temp = 0;
    int i = 0;
    uint16_t checksum_val = 0;

    if (!message) {
        printf("\n Empty message received");
        return;
    }

    /* Swap the source and destination in the ICMP packet */
    while (i < NUM_OCTETS) {
        temp = message[icmp_src_start + i];
        message[icmp_src_start + i] = message[icmp_dst_start + i];
        message[icmp_dst_start + i] = temp;
        i++;
    }

    /* Set type as ICMP */
    message[icmp_msg_type] = ICMP_ECHOREPLY;

    memset(message + icmp_checksum, 0, CHECKSUM_LENGTH);
    /* API Copyright (c) 2019 by Guillermo Baltra */
    checksum_val = checksum(message + icmp_msg_type, msg_size-icmp_msg_type);
    memcpy(message + icmp_checksum , &checksum_val, sizeof(checksum_val));
}

/* Given a buffer, return the address corresponding to start_index 
 * start_index can either be source or destination address's starting byte */
char* parse_ip_addr(char buffer[], int start_index)
{
    char *ip = NULL;
    
    ip = (char *) calloc (1, IPV4_STR_LEN);
    if (ip == NULL) {
        printf("\n Unable to allocate memory for storing IP address");
        exit(1);
    } 
  
   snprintf(ip, IPV4_STR_LEN, "%u.%u.%u.%u", (uint8_t)buffer[start_index], (uint8_t)buffer[start_index+1], (uint8_t)buffer[start_index+2], (uint8_t)buffer[start_index+3]);

    return ip;
}

/* Get the ICMP message type (Echo / Reply) of the given message */    
int get_icmp_type(char *message)
{
    if (!message) {
        printf("\n Empty message received ptr passed");
        exit(-1);
    }
    return (int) message[icmp_msg_type];
}

/* Get the source address field from the given message */
char *get_src_addr(char buffer[])
{
    char *src_ip = NULL;
    src_ip = parse_ip_addr(buffer, icmp_src_start);
    return src_ip;
}

/* Get the destination address field from the given message */
char *get_dst_addr(char buffer[])
{
    char *dst_ip = NULL;
    dst_ip = parse_ip_addr(buffer, icmp_dst_start);
    return dst_ip;
}
