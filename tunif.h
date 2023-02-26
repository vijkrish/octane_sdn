#ifndef TUNIF
#define TUNIF

#define MAX_BUFFER_SIZE 1024

int tunnel_init(char *dev_name, int flags);
char *router_tun_receive(int tun_fd, int *msg_size);
void router_tun_send(int tun_fd, char *message, int msg_size);

#endif 
