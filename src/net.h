#ifndef NET_H
#define NET_H

#include <stdint.h>

void net_init(void);
/* Real ICMP ping: works on 127.0.0.1 (loopback) and on real network via RTL8139 NIC */
int  net_ping(const char* host, int count);
int  net_is_online(void);
void net_get_mac(uint8_t out_mac[6]);
void net_get_ip(uint8_t out_ip[4]);

#endif
