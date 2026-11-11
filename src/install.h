#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>
#include <stddef.h>
#include "part.h"

#define INSTALL_MODE_HYBRID 0 /* Hybrid MBR + GPT (Recommended: boots BIOS & recognized by GPT) */
#define INSTALL_MODE_MBR    1 /* Traditional BIOS MBR */
#define INSTALL_MODE_GPT    2 /* Pure UEFI GPT with protective MBR */

#define INSTALL_STEP_COUNT  6

/* Progress callback type for GUI / custom loggers */
typedef void (*install_progress_cb)(int step_num, int total_steps, const char* status_text);

/* Register an optional callback invoked on every progress update */
void install_set_progress_callback(install_progress_cb cb);

/* Query installer status and current progress (0..6) */
int install_get_progress(void);
const char* install_get_status_message(void);

/* Backend step-by-step functions callable by GUI or custom runners */
int install_probe_disk(struct disk_info* out_info);
int install_step_partition(int mode, const struct disk_info* info, uint32_t* out_start_lba, uint32_t* out_size_sectors);
int install_step_bootloader(void);
int install_step_format(uint32_t start_lba, uint32_t size_sectors);
int install_step_kernel(uint32_t start_lba);
int install_step_mount(void);

/* Full install pipeline with default hybrid layout. Returns 0 on success. */
int install_run(void);

/* Full install pipeline with specified mode (MBR, GPT, or Hybrid). Returns 0 on success. */
int install_run_mode(int mode);

/* Interactive / step-by-step setup wizard with disk info and progress logging (CLI). */
void install_wizard(void);

#endif
