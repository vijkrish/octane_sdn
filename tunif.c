#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "packet_parser.h"
#include "tunif.h"

#define TUN_DEVICE "/dev/net/tun"

#define IPV4_STR_LEN 16

enum icmp_packet_format {
    icmp_src_start = 12,
    icmp_dst_start = 16
};

/* Allocate tunnel interface */
int tunnel_init(char *dev_name, int flags) 
{
    struct ifreq ifr = {0};
    int fd = -1;
    int ret = 0;

    if (!dev_name) {
        perror("\n Empty device name");
        return -1;
    }

    if((fd = open(TUN_DEVICE, O_RDWR)) < 0)  {
	    printf("\n Unable to open %s - %s", TUN_DEVICE, strerror(errno));
	    return fd;
    }

    ifr.ifr_flags = flags;
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);

    ret = ioctl(fd, TUNSETIFF, (void *)&ifr);
    if(ret < 0 ) {
        printf("Error performing ioctl on Tunnel (%s) with TUNSETIFF setting - %s", dev_name, strerror(errno));
	    close(fd);
	    return -1;
    }

    return fd;
}

/* Read data from tunnel (tun_fd) */
char *router_tun_receive(int tun_fd, int *msg_size) 
{
    char buffer[MAX_BUFFER_SIZE];
    int recv_bytes = 0;
    char *message = NULL;

    recv_bytes = read(tun_fd, buffer, MAX_BUFFER_SIZE);
    if (recv_bytes < 0) {
        printf("\n Error reading from tun fd (%d)", tun_fd);
        return NULL;
    }
    buffer[recv_bytes] = '\0';

    if (!is_protocol_icmp(buffer)) {
        printf("\n Received a non ICMP message"); 
        return NULL;
    }

    if (!is_icmp_echo(buffer)) {
        printf("\n Received ICMP message doesn't correspond to ECHO");
        return NULL;
    }

    message = (char *) malloc (recv_bytes+1);
    if (!message) {
        printf("\n Unable to allocate memory - %s", strerror(errno));
        exit(-1);
    }

    memset(message, 0, recv_bytes+1);
    memcpy(message, buffer, recv_bytes + 1);
    *msg_size = recv_bytes + 1;
    return message;
                        
}

/* Write the given message into tunnel (tun_fd) */
void router_tun_send(int tun_fd, char *message, int msg_size)
{
    int send_bytes = 0;

    if (!message) {
        printf("\n No message to send via tun device");
        return;
    }

    send_bytes = write(tun_fd, message, msg_size);
    if (send_bytes < 0) {
        printf("\n Error writing to tun fd (%d)", tun_fd);
        return;
    }
    printf("\n Sent message of length %d via tun device", send_bytes);
}
