#include "platform.h"
#include "pci.h"
#include "vbe.h"

static int g_vbox;
static int g_inited;

static int pci_has_vendor(uint16_t vendor) {
    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint16_t v = pci_config_read16(bus, dev, fn, 0x00);
                if (v == vendor)
                    return 1;
                if (fn == 0) {
                    uint8_t ht = (uint8_t)(pci_config_read16(bus, dev, fn, 0x0E) & 0xFF);
                    if ((ht & 0x80) == 0)
                        break; /* not multi-function */
                }
            }
        }
    }
    return 0;
}

static int g_cpu_inited;
static int g_has_cpuid;
static int g_has_long_mode;
static int g_has_pae;
static char g_cpu_vendor[13];

static void cpu_detect(void);

void platform_init(void) {
    g_vbox = pci_has_vendor(0x80EE); /* Innotek/Oracle VirtualBox */
    g_inited = 1;
    cpu_detect();
}

static void cpu_detect(void) {
    if (g_cpu_inited) return;
    g_cpu_inited = 1;
    for (int i = 0; i < 13; i++) g_cpu_vendor[i] = 0;

    /* Check EFLAGS ID bit (bit 21) */
    uint32_t a, b;
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        : "=r"(a), "=r"(b)
    );
    if (((a ^ b) & 0x200000) == 0) return;
    g_has_cpuid = 1;

    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    uint32_t max_leaf = eax;
    g_cpu_vendor[0]  = (char)(ebx & 0xFF);
    g_cpu_vendor[1]  = (char)((ebx >> 8) & 0xFF);
    g_cpu_vendor[2]  = (char)((ebx >> 16) & 0xFF);
    g_cpu_vendor[3]  = (char)((ebx >> 24) & 0xFF);
    g_cpu_vendor[4]  = (char)(edx & 0xFF);
    g_cpu_vendor[5]  = (char)((edx >> 8) & 0xFF);
    g_cpu_vendor[6]  = (char)((edx >> 16) & 0xFF);
    g_cpu_vendor[7]  = (char)((edx >> 24) & 0xFF);
    g_cpu_vendor[8]  = (char)(ecx & 0xFF);
    g_cpu_vendor[9]  = (char)((ecx >> 8) & 0xFF);
    g_cpu_vendor[10] = (char)((ecx >> 16) & 0xFF);
    g_cpu_vendor[11] = (char)((ecx >> 24) & 0xFF);
    g_cpu_vendor[12] = '\0';

    if (max_leaf >= 1) {
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (edx & (1u << 6)) g_has_pae = 1;
    }

    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000u));
    uint32_t max_ext = eax;

    if (max_ext >= 0x80000001u) {
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001u));
        if (edx & (1u << 29)) g_has_long_mode = 1;
    }
}

int platform_cpu_has_cpuid(void) {
    if (!g_cpu_inited) cpu_detect();
    return g_has_cpuid;
}

int platform_cpu_has_long_mode(void) {
    if (!g_cpu_inited) cpu_detect();
    return g_has_long_mode;
}

int platform_cpu_has_pae(void) {
    if (!g_cpu_inited) cpu_detect();
    return g_has_pae;
}

const char* platform_cpu_vendor(void) {
    if (!g_cpu_inited) cpu_detect();
    return g_cpu_vendor;
}

int platform_is_virtualbox(void) {
    if (!g_inited) platform_init();
    return g_vbox;
}

int platform_has_vbe(void) {
    if (platform_is_virtualbox())
        return 0; /* Bochs DISPI not available in VBox */
    return vbe_probe();
}
