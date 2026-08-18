#ifndef INSTALL_H
#define INSTALL_H

/* Full install pipeline: MBR + FAT32 + seed files. Returns 0 on success. */
int install_run(void);

/* Interactive text wizard (prints steps, calls install_run). */
void install_wizard(void);

#endif
