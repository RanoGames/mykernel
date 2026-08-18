#ifndef NET_H
#define NET_H
void net_init(void);
/* ICMP-ish ping: 127.0.0.1 always works (loopback). Others: demo RTT message. */
int net_ping(const char* host, int count);
#endif
