CROSS_CC := $(shell command -v i686-elf-gcc 2>/dev/null)

ifeq ($(CROSS_CC),)
    CC = gcc
    EXTRA_ARCH_FLAGS = -m32
    LD_ARCH_FLAGS = -m elf_i386
    AS = as
    AS_ARCH_FLAGS = --32
    OBJCOPY = objcopy
else
    CC = i686-elf-gcc
    EXTRA_ARCH_FLAGS =
    LD_ARCH_FLAGS =
    AS = i686-elf-as
    AS_ARCH_FLAGS =
    OBJCOPY = i686-elf-objcopy
    ifeq ($(shell command -v i686-elf-objcopy 2>/dev/null),)
        OBJCOPY = objcopy
    endif
endif

SRC_DIR   = src
BUILD_DIR = build
ISO_DIR   = iso
USER_DIR  = user

CFLAGS = -std=gnu99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -Wextra -O2 $(EXTRA_ARCH_FLAGS) -I$(SRC_DIR)

USER_CFLAGS = -std=gnu99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -Wextra -O2 $(EXTRA_ARCH_FLAGS) -I$(USER_DIR)/include

ifeq ($(CROSS_CC),)
    LINK_EXTRA_FLAGS = -no-pie -static
else
    LINK_EXTRA_FLAGS =
endif

ASM_SOURCES = boot.s gdt_flush.s idt_load.s isr.s
C_SOURCES   = kernel.c gdt.c serial.c vga.c idt.c irq.c keyboard.c shell.c fs.c calc.c syscall.c elf.c process.c ata.c fat32.c

ASM_OBJECTS = $(ASM_SOURCES:%.s=$(BUILD_DIR)/%.o)
C_OBJECTS   = $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)

USER_ELF    = $(BUILD_DIR)/hello.elf
USER_BLOB   = $(BUILD_DIR)/hello_blob.o
USER_LIB_OBJS = $(BUILD_DIR)/user_stdio.o $(BUILD_DIR)/user_string.o

OBJECTS = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(ASM_OBJECTS)) $(C_OBJECTS) $(USER_BLOB)

# mkfs: разные имена в разных дистрибутивах
MKFS_FAT := $(shell command -v mkfs.vfat 2>/dev/null || command -v mkfs.fat 2>/dev/null)

.PHONY: all clean run run-iso run-nographic run-fat iso user fat-image

all: $(BUILD_DIR)/mykernel.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(AS) $(AS_ARCH_FLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Userspace libc + hello ---
$(BUILD_DIR)/user_stdio.o: $(USER_DIR)/lib/stdio.c $(wildcard $(USER_DIR)/include/*.h) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_string.o: $(USER_DIR)/lib/string.c $(wildcard $(USER_DIR)/include/*.h) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

user: $(USER_ELF)

$(USER_ELF): $(USER_DIR)/hello.c $(USER_DIR)/linker.ld $(USER_LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_DIR)/linker.ld -nostdlib -Wl,--build-id=none -o $@ $(USER_DIR)/hello.c $(USER_LIB_OBJS)

$(USER_BLOB): $(USER_ELF)
	$(OBJCOPY) -O elf32-i386 -B i386 -I binary $< $@

$(BUILD_DIR)/mykernel.bin: $(OBJECTS) $(SRC_DIR)/linker.ld
	$(CC) -T $(SRC_DIR)/linker.ld -o $(BUILD_DIR)/mykernel.bin $(EXTRA_ARCH_FLAGS) $(LINK_EXTRA_FLAGS) -Wl,--build-id=none -ffreestanding -O2 -nostdlib $(OBJECTS)

iso: $(BUILD_DIR)/mykernel.bin
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/mykernel.bin $(ISO_DIR)/boot/mykernel.bin
	cp $(SRC_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/mykernel.iso $(ISO_DIR)

# Образ FAT32: нужен пакет dosfstools
fat-image:
ifeq ($(MKFS_FAT),)
	@echo ""
	@echo "ERROR: mkfs.vfat / mkfs.fat not found."
	@echo "Install dosfstools, then retry:"
	@echo "  sudo apt update && sudo apt install -y dosfstools"
	@echo ""
	@exit 1
else
	dd if=/dev/zero of=fat.img bs=1M count=32 status=none
	$(MKFS_FAT) -F 32 fat.img
	@echo "FAT image created: fat.img"
	@echo "Adding sample files with mtools if available..."
	@if command -v mcopy >/dev/null 2>&1; then \
		echo "Hello from FAT32" > /tmp/myk_hello.txt; \
		echo "MyKernel test file" > /tmp/myk_test.txt; \
		mcopy -i fat.img /tmp/myk_hello.txt ::HELLO.TXT; \
		mcopy -i fat.img /tmp/myk_test.txt ::TEST.TXT; \
		echo "Sample files added (mtools)."; \
	else \
		echo "mtools not installed — empty FAT ok."; \
		echo "Optional: sudo apt install mtools"; \
		echo "Or write files from the OS: fatmount && fatwrite HELLO.TXT hi"; \
	fi
	@echo "Run: make run-fat"
endif

run: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin

run-nographic: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -nographic

run-fat: $(BUILD_DIR)/mykernel.bin
	@test -f fat.img || (echo "No fat.img — run: make fat-image"; exit 1)
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -drive file=fat.img,format=raw,if=ide

run-iso: iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/mykernel.iso

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR)/boot/mykernel.bin $(ISO_DIR)/boot/grub/grub.cfg
