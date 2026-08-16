CROSS_CC := $(shell command -v i686-elf-gcc 2>/dev/null)

ifeq ($(CROSS_CC),)
    CC = gcc
    EXTRA_ARCH_FLAGS = -m32
    AS = as
    AS_ARCH_FLAGS = --32
    OBJCOPY = objcopy
else
    CC = i686-elf-gcc
    EXTRA_ARCH_FLAGS =
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
USER_PIC_CFLAGS = -std=gnu99 -ffreestanding -fno-stack-protector -fPIC -Wall -Wextra -O2 $(EXTRA_ARCH_FLAGS) -I$(USER_DIR)/include

ifeq ($(CROSS_CC),)
    LINK_EXTRA_FLAGS = -no-pie -static
else
    LINK_EXTRA_FLAGS =
endif

ASM_SOURCES = boot.s gdt_flush.s idt_load.s isr.s context_switch.s
C_SOURCES   = kernel.c gdt.c serial.c vga.c idt.c irq.c keyboard.c shell.c fs.c calc.c syscall.c elf.c process.c ata.c fat32.c kmalloc.c mlang.c timer.c sched.c dyld.c gfx.c mouse.c snake.c sound.c pci.c ac97.c vbe.c settings.c

ASM_OBJECTS = $(ASM_SOURCES:%.s=$(BUILD_DIR)/%.o)
C_OBJECTS   = $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)

USER_ELF    = $(BUILD_DIR)/hello.elf
USER_BLOB   = $(BUILD_DIR)/hello_blob.o
LIBHELLO_SO = $(BUILD_DIR)/libhello.so
LIBHELLO_BLOB = $(BUILD_DIR)/libhello_blob.o
DYNHELLO_ELF = $(BUILD_DIR)/dynhello.elf
DYNHELLO_BLOB = $(BUILD_DIR)/dynhello_blob.o
USER_LIB_OBJS = $(BUILD_DIR)/user_stdio.o $(BUILD_DIR)/user_string.o $(BUILD_DIR)/user_malloc.o

OBJECTS = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(ASM_OBJECTS)) $(C_OBJECTS) $(USER_BLOB) $(LIBHELLO_BLOB) $(DYNHELLO_BLOB)

MKFS_FAT := $(shell command -v mkfs.vfat 2>/dev/null || command -v mkfs.fat 2>/dev/null)

.PHONY: all clean run run-iso run-nographic run-fat run-vbe iso user fat-image

all: $(BUILD_DIR)/mykernel.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(AS) $(AS_ARCH_FLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_stdio.o: $(USER_DIR)/lib/stdio.c $(wildcard $(USER_DIR)/include/*.h) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_string.o: $(USER_DIR)/lib/string.c $(wildcard $(USER_DIR)/include/*.h) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_malloc.o: $(USER_DIR)/lib/malloc.c $(wildcard $(USER_DIR)/include/*.h) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

user: $(USER_ELF) $(LIBHELLO_SO) $(DYNHELLO_ELF)

$(USER_ELF): $(USER_DIR)/hello.c $(USER_DIR)/linker.ld $(USER_LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_DIR)/linker.ld -nostdlib -Wl,--build-id=none -o $@ $(USER_DIR)/hello.c $(USER_LIB_OBJS)

$(USER_BLOB): $(USER_ELF)
	$(OBJCOPY) -O elf32-i386 -B i386 -I binary $< $@

$(LIBHELLO_SO): $(USER_DIR)/libhello.c $(USER_DIR)/libhello.ld | $(BUILD_DIR)
	$(CC) $(USER_PIC_CFLAGS) -shared -nostdlib -Wl,--build-id=none \
		-T $(USER_DIR)/libhello.ld -o $@ $(USER_DIR)/libhello.c

$(LIBHELLO_BLOB): $(LIBHELLO_SO)
	$(OBJCOPY) -O elf32-i386 -B i386 -I binary $< $@

$(DYNHELLO_ELF): $(USER_DIR)/dynhello.c $(USER_DIR)/dynhello.ld $(LIBHELLO_SO) | $(BUILD_DIR)
	$(CC) $(USER_PIC_CFLAGS) -nostdlib -Wl,--build-id=none \
		-T $(USER_DIR)/dynhello.ld \
		-Wl,-dynamic-linker,/lib/ld-mykernel.so \
		-o $@ $(USER_DIR)/dynhello.c $(LIBHELLO_SO)

$(DYNHELLO_BLOB): $(DYNHELLO_ELF)
	$(OBJCOPY) -O elf32-i386 -B i386 -I binary $< $@

$(BUILD_DIR)/mykernel.bin: $(OBJECTS) $(SRC_DIR)/linker.ld
	$(CC) -T $(SRC_DIR)/linker.ld -o $(BUILD_DIR)/mykernel.bin $(EXTRA_ARCH_FLAGS) $(LINK_EXTRA_FLAGS) -Wl,--build-id=none -ffreestanding -O2 -nostdlib $(OBJECTS)

iso: $(BUILD_DIR)/mykernel.bin
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/mykernel.bin $(ISO_DIR)/boot/mykernel.bin
	cp $(SRC_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/mykernel.iso $(ISO_DIR)

fat-image:
ifeq ($(MKFS_FAT),)
	@echo "ERROR: install dosfstools"
	@exit 1
else
	dd if=/dev/zero of=fat.img bs=1M count=32 status=none
	$(MKFS_FAT) -F 32 fat.img
endif

run: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -vga std \
		-audiodev pa,id=spk -machine pcspk-audiodev=spk 2>/dev/null \
	|| qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -vga std \
		-audiodev sdl,id=spk -machine pcspk-audiodev=spk 2>/dev/null \
	|| qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -vga std

run-nographic: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -nographic

run-fat: $(BUILD_DIR)/mykernel.bin
	@test -f fat.img || (echo "No fat.img — make fat-image"; exit 1)
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -drive file=fat.img,format=raw,if=ide -vga std

run-vbe: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -vga std

run-iso: iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/mykernel.iso -vga std

clean:
	rm -rf $(BUILD_DIR)
