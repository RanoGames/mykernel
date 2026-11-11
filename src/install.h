#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>

#define INSTALL_MODE_HYBRID 0 /* Hybrid MBR + GPT (Recommended: boots BIOS & recognized by GPT) */
#define INSTALL_MODE_MBR    1 /* Traditional BIOS MBR */
#define INSTALL_MODE_GPT    2 /* Pure UEFI GPT with protective MBR */

/* Full install pipeline with default hybrid layout. Returns 0 on success. */
int install_run(void);

/* Full install pipeline with specified mode (MBR, GPT, or Hybrid). Returns 0 on success. */
int install_run_mode(int mode);

/* Interactive / step-by-step setup wizard with disk info and progress logging. */
void install_wizard(void);

#endif
