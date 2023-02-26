#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

/* Socket Libraries */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <signal.h>

/* Local Libraries */
#include "config.h"
#include "tunif.h"
#include "packet_parser.h"

struct in_addr interface_addr = {0};

/* Networking */
#define PORT_ANY 0
#define INTERFACE_NAME "lo"
#define IDLE_TIMEOUT 15 
#define TUN_NAME "tun1"

enum router_order {
    router_order_primary,
    router_order_2
};

/* Global structure to hold router's FD, port, log file pointer and pid */
struct router_info
{
    int router_fd;
    int port;
    FILE *fp;
    pid_t pid;
} router_info[MAX_ROUTERS + 1];

/* Open a log file of format stage<stage-number>.r<router-number>.out */
void logger_init(int stage, int router_num)
{
    FILE *fp = NULL;
    char log_file[MAX_FILE_LEN] = {0};

    snprintf(log_file, MAX_FILE_LEN, "stage%d.r%d.out", stage, router_num);
    fp = fopen(log_file, "w");
    if (!fp) {
        printf("\n Unable to open config file %s - %s", log_file, strerror(errno));
        exit(1);
    }
    router_info[router_num].fp = fp; 
}

/* Get the IP for the given interface 
 * I/P - Interface name
 * O/P - IP corresponding to the given interface_name */
struct in_addr get_interface_addr(char * interface_name)
{
    int interface_fd = 0;
    struct ifreq interface = {0};

    interface_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (interface_fd < 0) {
        printf("\n Unable to create a socket for interface (%s) - %s", interface_name, strerror(errno));
        exit(1) ;
    }

    interface.ifr_addr.sa_family = AF_INET;
    strncpy(interface.ifr_name, INTERFACE_NAME, IFNAMSIZ-1);
    if (ioctl(interface_fd, SIOCGIFADDR, &interface) < 0) {
        printf("\n Error in ioctl - %s", strerror(errno));
    }

    return ((struct sockaddr_in *)&interface.ifr_addr)->sin_addr;
}

/* Initialize a router with given router_id 
 * - Open a UDP socket
 * - Assign a dynamic port
 * - Get the port number assigned 
 * - Log info */
void router_init(int router_id)
{
    int socket_fd = 0;
    struct sockaddr_in server_addr = {0};
    socklen_t len = 0;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        printf("\n Unable to open socket - %s. Aborting ", strerror(errno));
        exit(-1);
    }

    interface_addr = get_interface_addr(INTERFACE_NAME); 

    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, &interface_addr, sizeof(interface_addr));
    server_addr.sin_port = PORT_ANY;

    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        printf("\n Unable to bind to socket (%d) - %s", socket_fd, strerror(errno));
        exit(1);
    }

    len = sizeof(server_addr);

    if (getsockname(socket_fd, (struct sockaddr *)&server_addr, &len) < 0) {
        printf("\n Unable to get socket (%d) information - %s", socket_fd, strerror(errno));
        exit(1);
    } 

    router_info[router_id].router_fd = socket_fd;
    router_info[router_id].port = ntohs(server_addr.sin_port);

    if (router_id == router_order_primary) {
        fprintf(router_info[router_id].fp, "primary port: %d\n", router_info[router_id].port);
    } else {
        fprintf(router_info[router_order_primary].fp, 
                "router: %d, pid: %d, port: %d\n", router_id, getpid(), router_info[router_id].port); 
        fprintf(router_info[router_id].fp, 
                "router: %d, pid: %d, port: %d\n", router_id, getpid(), router_info[router_id].port);
        fflush(router_info[router_order_primary].fp);
    }
    fflush(router_info[router_id].fp);
} 

/* Send the buffer argument passed to socket socket_fd */
void router_ipc_send(int socket_fd, char * buffer, int msg_size, struct sockaddr_in dst)
{
    int sent_bytes = 0;

    if (!buffer) {
        printf("\n Buffer is empty");
        return;
    }

    sent_bytes = sendto(socket_fd, buffer, msg_size, 0, (const struct sockaddr *) &dst, sizeof(dst)); 
    if (sent_bytes < 0) {
        printf("\n Error in sending message on socket (%d) - %s", socket_fd, strerror(errno));
    }
}

/* Receive message from socket fd of router <router_id>
 * I/P - Router ID
 * O/P - Message received and it's size
 * Caller should take care of free'ing the memory allocated */
char* router_ipc_receive(int router_id, int *msg_size)
{
    socklen_t len = 0;
    int recv_bytes = 0;
    char buffer[MAX_BUFFER_SIZE] = {0};
    struct sockaddr_storage  client_addr = {0};
    char *message = NULL;

    recv_bytes = recvfrom(router_info[router_id].router_fd, &buffer, 
            MAX_BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &len);
    buffer[recv_bytes] = '\0';

    message = (char *) malloc (recv_bytes + 1);
    if (!message) {
        printf("\n Unable to allocate memory - %s", strerror(errno));
        exit(-1);
    }
    memset(message, 0, recv_bytes + 1);
    memcpy(message, &buffer, recv_bytes);

    *msg_size = recv_bytes + 1;
    return message;
}

void set_sockaddr_details(struct sockaddr_in *sockaddr, int port)
{
    sockaddr->sin_family = AF_INET;
    memcpy(&sockaddr->sin_addr.s_addr, &interface_addr, sizeof(interface_addr));
    sockaddr->sin_port = htons(port);
}

void handle_other_routers(int router_id)
{
    fd_set router_fd_set;
    fd_set working_fd_set;
    int i = 0;
    int max_fd = 0;
    int ret = 0;
    struct sockaddr_in dst_sockaddr = {0};
    char *message = NULL;
    int msg_size = 0;
    char *src_ip = NULL;
    char *dst_ip = NULL;

    FD_ZERO(&router_fd_set);
    FD_SET(router_info[router_id].router_fd, &router_fd_set);
    max_fd = router_info[router_id].router_fd;

    while (1) {
        memcpy(&working_fd_set, &router_fd_set, sizeof(router_fd_set));
        ret = select(max_fd+1, &working_fd_set, NULL, NULL, NULL);
        if (ret == -1) {
            if (errno == EINTR) {
                printf("\n Received a Signal, gracefully shutdown");
                return;
            }
            printf("\n Unable to perform select operation - %s", strerror(errno));
            exit(-1);
        } else if (ret == 0) {
            printf("\n Socket (%d) has been idle for %d seconds", 
                    router_info[router_id].router_fd, IDLE_TIMEOUT);
            return;
        }

        for (i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &working_fd_set)) {
                if (i == router_info[router_id].router_fd) {

                    message = router_ipc_receive(router_id, &msg_size);

                    src_ip = get_src_addr(message);
                    dst_ip = get_dst_addr(message);
                    fprintf(router_info[router_id].fp, "ICMP from port: %d, src:"
                        " %s, dst: %s, type: %d\n", router_info[router_order_primary].port, 
                        src_ip, dst_ip, get_icmp_type(message));
 
                    form_echo_reply(message, msg_size);                   
                    set_sockaddr_details(&dst_sockaddr, router_info[router_order_primary].port);
                    router_ipc_send(router_info[router_order_primary].router_fd, message, msg_size, dst_sockaddr);
                    fflush(router_info[router_id].fp);
                    free(message);
                    free(src_ip);
                    free(dst_ip);
                }
            }

        }
        sleep(1);

    }

}

/* Primary router's action 
 * Listen on both the tunnel and socket (Primary->Secondary) FDs 
 * If tunnel FD is available:
 *      Read from tunnel
 *      Parse the request packet, extract source and destination address
 *      Send packet to secondary router
 * If socket FD is available:
 *      Read from socket FD (Primary <-> Secondary)
 *      Parse the response packet, extract source and destination address
 *      Write packet to tunnel */
void handle_primary_router(int pr_router_fd, int router_tun_fd)
{
    fd_set pr_router_fd_set;
    fd_set working_fd_set;
    int ret = 0;
    int i = 0;
    int max_fd = 0;
    struct timeval timeout = {0};
    char *message = NULL;
    struct sockaddr_in dst_sockaddr = {0};
    char *src_ip = NULL;
    char *dst_ip = NULL;
    int msg_size = 0;

    /* Add the tunnel fd and primary router's fd to fd_set */
    FD_ZERO(&pr_router_fd_set);
    FD_SET(router_tun_fd, &pr_router_fd_set);
    FD_SET(pr_router_fd, &pr_router_fd_set);

    if (pr_router_fd > router_tun_fd) {
        max_fd = pr_router_fd;
    } else {
        max_fd = router_tun_fd; 
    }

    while (1) {
        /* Set idle timeout to IDLE_TIMEOUT (15 seconds) */
        timeout.tv_sec = IDLE_TIMEOUT;
        timeout.tv_usec = 0;
        memcpy(&working_fd_set, &pr_router_fd_set, sizeof(pr_router_fd_set));

        ret = select(max_fd + 1, &working_fd_set, NULL, NULL, &timeout);
        if (ret == -1) {
            printf("\n Unable to perform select operation - %s", strerror(errno));
            exit(-1);
        } else if (ret == 0) {
            printf("\n Socket (%d) has been idle for %d seconds", pr_router_fd, IDLE_TIMEOUT);

            /* Send SIGHUP signal to the secondary router */
            kill(router_info[router_order_2].pid, SIGHUP);
            break;
        }

        for (i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i,  &working_fd_set)) {
                /* FD i has data ready to be read */

                if (i == router_tun_fd) {
                    /* Receive the message from tun device */
                    message = router_tun_receive(router_tun_fd, &msg_size);
                    if (!message) {
                        /* Non ICMP packet / Non ECHO packet */
                        continue;
                    }

                    src_ip = get_src_addr(message);
                    dst_ip = get_dst_addr(message);
                    fprintf(router_info[router_order_primary].fp, "ICMP from tunnel, src:"
                        " %s, dst: %s, type: %d\n", src_ip, dst_ip, get_icmp_type(message));

                    /* Send the ICMP packet to secondary router  */
                    set_sockaddr_details(&dst_sockaddr, router_info[router_order_2].port);
                    router_ipc_send(router_info[router_order_primary].router_fd, 
                        message, msg_size, dst_sockaddr);

                    /* Cleanup */
                    fflush(router_info[router_order_primary].fp);
                    free(message);
                } else if (i == pr_router_fd) {
                    /* Receive ICMP packet from secondary router */
                    message = router_ipc_receive(router_order_primary, &msg_size);

                    src_ip = get_src_addr(message);
                    dst_ip = get_dst_addr(message);
                    fprintf(router_info[router_order_primary].fp, "ICMP from port: %d, src: "
                            "%s, dst: %s, type: %d\n", router_info[router_order_2].port, 
                            src_ip, dst_ip, get_icmp_type(message));

                    /* Write ICMP packet to tunnel */
                    router_tun_send(router_tun_fd, message, msg_size);

                    /* Cleanup */
                    fflush(router_info[router_order_primary].fp);
                    free(message);
                }
                if (src_ip) {
                    free(src_ip);
                    src_ip = NULL;
                }
                if (dst_ip) {
                    free(dst_ip);
                    dst_ip = NULL;
                }
            }
        }
    }
}

/* Close router's log file and socket */
void cleanup(int router_id)
{
    printf("\n Cleaning up router %d", router_id);
    fprintf(router_info[router_id].fp, "router %d closed", router_id);
    fflush(router_info[router_id].fp);
    fclose(router_info[router_id].fp);
    close(router_info[router_id].router_fd);
}

/* Send a signal to secondary router and cleanup the */
void sighup()
{
    signal(SIGHUP,   sighup);
    cleanup(router_order_2);
}

/* Check if the received message is "I am up" from secondary router
 * Exit if so.  */
void handle_primary_router_stage_1()
{
    char *message = NULL;
    int msg_size = 0;

    while (1) {
        message = router_ipc_receive(router_order_primary, &msg_size);
        if (atoi(message) == router_info[router_order_2].pid) {
            printf("\n Received a logout message ");
            free(message);
            return;
        }
        sleep(1);
    }
}

/* Send an "I am up" message to primary router 
 * "I am up" message of router i will be PID(router i) 
 * Eg: If router 1 sends it's pid to primary router, 
 *     it is considered an "I am up" message from router 1 */
void handle_other_routers_stage_1(int router_id)
{
    char message[20] = {0};
    struct sockaddr_in dst_sockaddr = {0};

    set_sockaddr_details(&dst_sockaddr, router_info[router_order_primary].port);
    snprintf(message, sizeof(message), "%d", getpid());
    router_ipc_send(router_info[router_id].router_fd, message, strlen(message), dst_sockaddr);
}

void create_routers(int stage, int num_routers, int router_tun_fd)
{
    int i = 0;
    pid_t pid = 0;
        
    if (num_routers != 1) {
        printf("\n Number of routers must be 1 for now. Overriding the value to 1");
        num_routers = 1;
    }

    /* Initialize secondary router */
    for (i = 1; i <= num_routers; i++) { 
        logger_init(stage, num_routers);
        router_init(i);
    }

    pid = fork();
    if (pid > 0) {
        /* Store the pid of the secondary router (child process) */
        router_info[router_order_2].pid = pid;
        switch(stage) {
            case 1:
                handle_primary_router_stage_1();
                break;
            case 2:
                handle_primary_router(router_info[router_order_primary].router_fd, router_tun_fd);
                break;
            default:
                printf("\n Invalid stage number ");
        }       
        cleanup(router_order_primary);
    } else {
        /* Register for SIGHUP signal */
        signal(SIGHUP, sighup);
        switch(stage) {
            case 1:
                handle_other_routers_stage_1(router_order_2);
                break;
            case 2:
                handle_other_routers(router_order_2);
                break;
            default:
                printf("\n Invalid stage number ");
        }
    }
    
}

/* Usage - ./router <config-file> */
int main(int argc, char *argv[])
{
    char * config_file = NULL;
    int stage = 0;
    int num_routers = 0;
    int router_tun_fd = 0;

    if (argc <= 1) {
        printf("\n Usage \n ./router <config-file > ");
        return 0;
    }
    config_file = argv[1];

    /* Parse config file and set stage and num_routers */
    if (!parse_config_file(config_file, &stage, &num_routers)) {
        return 0;
    }

    printf("\n Stage = %d \n Number of router = %d", stage, num_routers);
    if ((stage <= 0) || (stage > MAX_STAGE)) {
        printf("\n Exiting as this stage (%d) is not we are supposed to run", stage);
        return 0;
    } 
   
    /* Initialize log files */ 
    logger_init(stage, router_order_primary);

    /* Initialize the primary router */
    router_init(router_order_primary);
    router_info[router_order_primary].pid = getpid();

    /* Initialize tun device */
    router_tun_fd = tunnel_init(TUN_NAME, IFF_TUN | IFF_NO_PI);
    if (router_tun_fd < 0) {
        printf("\n Unable to create a tunnel for %s", TUN_NAME);
        return 0;
    }

    /* Create the primary and secondary routers */
    create_routers(stage, num_routers, router_tun_fd);

    return 0;
}
