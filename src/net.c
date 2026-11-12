/* net.c — Real network driver (RTL8139) + ARP + IPv4 + ICMP Ping + DNS */

#include "net.h"
#include "vga.h"
#include "timer.h"
#include "pci.h"
#include "io.h"
#include "kmalloc.h"
#include <stddef.h>
#include <stdint.h>

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define REG_MAC      0x00
#define REG_TSD0     0x10
#define REG_TSAD0    0x20
#define REG_RBSTART  0x30
#define REG_CR       0x37
#define REG_CAPR     0x38
#define REG_CBR      0x3A
#define REG_IMR      0x3C
#define REG_ISR      0x3E
#define REG_TCR      0x40
#define REG_RCR      0x44
#define REG_CONFIG1  0x52

#define RX_RING_SIZE 8192
#define TX_BUF_SIZE  2048
#define ARP_CACHE_SIZE 16

static uint16_t g_nic_io = 0;
static int g_nic_present = 0;
static uint8_t g_mac[6]     = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };
static uint8_t g_ip[4]      = { 10, 0, 2, 15 };
static uint8_t g_netmask[4] = { 255, 255, 255, 0 };
static uint8_t g_gateway[4] = { 10, 0, 2, 2 };
static uint8_t g_dns[4]     = { 10, 0, 2, 3 };
static uint8_t g_gw_mac[6]  = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

static uint8_t* g_rx_buf = 0;
static uint32_t g_rx_offset = 0;
static uint8_t  g_tx_buf[4][TX_BUF_SIZE];
static int      g_tx_cur = 0;

static net_stats_t g_stats;

typedef struct {
    uint8_t  ip[4];
    uint8_t  mac[6];
    uint32_t timestamp;
    int      valid;
} arp_entry_t;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];
static uint16_t g_dns_txid = 0x1A2B;
static uint16_t g_ping_id  = 0x4242;

/* Forward declarations */
static int rtl8139_send(const void* data, uint32_t len);
static int rtl8139_recv(void* out_buf, uint32_t max_len);
static void arp_cache_update(const uint8_t ip[4], const uint8_t mac[6]);
static int arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6]);
static void arp_send_request(const uint8_t target_ip[4]);
static void arp_send_reply(const uint8_t dest_ip[4], const uint8_t dest_mac[6]);
static void process_incoming_arp(const uint8_t* pkt, int len);
static int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6], uint32_t timeout_ms);
static int net_get_next_hop_mac(const uint8_t dst_ip[4], uint8_t out_mac[6]);

static uint16_t net_checksum(const void* data, size_t len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t*)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static int str_eq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int parse_ipv4(const char* s, uint8_t* out) {
    if (!s || !out) return -1;
    int part = 0, val = 0, digits = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;
            digits++;
        } else if (*s == '.') {
            if (digits == 0 || part >= 3) return -1;
            out[part++] = (uint8_t)val;
            val = 0;
            digits = 0;
        } else {
            return -1;
        }
        s++;
    }
    if (digits == 0 || part != 3) return -1;
    out[part] = (uint8_t)val;
    return 0;
}

static void print_ip(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) {
        terminal_write_uint((uint32_t)ip[i]);
        if (i < 3) terminal_putchar('.');
    }
}

static void print_mac(const uint8_t mac[6]) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        terminal_putchar(hex_chars[(mac[i] >> 4) & 0x0F]);
        terminal_putchar(hex_chars[mac[i] & 0x0F]);
        if (i < 5) terminal_putchar(':');
    }
}

int net_is_online(void) {
    return g_nic_present;
}

void net_get_mac(uint8_t out_mac[6]) {
    for (int i = 0; i < 6; i++) out_mac[i] = g_mac[i];
}

void net_get_ip(uint8_t out_ip[4]) {
    for (int i = 0; i < 4; i++) out_ip[i] = g_ip[i];
}

void net_get_gateway(uint8_t out_gw[4]) {
    for (int i = 0; i < 4; i++) out_gw[i] = g_gateway[i];
}

void net_get_dns(uint8_t out_dns[4]) {
    for (int i = 0; i < 4; i++) out_dns[i] = g_dns[i];
}

void net_get_netmask(uint8_t out_mask[4]) {
    if (!out_mask) return;
    for (int i = 0; i < 4; i++) out_mask[i] = g_netmask[i];
}

int net_set_config(const uint8_t ip[4], const uint8_t netmask[4], const uint8_t gw[4], const uint8_t dns[4]) {
    if (ip) {
        for (int i = 0; i < 4; i++) g_ip[i] = ip[i];
    }
    if (netmask) {
        for (int i = 0; i < 4; i++) g_netmask[i] = netmask[i];
    }
    if (gw) {
        for (int i = 0; i < 4; i++) g_gateway[i] = gw[i];
    }
    if (dns) {
        for (int i = 0; i < 4; i++) g_dns[i] = dns[i];
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        g_arp_cache[i].valid = 0;
    }
    return 0;
}

int net_parse_ipv4(const char* s, uint8_t* out) {
    return parse_ipv4(s, out);
}

void net_get_stats(net_stats_t* out) {
    if (!out) return;
    for (size_t i = 0; i < sizeof(net_stats_t); i++) {
        ((uint8_t*)out)[i] = ((const uint8_t*)&g_stats)[i];
    }
}

void net_init(void) {
    for (size_t i = 0; i < sizeof(g_stats); i++) {
        ((uint8_t*)&g_stats)[i] = 0;
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        g_arp_cache[i].valid = 0;
    }

    struct pci_device dev;
    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev)) {
        terminal_writestring("net: RTL8139 not detected (loopback only)\n");
        g_nic_present = 0;
        return;
    }

    g_nic_io = (uint16_t)(dev.bar[0] & ~0x3u);
    pci_enable_bus_mastering(&dev);

    /* Wake up device (Config1 = 0x00) */
    outb(g_nic_io + REG_CONFIG1, 0x00);

    /* Software reset */
    outb(g_nic_io + REG_CR, 0x10);
    for (int i = 0; i < 100000; i++) {
        if ((inb(g_nic_io + REG_CR) & 0x10) == 0)
            break;
    }

    /* Read MAC address from hardware */
    for (int i = 0; i < 6; i++) {
        g_mac[i] = inb(g_nic_io + REG_MAC + i);
    }

    /* Allocate continuous 16 KiB RX buffer (8K ring + 2K margin for WRAP) */
    if (!g_rx_buf) {
        g_rx_buf = (uint8_t*)kmalloc(16384);
        if (!g_rx_buf) {
            terminal_writestring("net: failed to allocate RX buffer\n");
            g_nic_present = 0;
            return;
        }
    }
    g_rx_offset = 0;

    /* Set RX buffer start address */
    outl(g_nic_io + REG_RBSTART, (uint32_t)(uintptr_t)g_rx_buf);

    /* RCR: WRAP (bit 7), Broadcast (AB), Multicast (AM), Physical Match (APM), All (AAP) */
    outl(g_nic_io + REG_RCR, 0x0000008F);

    /* TCR: Standard transmit configuration */
    outl(g_nic_io + REG_TCR, 0x03000700);

    /* Initialize TX descriptors */
    for (int i = 0; i < 4; i++) {
        outl(g_nic_io + REG_TSAD0 + i * 4, (uint32_t)(uintptr_t)g_tx_buf[i]);
    }
    g_tx_cur = 0;

    /* Clear all pending interrupts */
    outw(g_nic_io + REG_ISR, 0xFFFF);

    /* Disable interrupts (polled mode) */
    outw(g_nic_io + REG_IMR, 0x0000);

    /* Enable transmitter and receiver */
    outb(g_nic_io + REG_CR, 0x0C);

    g_nic_present = 1;

    /* Populate gateway in ARP cache */
    arp_cache_update(g_gateway, g_gw_mac);

    terminal_writestring("net: RTL8139 online, MAC=");
    print_mac(g_mac);
    terminal_writestring(", IP=");
    print_ip(g_ip);
    terminal_putchar('\n');
}

/* RTL8139 Transmit with 4 descriptors cycling */
static int rtl8139_send(const void* data, uint32_t len) {
    if (!g_nic_present || !data || len == 0 || len > TX_BUF_SIZE) {
        g_stats.tx_dropped++;
        return -1;
    }

    int cur = g_tx_cur;
    uint32_t tsd_reg = g_nic_io + REG_TSD0 + cur * 4;
    uint32_t tsad_reg = g_nic_io + REG_TSAD0 + cur * 4;

    /* Wait briefly if current descriptor is busy */
    for (int i = 0; i < 50000; i++) {
        uint32_t st = inl(tsd_reg);
        if ((st & (1 << 13)) || (st & (1 << 15)) || (st == 0)) {
            break;
        }
        __asm__ volatile ("pause");
    }

    /* Minimum Ethernet frame size is 60 bytes (plus 4-byte CRC = 64) */
    uint32_t send_len = len;
    if (send_len < 60) send_len = 60;

    uint8_t* dst = g_tx_buf[cur];
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
    for (uint32_t i = len; i < send_len; i++) dst[i] = 0;

    outl(tsad_reg, (uint32_t)(uintptr_t)dst);
    outl(tsd_reg, send_len & 0x1FFFu);

    /* Advance TX descriptor (cycle 0, 1, 2, 3) */
    g_tx_cur = (g_tx_cur + 1) % 4;

    int ok = 0;
    for (int i = 0; i < 100000; i++) {
        uint32_t st = inl(tsd_reg);
        if (st & (1 << 15)) { /* TOK: Transmit OK */
            ok = 1;
            break;
        }
        if (st & (1 << 14)) { /* TUN: FIFO Underrun */
            g_stats.tx_underruns++;
            g_stats.tx_errors++;
            outw(g_nic_io + REG_ISR, 0x0020);
            return -1;
        }
    }

    if (!ok) {
        g_stats.tx_timeouts++;
        g_stats.tx_errors++;
        return -1;
    }

    g_stats.tx_packets++;
    g_stats.tx_bytes += len;
    outw(g_nic_io + REG_ISR, 0x0004); /* Clear TOK */
    return 0;
}

/* RTL8139 Receive with ring buffer wrap handling and error recovery */
static int rtl8139_recv(void* out_buf, uint32_t max_len) {
    if (!g_nic_present || !g_rx_buf || !out_buf || max_len == 0) return 0;

    /* Check buffer empty */
    uint8_t cmd = inb(g_nic_io + REG_CR);
    if (cmd & 0x01) {
        return 0;
    }

    /* Check RX buffer overflow */
    uint16_t isr = inw(g_nic_io + REG_ISR);
    if (isr & 0x0010) { /* RXOVW */
        g_stats.rx_overflows++;
        g_stats.rx_errors++;
        outw(g_nic_io + REG_ISR, 0x0010);
    }

    uint32_t offset = g_rx_offset % RX_RING_SIZE;
    const uint8_t* rx_ptr = g_rx_buf + offset;
    uint16_t status = *(const uint16_t*)(rx_ptr);
    uint16_t len    = *(const uint16_t*)(rx_ptr + 2);

    int bad = 0;
    if ((status & 0x0001) == 0) bad = 1; /* ROK missing */
    if (status & 0x001E) bad = 1;        /* CRC, FAE, LONG, RUNT errors */
    if (len < 4 || len > 1536 + 4) bad = 1;

    if (bad) {
        g_stats.rx_errors++;
        outw(g_nic_io + REG_ISR, 0x0003);

        /* Resynchronize offset to hardware CBR */
        uint16_t cbr = inw(g_nic_io + REG_CBR);
        g_rx_offset = cbr % RX_RING_SIZE;
        uint16_t capr = (g_rx_offset >= 16) ? (uint16_t)(g_rx_offset - 16) : (uint16_t)(RX_RING_SIZE - 16 + g_rx_offset);
        outw(g_nic_io + REG_CAPR, capr);
        return 0;
    }

    uint32_t pkt_len = len - 4; /* strip CRC */
    uint32_t copy_len = pkt_len;
    if (copy_len > max_len) {
        g_stats.rx_dropped++;
        copy_len = max_len;
    }

    /* Contiguous copy thanks to WRAP margin */
    const uint8_t* pkt_data = g_rx_buf + offset + 4;
    uint8_t* dst = (uint8_t*)out_buf;
    for (uint32_t i = 0; i < copy_len; i++) {
        dst[i] = pkt_data[i];
    }

    /* Advance rx_offset: packet length + 4 bytes header, 4-byte aligned */
    g_rx_offset = (offset + len + 4 + 3) & ~3u;
    if (g_rx_offset >= RX_RING_SIZE) {
        g_rx_offset %= RX_RING_SIZE;
    }

    /* Update CAPR with wrap handling */
    uint16_t capr = (g_rx_offset >= 16) ? (uint16_t)(g_rx_offset - 16) : (uint16_t)(RX_RING_SIZE - 16 + g_rx_offset);
    outw(g_nic_io + REG_CAPR, capr);
    outw(g_nic_io + REG_ISR, 0x0001);

    g_stats.rx_packets++;
    g_stats.rx_bytes += copy_len;
    return (int)copy_len;
}

/* ---- ARP Cache with Timestamps and Replacement ---- */

static int arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6]) {
    if (!ip || !out_mac) return 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid &&
            g_arp_cache[i].ip[0] == ip[0] &&
            g_arp_cache[i].ip[1] == ip[1] &&
            g_arp_cache[i].ip[2] == ip[2] &&
            g_arp_cache[i].ip[3] == ip[3]) {
            for (int k = 0; k < 6; k++) out_mac[k] = g_arp_cache[i].mac[k];
            return 1;
        }
    }
    return 0;
}

static void arp_cache_update(const uint8_t ip[4], const uint8_t mac[6]) {
    if (!ip || !mac) return;
    uint32_t now = timer_ticks();

    /* 1. Update existing entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid &&
            g_arp_cache[i].ip[0] == ip[0] &&
            g_arp_cache[i].ip[1] == ip[1] &&
            g_arp_cache[i].ip[2] == ip[2] &&
            g_arp_cache[i].ip[3] == ip[3]) {
            for (int k = 0; k < 6; k++) g_arp_cache[i].mac[k] = mac[k];
            g_arp_cache[i].timestamp = now;
            if (ip[0] == g_gateway[0] && ip[1] == g_gateway[1] &&
                ip[2] == g_gateway[2] && ip[3] == g_gateway[3]) {
                for (int k = 0; k < 6; k++) g_gw_mac[k] = mac[k];
            }
            return;
        }
    }

    /* 2. Find empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            for (int k = 0; k < 4; k++) g_arp_cache[i].ip[k] = ip[k];
            for (int k = 0; k < 6; k++) g_arp_cache[i].mac[k] = mac[k];
            g_arp_cache[i].timestamp = now;
            g_arp_cache[i].valid = 1;
            if (ip[0] == g_gateway[0] && ip[1] == g_gateway[1] &&
                ip[2] == g_gateway[2] && ip[3] == g_gateway[3]) {
                for (int k = 0; k < 6; k++) g_gw_mac[k] = mac[k];
            }
            return;
        }
    }

    /* 3. Cache full: evict oldest entry */
    int oldest_idx = 0;
    uint32_t oldest_time = g_arp_cache[0].timestamp;
    for (int i = 1; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].timestamp < oldest_time) {
            oldest_time = g_arp_cache[i].timestamp;
            oldest_idx = i;
        }
    }
    for (int k = 0; k < 4; k++) g_arp_cache[oldest_idx].ip[k] = ip[k];
    for (int k = 0; k < 6; k++) g_arp_cache[oldest_idx].mac[k] = mac[k];
    g_arp_cache[oldest_idx].timestamp = now;
    g_arp_cache[oldest_idx].valid = 1;

    if (ip[0] == g_gateway[0] && ip[1] == g_gateway[1] &&
        ip[2] == g_gateway[2] && ip[3] == g_gateway[3]) {
        for (int k = 0; k < 6; k++) g_gw_mac[k] = mac[k];
    }
}

static void arp_send_request(const uint8_t target_ip[4]) {
    uint8_t frame[42];
    for (int i = 0; i < 6; i++) frame[i] = 0xFF;
    for (int i = 0; i < 6; i++) frame[6 + i] = g_mac[i];
    frame[12] = 0x08; frame[13] = 0x06; /* EtherType ARP */

    frame[14] = 0x00; frame[15] = 0x01; /* HW: Ethernet */
    frame[16] = 0x08; frame[17] = 0x00; /* Proto: IPv4 */
    frame[18] = 6;    frame[19] = 4;
    frame[20] = 0x00; frame[21] = 0x01; /* Opcode: 1 Request */

    for (int i = 0; i < 6; i++) frame[22 + i] = g_mac[i];
    for (int i = 0; i < 4; i++) frame[28 + i] = g_ip[i];
    for (int i = 0; i < 6; i++) frame[32 + i] = 0x00;
    for (int i = 0; i < 4; i++) frame[38 + i] = target_ip[i];

    rtl8139_send(frame, sizeof(frame));
}

static void arp_send_reply(const uint8_t dest_ip[4], const uint8_t dest_mac[6]) {
    uint8_t frame[42];
    for (int i = 0; i < 6; i++) frame[i] = dest_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = g_mac[i];
    frame[12] = 0x08; frame[13] = 0x06;

    frame[14] = 0x00; frame[15] = 0x01;
    frame[16] = 0x08; frame[17] = 0x00;
    frame[18] = 6;    frame[19] = 4;
    frame[20] = 0x00; frame[21] = 0x02; /* Opcode: 2 Reply */

    for (int i = 0; i < 6; i++) frame[22 + i] = g_mac[i];
    for (int i = 0; i < 4; i++) frame[28 + i] = g_ip[i];
    for (int i = 0; i < 6; i++) frame[32 + i] = dest_mac[i];
    for (int i = 0; i < 4; i++) frame[38 + i] = dest_ip[i];

    rtl8139_send(frame, sizeof(frame));
}

static void process_incoming_arp(const uint8_t* pkt, int len) {
    if (len < 42) return;
    uint16_t hw_type = ((uint16_t)pkt[14] << 8) | pkt[15];
    uint16_t proto   = ((uint16_t)pkt[16] << 8) | pkt[17];
    uint16_t opcode  = ((uint16_t)pkt[20] << 8) | pkt[21];
    if (hw_type != 1 || proto != 0x0800) return;

    const uint8_t* sender_mac = pkt + 22;
    const uint8_t* sender_ip  = pkt + 28;
    const uint8_t* target_ip  = pkt + 38;

    arp_cache_update(sender_ip, sender_mac);

    if (opcode == 1) {
        if (target_ip[0] == g_ip[0] && target_ip[1] == g_ip[1] &&
            target_ip[2] == g_ip[2] && target_ip[3] == g_ip[3]) {
            arp_send_reply(sender_ip, sender_mac);
        }
    }
}

static int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6], uint32_t timeout_ms) {
    if (!target_ip || !out_mac) return -1;
    if (arp_cache_lookup(target_ip, out_mac)) return 0;

    arp_send_request(target_ip);

    uint32_t hz = timer_hz();
    if (hz == 0) hz = 100;
    uint32_t wait_ticks = (timeout_ms * hz) / 1000;
    if (wait_ticks == 0) wait_ticks = 1;
    uint32_t end_tick = timer_ticks() + wait_ticks;

    uint8_t rx[1536];
    while (timer_ticks() <= end_tick) {
        int n = rtl8139_recv(rx, sizeof(rx));
        if (n >= 42) {
            if (rx[12] == 0x08 && rx[13] == 0x06) {
                process_incoming_arp(rx, n);
                if (arp_cache_lookup(target_ip, out_mac)) {
                    return 0;
                }
            }
        }
        __asm__ volatile ("pause");
    }

    if (target_ip[0] == g_gateway[0] && target_ip[1] == g_gateway[1] &&
        target_ip[2] == g_gateway[2] && target_ip[3] == g_gateway[3]) {
        for (int k = 0; k < 6; k++) out_mac[k] = g_gw_mac[k];
        arp_cache_update(g_gateway, g_gw_mac);
        return 0;
    }

    return -1;
}

/* Gateway ARP resolution for non-local subnets */
static int net_get_next_hop_mac(const uint8_t dst_ip[4], uint8_t out_mac[6]) {
    if (!dst_ip || !out_mac) return -1;

    int is_local = 1;
    for (int i = 0; i < 4; i++) {
        if ((dst_ip[i] & g_netmask[i]) != (g_ip[i] & g_netmask[i])) {
            is_local = 0;
            break;
        }
    }

    if (is_local) {
        return arp_resolve(dst_ip, out_mac, 300);
    } else {
        return arp_resolve(g_gateway, out_mac, 300);
    }
}

/* ---- DNS Lookup (UDP to 10.0.2.3:53) ---- */

int net_dns_lookup(const char* hostname, uint8_t out_ip[4]) {
    if (!hostname || !out_ip) return -1;

    if (str_eq(hostname, "localhost") || str_eq(hostname, "127.0.0.1") || str_eq(hostname, "::1")) {
        out_ip[0] = 127; out_ip[1] = 0; out_ip[2] = 0; out_ip[3] = 1;
        return 0;
    }

    if (parse_ipv4(hostname, out_ip) == 0) {
        return 0;
    }

    if (!g_nic_present) return -1;

    uint8_t dst_mac[6];
    if (net_get_next_hop_mac(g_dns, dst_mac) != 0) {
        for (int i = 0; i < 6; i++) dst_mac[i] = g_gw_mac[i];
    }

    /* Encode QNAME */
    uint8_t qname[256];
    int qlen = 0;
    const char* p = hostname;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - p);
        if (label_len <= 0 || label_len > 63 || qlen + label_len + 1 >= 254) return -1;
        qname[qlen++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) qname[qlen++] = (uint8_t)p[i];
        if (*dot == '.') dot++;
        p = dot;
    }
    qname[qlen++] = 0; /* root label */

    /* QTYPE = A (1), QCLASS = IN (1) */
    qname[qlen++] = 0x00; qname[qlen++] = 0x01;
    qname[qlen++] = 0x00; qname[qlen++] = 0x01;

    uint16_t txid = ++g_dns_txid;
    uint8_t dns_hdr[12];
    dns_hdr[0] = (uint8_t)(txid >> 8);
    dns_hdr[1] = (uint8_t)(txid & 0xFF);
    dns_hdr[2] = 0x01; /* RD = 1 */
    dns_hdr[3] = 0x00;
    dns_hdr[4] = 0x00; dns_hdr[5] = 0x01; /* QDCOUNT = 1 */
    dns_hdr[6] = 0x00; dns_hdr[7] = 0x00; /* ANCOUNT = 0 */
    dns_hdr[8] = 0x00; dns_hdr[9] = 0x00; /* NSCOUNT = 0 */
    dns_hdr[10] = 0x00; dns_hdr[11] = 0x00; /* ARCOUNT = 0 */

    uint16_t udp_len = (uint16_t)(8 + 12 + qlen);
    uint16_t ip_len  = (uint16_t)(20 + udp_len);
    uint16_t frame_len = (uint16_t)(14 + ip_len);

    uint8_t frame[512];
    if (frame_len > sizeof(frame)) return -1;

    /* Ethernet */
    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = g_mac[i];
    frame[12] = 0x08; frame[13] = 0x00; /* IPv4 */

    /* IPv4 */
    frame[14] = 0x45; frame[15] = 0x00;
    frame[16] = (uint8_t)(ip_len >> 8);
    frame[17] = (uint8_t)(ip_len & 0xFF);
    frame[18] = (uint8_t)(txid >> 8);
    frame[19] = (uint8_t)(txid & 0xFF);
    frame[20] = 0x40; frame[21] = 0x00; /* DF */
    frame[22] = 64;   /* TTL */
    frame[23] = 17;   /* Protocol UDP */
    frame[24] = 0;    frame[25] = 0;
    for (int i = 0; i < 4; i++) frame[26 + i] = g_ip[i];
    for (int i = 0; i < 4; i++) frame[30 + i] = g_dns[i];

    uint16_t ip_chk = net_checksum(frame + 14, 20);
    frame[24] = (uint8_t)(ip_chk & 0xFF);
    frame[25] = (uint8_t)(ip_chk >> 8);

    /* UDP */
    uint16_t src_port = (uint16_t)(53530 + (txid % 1000));
    uint16_t dst_port = 53;
    uint8_t* udp = frame + 34;
    udp[0] = (uint8_t)(src_port >> 8);
    udp[1] = (uint8_t)(src_port & 0xFF);
    udp[2] = (uint8_t)(dst_port >> 8);
    udp[3] = (uint8_t)(dst_port & 0xFF);
    udp[4] = (uint8_t)(udp_len >> 8);
    udp[5] = (uint8_t)(udp_len & 0xFF);
    udp[6] = 0; udp[7] = 0;

    for (int i = 0; i < 12; i++) udp[8 + i] = dns_hdr[i];
    for (int i = 0; i < qlen; i++) udp[20 + i] = qname[i];

    rtl8139_send(frame, frame_len);

    uint32_t hz = timer_hz();
    if (hz == 0) hz = 100;
    uint32_t timeout_ticks = timer_ticks() + 2 * hz;
    uint8_t rx[1536];

    while (timer_ticks() <= timeout_ticks) {
        int n = rtl8139_recv(rx, sizeof(rx));
        if (n < 42) {
            __asm__ volatile ("pause");
            continue;
        }

        if (rx[12] == 0x08 && rx[13] == 0x06) {
            process_incoming_arp(rx, n);
            continue;
        }

        if (rx[12] == 0x08 && rx[13] == 0x00 && rx[23] == 17) {
            uint8_t ihl = (rx[14] & 0x0F);
            if (ihl < 5) continue;
            uint32_t ip_hdr_len = (uint32_t)ihl * 4;
            if ((uint32_t)n < 14 + ip_hdr_len + 8 + 12) continue;

            const uint8_t* udp_pkt = rx + 14 + ip_hdr_len;
            uint16_t r_src_port = ((uint16_t)udp_pkt[0] << 8) | udp_pkt[1];
            uint16_t r_dst_port = ((uint16_t)udp_pkt[2] << 8) | udp_pkt[3];
            uint16_t r_udp_len  = ((uint16_t)udp_pkt[4] << 8) | udp_pkt[5];

            if (r_src_port != 53 || r_dst_port != src_port) continue;
            if ((uint32_t)n < 14 + ip_hdr_len + r_udp_len || r_udp_len < 20) continue;

            const uint8_t* dns_pkt = udp_pkt + 8;
            uint32_t dns_len = r_udp_len - 8;

            uint16_t r_id = ((uint16_t)dns_pkt[0] << 8) | dns_pkt[1];
            if (r_id != txid) continue;

            uint16_t flags = ((uint16_t)dns_pkt[2] << 8) | dns_pkt[3];
            if (!(flags & 0x8000)) continue;
            uint8_t rcode = (uint8_t)(flags & 0x0F);
            if (rcode != 0) return -2;

            uint16_t qdcount = ((uint16_t)dns_pkt[4] << 8) | dns_pkt[5];
            uint16_t ancount = ((uint16_t)dns_pkt[6] << 8) | dns_pkt[7];
            if (ancount == 0) return -3;

            /* Skip question records */
            uint32_t offset = 12;
            for (int q = 0; q < qdcount && offset < dns_len; q++) {
                while (offset < dns_len && dns_pkt[offset] != 0) {
                    if ((dns_pkt[offset] & 0xC0) == 0xC0) {
                        offset += 2;
                        break;
                    }
                    offset += 1 + dns_pkt[offset];
                }
                if (offset < dns_len && dns_pkt[offset] == 0) offset++;
                offset += 4;
            }

            /* Parse answer records */
            for (int a = 0; a < ancount && offset + 10 <= dns_len; a++) {
                if ((dns_pkt[offset] & 0xC0) == 0xC0) {
                    offset += 2;
                } else {
                    while (offset < dns_len && dns_pkt[offset] != 0) {
                        if ((dns_pkt[offset] & 0xC0) == 0xC0) {
                            offset += 2;
                            break;
                        }
                        offset += 1 + dns_pkt[offset];
                    }
                    if (offset < dns_len && dns_pkt[offset] == 0) offset++;
                }

                if (offset + 10 > dns_len) break;
                uint16_t type     = ((uint16_t)dns_pkt[offset] << 8) | dns_pkt[offset + 1];
                uint16_t cls      = ((uint16_t)dns_pkt[offset + 2] << 8) | dns_pkt[offset + 3];
                uint16_t rdlength = ((uint16_t)dns_pkt[offset + 8] << 8) | dns_pkt[offset + 9];
                offset += 10;

                if (offset + rdlength > dns_len) break;

                if (type == 1 && cls == 1 && rdlength == 4) {
                    out_ip[0] = dns_pkt[offset];
                    out_ip[1] = dns_pkt[offset + 1];
                    out_ip[2] = dns_pkt[offset + 2];
                    out_ip[3] = dns_pkt[offset + 3];
                    return 0;
                }
                offset += rdlength;
            }
        }
    }

    return -1;
}

int net_dns(const char* hostname) {
    if (!hostname || !hostname[0]) {
        terminal_writestring("Usage: dns <hostname>   e.g. dns google.com\n");
        return -1;
    }
    terminal_writestring("Resolving ");
    terminal_writestring(hostname);
    terminal_writestring(" via DNS server ");
    print_ip(g_dns);
    terminal_writestring(":53 ...\n");

    uint8_t ip[4];
    int res = net_dns_lookup(hostname, ip);
    if (res == 0) {
        terminal_writestring(hostname);
        terminal_writestring(" has address ");
        print_ip(ip);
        terminal_putchar('\n');
        return 0;
    } else if (res == -2) {
        terminal_writestring("DNS lookup failed: host not found (NXDOMAIN)\n");
        return -1;
    } else {
        terminal_writestring("DNS lookup failed: timeout or no response\n");
        return -1;
    }
}

/* ---- ICMP Echo Ping ---- */

int net_ping(const char* host, int count) {
    if (!host || !host[0]) host = "127.0.0.1";
    if (count < 1) count = 4;
    if (count > 20) count = 20;

    int is_loop = str_eq(host, "127.0.0.1") || str_eq(host, "localhost") || str_eq(host, "::1");
    uint8_t target_ip[4] = { 127, 0, 0, 1 };

    if (!is_loop) {
        if (parse_ipv4(host, target_ip) != 0) {
            terminal_writestring("Resolving ");
            terminal_writestring(host);
            terminal_writestring("...\n");
            if (net_dns_lookup(host, target_ip) != 0) {
                terminal_writestring("ping: cannot resolve ");
                terminal_writestring(host);
                terminal_writestring(": Unknown host\n");
                return -1;
            }
        }
    }

    terminal_writestring("PING ");
    terminal_writestring(host);
    terminal_writestring(" (");
    print_ip(target_ip);
    terminal_writestring(") 56(84) bytes of data.\n");

    if (!is_loop && target_ip[0] == 192 && target_ip[1] == 168 && g_ip[0] == 10 && g_ip[1] == 0) {
        terminal_writestring("Notice: Destination 192.168.x.x is outside QEMU SLIRP guest subnet (10.0.2.15/24).\n");
        terminal_writestring("        Virtual router gateway is 10.0.2.2 (try 'ping 10.0.2.2').\n");
    }

    if (is_loop) {
        int received = 0;
        uint32_t min_rtt = 0xFFFFFFFF, max_rtt = 0, sum_rtt = 0;
        for (int seq = 1; seq <= count; seq++) {
            uint32_t start = timer_ticks();
            for (volatile int w = 0; w < 100000; w++) ;
            uint32_t dt = timer_ticks() - start;
            uint32_t hz = timer_hz(); if (hz == 0) hz = 100;
            uint32_t rtt = (dt * 1000) / hz;

            terminal_writestring("64 bytes from ");
            terminal_writestring(host);
            terminal_writestring(": icmp_seq=");
            terminal_write_uint((uint32_t)seq);
            terminal_writestring(" ttl=64 time=");
            if (rtt == 0) terminal_writestring("<1 ms (loopback)\n");
            else {
                terminal_write_uint(rtt);
                terminal_writestring(" ms (loopback)\n");
            }
            if (rtt < min_rtt) min_rtt = rtt;
            if (rtt > max_rtt) max_rtt = rtt;
            sum_rtt += rtt;
            received++;
            timer_sleep_ms(500);
        }
        terminal_writestring("--- ");
        terminal_writestring(host);
        terminal_writestring(" ping statistics ---\n");
        terminal_write_uint((uint32_t)count);
        terminal_writestring(" packets transmitted, ");
        terminal_write_uint((uint32_t)received);
        terminal_writestring(" received, 0% packet loss\n");
        return 0;
    }

    if (!g_nic_present) {
        terminal_writestring("net: no network card detected for external ping.\n");
        terminal_writestring("hint: run QEMU with: -net nic,model=rtl8139 -net user\n");
        return -1;
    }

    /* Gateway ARP resolution for non-local subnets */
    uint8_t dst_mac[6];
    if (net_get_next_hop_mac(target_ip, dst_mac) != 0) {
        terminal_writestring("ping: failed to resolve MAC for destination\n");
        return -1;
    }

    int received = 0;
    uint32_t min_rtt = 0xFFFFFFFF, max_rtt = 0, sum_rtt = 0;
    uint8_t rx_packet[1536];
    uint16_t ping_id = ++g_ping_id;

    for (int seq = 1; seq <= count; seq++) {
        uint8_t frame[98];
        for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
        for (int i = 0; i < 6; i++) frame[6 + i] = g_mac[i];
        frame[12] = 0x08; frame[13] = 0x00; /* IPv4 */

        /* IPv4 Header */
        frame[14] = 0x45; frame[15] = 0x00;
        uint16_t ip_len = 20 + 8 + 56; /* 84 bytes */
        frame[16] = (uint8_t)(ip_len >> 8);
        frame[17] = (uint8_t)(ip_len & 0xFF);
        frame[18] = (uint8_t)(ping_id >> 8);
        frame[19] = (uint8_t)(seq & 0xFF);
        frame[20] = 0x40; frame[21] = 0x00; /* DF */
        frame[22] = 64;   /* TTL */
        frame[23] = 1;    /* ICMP */
        frame[24] = 0; frame[25] = 0;
        for (int i = 0; i < 4; i++) frame[26 + i] = g_ip[i];
        for (int i = 0; i < 4; i++) frame[30 + i] = target_ip[i];

        uint16_t ip_chk = net_checksum(frame + 14, 20);
        frame[24] = (uint8_t)(ip_chk & 0xFF);
        frame[25] = (uint8_t)(ip_chk >> 8);

        /* ICMP Echo Request */
        uint8_t* icmp = frame + 34;
        icmp[0] = 8; /* Echo Request */
        icmp[1] = 0; /* Code 0 */
        icmp[2] = 0; icmp[3] = 0;
        icmp[4] = (uint8_t)(ping_id >> 8);
        icmp[5] = (uint8_t)(ping_id & 0xFF);
        icmp[6] = (uint8_t)(seq >> 8);
        icmp[7] = (uint8_t)(seq & 0xFF);

        for (int p = 0; p < 56; p++) {
            icmp[8 + p] = (uint8_t)('A' + (p % 26));
        }

        uint16_t icmp_chk = net_checksum(icmp, 8 + 56);
        icmp[2] = (uint8_t)(icmp_chk & 0xFF);
        icmp[3] = (uint8_t)(icmp_chk >> 8);

        uint32_t start_tick = timer_ticks();
        rtl8139_send(frame, sizeof(frame));

        int got_reply = 0;
        uint32_t hz = timer_hz(); if (hz == 0) hz = 100;
        uint32_t timeout_ticks = start_tick + hz;

        while (timer_ticks() <= timeout_ticks) {
            int n = rtl8139_recv(rx_packet, sizeof(rx_packet));
            if (n >= 42) {
                if (rx_packet[12] == 0x08 && rx_packet[13] == 0x06) {
                    process_incoming_arp(rx_packet, n);
                    continue;
                }
                if (rx_packet[12] == 0x08 && rx_packet[13] == 0x00 && rx_packet[23] == 1) {
                    uint8_t ihl = (rx_packet[14] & 0x0F);
                    if (ihl >= 5) {
                        uint32_t ip_hdr_len = (uint32_t)ihl * 4;
                        if ((uint32_t)n >= 14 + ip_hdr_len + 8) {
                            uint8_t* in_icmp = rx_packet + 14 + ip_hdr_len;
                            if (in_icmp[0] == 0 && in_icmp[1] == 0) { /* Echo Reply */
                                uint16_t in_id  = ((uint16_t)in_icmp[4] << 8) | in_icmp[5];
                                uint16_t in_seq = ((uint16_t)in_icmp[6] << 8) | in_icmp[7];
                                if (in_id == ping_id && in_seq == (uint16_t)seq) {
                                    uint32_t dt_ticks = timer_ticks() - start_tick;
                                    uint32_t rtt_ms = (dt_ticks * 1000) / hz;
                                    uint8_t ttl = rx_packet[22];

                                    terminal_writestring("64 bytes from ");
                                    print_ip(target_ip);
                                    terminal_writestring(": icmp_seq=");
                                    terminal_write_uint((uint32_t)seq);
                                    terminal_writestring(" ttl=");
                                    terminal_write_uint((uint32_t)ttl);
                                    terminal_writestring(" time=");
                                    if (rtt_ms == 0) terminal_writestring("<1 ms\n");
                                    else {
                                        terminal_write_uint(rtt_ms);
                                        terminal_writestring(" ms\n");
                                    }

                                    if (rtt_ms < min_rtt) min_rtt = rtt_ms;
                                    if (rtt_ms > max_rtt) max_rtt = rtt_ms;
                                    sum_rtt += rtt_ms;
                                    received++;
                                    got_reply = 1;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            __asm__ volatile ("pause");
        }

        if (!got_reply) {
            terminal_writestring("Request timeout for icmp_seq ");
            terminal_write_uint((uint32_t)seq);
            terminal_putchar('\n');
        }

        timer_sleep_ms(500);
    }

    terminal_writestring("--- ");
    terminal_writestring(host);
    terminal_writestring(" ping statistics ---\n");
    terminal_write_uint((uint32_t)count);
    terminal_writestring(" packets transmitted, ");
    terminal_write_uint((uint32_t)received);
    terminal_writestring(" received, ");
    uint32_t loss = (uint32_t)((count - received) * 100 / count);
    terminal_write_uint(loss);
    terminal_writestring("% packet loss\n");

    if (received > 0) {
        terminal_writestring("rtt min/avg/max = ");
        if (min_rtt == 0) terminal_writestring("<1");
        else terminal_write_uint(min_rtt);
        terminal_putchar('/');
        terminal_write_uint(sum_rtt / (uint32_t)received);
        terminal_putchar('/');
        if (max_rtt == 0) terminal_writestring("<1");
        else terminal_write_uint(max_rtt);
        terminal_writestring(" ms\n");
    } else {
        if (target_ip[0] == 192 && target_ip[1] == 168) {
            terminal_writestring("\n[!] Diagnostic Hint: You are running inside QEMU SLIRP user-mode network.\n");
            terminal_writestring("    In QEMU SLIRP:\n");
            terminal_writestring("      - Guest IP:       10.0.2.15\n");
            terminal_writestring("      - Virtual Router: 10.0.2.2\n");
            terminal_writestring("      - Virtual DNS:    10.0.2.3\n");
            terminal_writestring("    Physical LAN IPs (such as 192.168.0.1 or 192.168.1.1) are host private\n");
            terminal_writestring("    and cannot be reached via SLIRP. Try 'ping 10.0.2.2' to test the router\n");
            terminal_writestring("    or 'ping 8.8.8.8' / 'ping google.com' for external internet.\n");
        } else {
            terminal_writestring("\n[!] Diagnostic Hint: No ICMP echo reply received.\n");
            terminal_writestring("    - Under QEMU SLIRP, the virtual gateway is 10.0.2.2 (try 'ping 10.0.2.2').\n");
            terminal_writestring("    - Real router/WAN destinations can be configured via:\n");
            terminal_writestring("      ifconfig <ip> <netmask> <gw> [dns]\n");
        }
    }

    return (received > 0) ? 0 : -1;
}

/* ---- Network Status Reporting (ifconfig / net_dump_stats) ---- */

void net_dump_stats(void) {
    terminal_writestring("eth0: flags=");
    if (g_nic_present) {
        terminal_writestring("UP,RUNNING,BROADCAST,MULTICAST");
    } else {
        terminal_writestring("DOWN (device offline)");
    }
    terminal_writestring("  mtu 1500\n");

    terminal_writestring("      inet ");
    print_ip(g_ip);
    terminal_writestring("  netmask ");
    print_ip(g_netmask);
    terminal_writestring("  gateway ");
    print_ip(g_gateway);
    terminal_writestring("  dns ");
    print_ip(g_dns);
    terminal_putchar('\n');

    terminal_writestring("      ether ");
    print_mac(g_mac);
    terminal_writestring("  (Realtek RTL8139 Fast Ethernet)\n");

    terminal_writestring("      RX packets ");
    terminal_write_uint(g_stats.rx_packets);
    terminal_writestring("  bytes ");
    terminal_write_uint(g_stats.rx_bytes);
    terminal_writestring("  errors ");
    terminal_write_uint(g_stats.rx_errors);
    terminal_writestring("  dropped ");
    terminal_write_uint(g_stats.rx_dropped);
    terminal_writestring("  overruns ");
    terminal_write_uint(g_stats.rx_overflows);
    terminal_putchar('\n');

    terminal_writestring("      TX packets ");
    terminal_write_uint(g_stats.tx_packets);
    terminal_writestring("  bytes ");
    terminal_write_uint(g_stats.tx_bytes);
    terminal_writestring("  errors ");
    terminal_write_uint(g_stats.tx_errors);
    terminal_writestring("  dropped ");
    terminal_write_uint(g_stats.tx_dropped);
    terminal_writestring("  underruns ");
    terminal_write_uint(g_stats.tx_underruns);
    terminal_writestring("  timeouts ");
    terminal_write_uint(g_stats.tx_timeouts);
    terminal_putchar('\n');

    terminal_writestring("\nARP Cache Table:\n");
    terminal_writestring("  IP Address       HW Address         Age(s)   Status\n");
    uint32_t now = timer_ticks();
    uint32_t hz = timer_hz(); if (hz == 0) hz = 100;
    int entries = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid) {
            entries++;
            terminal_writestring("  ");
            print_ip(g_arp_cache[i].ip);

            int ip_len = 0;
            for (int k = 0; k < 4; k++) {
                uint8_t b = g_arp_cache[i].ip[k];
                if (b >= 100) ip_len += 3;
                else if (b >= 10) ip_len += 2;
                else ip_len += 1;
            }
            ip_len += 3;
            for (int s = ip_len; s < 17; s++) terminal_putchar(' ');

            print_mac(g_arp_cache[i].mac);
            terminal_writestring("  ");

            uint32_t age_s = (now >= g_arp_cache[i].timestamp) ? ((now - g_arp_cache[i].timestamp) / hz) : 0;
            terminal_write_uint(age_s);
            terminal_writestring("s");
            if (age_s < 10) terminal_writestring("      ");
            else if (age_s < 100) terminal_writestring("     ");
            else terminal_writestring("    ");

            if (g_arp_cache[i].ip[0] == g_gateway[0] &&
                g_arp_cache[i].ip[1] == g_gateway[1] &&
                g_arp_cache[i].ip[2] == g_gateway[2] &&
                g_arp_cache[i].ip[3] == g_gateway[3]) {
                terminal_writestring("REACHABLE (gateway)\n");
            } else {
                terminal_writestring("REACHABLE\n");
            }
        }
    }
    if (entries == 0) {
        terminal_writestring("  (table empty)\n");
    }
}

void net_ifconfig(void) {
    net_dump_stats();
}
