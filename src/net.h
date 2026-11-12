#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t rx_errors;
    uint32_t rx_dropped;
    uint32_t rx_overflows;
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_errors;
    uint32_t tx_dropped;
    uint32_t tx_underruns;
    uint32_t tx_timeouts;
} net_stats_t;

void net_init(void);
int  net_is_online(void);
void net_get_mac(uint8_t out_mac[6]);
void net_get_ip(uint8_t out_ip[4]);
void net_get_netmask(uint8_t out_mask[4]);
void net_get_gateway(uint8_t out_gw[4]);
void net_get_dns(uint8_t out_dns[4]);
int  net_set_config(const uint8_t ip[4], const uint8_t netmask[4], const uint8_t gw[4], const uint8_t dns[4]);
int  net_parse_ipv4(const char* s, uint8_t* out);

/* Real ICMP ping: works on 127.0.0.1 (loopback), IP addresses, and hostnames (via DNS) */
int  net_ping(const char* host, int count);

/* DNS resolution: queries DNS server at 10.0.2.3:53 via UDP */
int  net_dns_lookup(const char* hostname, uint8_t out_ip[4]);
int  net_dns(const char* hostname);

/* Network statistics and configuration dump (ifconfig / netstat) */
void net_get_stats(net_stats_t* out);
void net_dump_stats(void);
void net_ifconfig(void);

#endif
