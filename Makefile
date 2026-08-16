# Makefile для сборки ядра ОС + ISO-образа с GRUB.
#
# Компилятор: используем кросс-компилятор i686-elf-gcc, если он есть
# в системе (правильный вариант, см. README). Если его нет, откатываемся
# на обычный gcc с флагами "-m32 -ffreestanding", что тоже часто работает
# на x86_64 Linux с установленными 32-битными библиотеками.

CROSS_CC := $(shell command -v i686-elf-gcc 2>/dev/null)

ifeq ($(CROSS_CC),)
    CC = gcc
    EXTRA_ARCH_FLAGS = -m32
    LD_ARCH_FLAGS = -m elf_i386
    AS = as
    AS_ARCH_FLAGS = --32
else
    CC = i686-elf-gcc
    EXTRA_ARCH_FLAGS =
    LD_ARCH_FLAGS =
    AS = i686-elf-as
    AS_ARCH_FLAGS =
endif

SRC_DIR   = src
BUILD_DIR = build
ISO_DIR   = iso

CFLAGS = -std=gnu99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -Wall -Wextra -O2 $(EXTRA_ARCH_FLAGS) -I$(SRC_DIR)

ifeq ($(CROSS_CC),)
    LINK_EXTRA_FLAGS = -no-pie -static
else
    LINK_EXTRA_FLAGS =
endif

# --- Список всех исходников ---
# ВАЖНО: boot.o должен быть первым объектным файлом при линковке,
# чтобы Multiboot-заголовок и _start оказались в начале бинарника.
ASM_SOURCES = boot.s gdt_flush.s idt_load.s isr.s
C_SOURCES   = kernel.c gdt.c serial.c vga.c idt.c irq.c keyboard.c shell.c fs.c calc.c

ASM_OBJECTS = $(ASM_SOURCES:%.s=$(BUILD_DIR)/%.o)
C_OBJECTS   = $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJECTS     = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(ASM_OBJECTS)) $(C_OBJECTS)

.PHONY: all clean run run-iso iso

all: $(BUILD_DIR)/mykernel.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Общее правило: любой src/X.s -> build/X.o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(AS) $(AS_ARCH_FLAGS) $< -o $@

# Общее правило: любой src/X.c -> build/X.o (пересобирается и при
# изменении любого .h файла в src/, чтобы не забыть про правки заголовков)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Линковка всех объектных файлов в итоговый бинарник ядра согласно linker.ld.
# -Wl,--build-id=none : запрещаем линкеру добавлять секцию .note.gnu.build-id —
# без этого она может "втиснуться" перед .multiboot и сдвинуть Multiboot-заголовок
# за пределы первых 8 КБ файла, из-за чего GRUB/QEMU перестают его находить
$(BUILD_DIR)/mykernel.bin: $(OBJECTS) $(SRC_DIR)/linker.ld
	$(CC) -T $(SRC_DIR)/linker.ld -o $(BUILD_DIR)/mykernel.bin $(EXTRA_ARCH_FLAGS) $(LINK_EXTRA_FLAGS) -Wl,--build-id=none -ffreestanding -O2 -nostdlib $(OBJECTS)

# Сборка загрузочного ISO-образа с помощью grub-mkrescue
iso: $(BUILD_DIR)/mykernel.bin
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/mykernel.bin $(ISO_DIR)/boot/mykernel.bin
	cp $(SRC_DIR)/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/mykernel.iso $(ISO_DIR)

# Быстрый запуск в QEMU напрямую из бинарника ядра (без GRUB/ISO)
run: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin

# То же самое, но без графического окна вообще — весь вывод (VGA
# дублируется в serial-порт, см. src/serial.c) идёт прямо в терминал.
# Полезно, если у вас не получается увидеть графическое окно QEMU
# (проблемы с рендерингом WSLg/драйверами видеокарты и т.п.)
run-nographic: $(BUILD_DIR)/mykernel.bin
	qemu-system-i386 -kernel $(BUILD_DIR)/mykernel.bin -nographic

# Запуск собранного ISO-образа (полная эмуляция настоящей загрузки через GRUB)
run-iso: iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/mykernel.iso

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR)/boot/mykernel.bin $(ISO_DIR)/boot/grub/grub.cfg
