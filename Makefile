# ==== Architecture Selection ====
# Default architecture (can be overridden: make ARCH=i386)
ARCH ?= i386

# ==== Variables ====
CC = gcc
AS = nasm
LD = ld

SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include
OBJ_DIR = $(BUILD_DIR)/obj

# Architecture-specific configuration
ifeq ($(ARCH),i386)
    ARCH_CFLAGS = -m32 -DARCH_I386 -DARCH_HAS_IO_PORTS -DARCH_HAS_SEGMENTATION
    ARCH_ASFLAGS = -f elf32
    ARCH_LDFLAGS = -melf_i386
    QEMU_ARCH = i386
else ifeq ($(ARCH),x86_64)
    ARCH_CFLAGS = -m64 -DARCH_X86_64 -DARCH_HAS_IO_PORTS
    ARCH_ASFLAGS = -f elf64
    ARCH_LDFLAGS = -melf_x86_64
    QEMU_ARCH = x86_64
else ifeq ($(ARCH),arm)
    ARCH_CFLAGS = -march=armv7-a -DARCH_ARM
    ARCH_ASFLAGS = -march=armv7-a
    ARCH_LDFLAGS = -marmelf
    QEMU_ARCH = arm
else ifeq ($(ARCH),riscv)
    ARCH_CFLAGS = -march=rv32i -mabi=ilp32 -DARCH_RISCV
    ARCH_ASFLAGS = 
    ARCH_LDFLAGS = -melf32lriscv
    QEMU_ARCH = riscv32
else
    $(error Unknown architecture: $(ARCH). Supported: i386, x86_64, arm, riscv)
endif

CFLAGS = $(ARCH_CFLAGS) -ffreestanding -O2 -Wall -Wextra -I$(INCLUDE_DIR) -g
ASFLAGS = $(ARCH_ASFLAGS) -g
LDFLAGS = $(ARCH_LDFLAGS) -n

# Source File Discovery
C_SOURCES = $(shell find $(SRC_DIR) -name '*.c')
S_SOURCES = $(shell find $(SRC_DIR) -name '*.s')

# Object File Generation
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
S_OBJECTS = $(patsubst $(SRC_DIR)/%.s,$(OBJ_DIR)/%.o,$(S_SOURCES))
ALL_OBJECTS = $(C_OBJECTS) $(S_OBJECTS)

KERNEL_ELF = $(BUILD_DIR)/kernel.elf
ISO_DIR = $(BUILD_DIR)/iso
GRUB_DIR = $(ISO_DIR)/boot/grub
ISO = $(BUILD_DIR)/aOS.iso
DISK_IMG = disk.img

# ==== Targets ====

# Default target
all: run

# Ensure build and object directories exist
$(shell mkdir -p $(BUILD_DIR) $(OBJ_DIR))

# C Compilation Rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling C: $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly Compilation Rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.s
	@echo "Assembling: $<..."
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Link ELF
$(KERNEL_ELF): linker.ld $(ALL_OBJECTS)
	@echo "Creating object file directories..."
	@mkdir -p $(sort $(dir $(ALL_OBJECTS)))
	@echo "Linking kernel ELF..."
	$(LD) $(LDFLAGS) -T linker.ld -o $(KERNEL_ELF) $(ALL_OBJECTS)

# Create the ISO with the kernel and grub configuration
iso: $(KERNEL_ELF) boot/grub/grub.cfg
	@echo "Creating ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)
	@echo "ISO created at $(ISO)"

# Run the ISO using QEMU (graphical mode)
run-vga: iso
	@echo "Running in QEMU ($(ARCH))..."
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -enable-kvm 2>/dev/null || qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d
	@echo "QEMU exited."

# Run with VGA + serial logs
run: iso
	@echo "Running in QEMU ($(ARCH)) with VGA + serial logs..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio | tee serial.log

# Run serial-only (no graphics, pure console)
run-nographic: iso
	@echo "Running in QEMU ($(ARCH), serial-only)..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -nographic -serial stdio | tee serial.log

# Debug run with more verbose output
run-debug: iso
	@echo "Running in QEMU ($(ARCH)) with debugging..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio -d guest_errors,unimp | tee serial.log

# Run with attached storage device (creates 50MB disk image if not exists)
run-s: iso
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 50MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=50 2>/dev/null; \
		echo "Disk image created."; \
	else \
		echo "Using existing disk image at $(DISK_IMG)"; \
	fi
	@echo "Running in QEMU ($(ARCH)) with VGA + serial logs + storage..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio -drive file=$(DISK_IMG),format=raw,index=0,media=disk | tee serial.log

# Run with storage and networking enabled (e1000 NIC + TAP networking)
run-sn: iso
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 50MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=50 2>/dev/null; \
		echo "Disk image created."; \
	else \
		echo "Using existing disk image at $(DISK_IMG)"; \
	fi
	@echo "Running in QEMU ($(ARCH)) with storage + TAP networking..."
	@echo "Note: TAP networking requires sudo for bridge setup"
	@echo "Serial output will be saved to serial.log"
	sudo qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk \
		-netdev tap,id=net0,script=./scripts/tap-up.sh,downscript=./scripts/tap-down.sh \
		-device e1000,netdev=net0,mac=52:54:00:12:34:56 | tee serial.log

# Clean up the build directory
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned up build directory."

# Run with user-mode networking (no sudo required, but limited)
run-sn-user: iso
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 50MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=50 2>/dev/null; \
		echo "Disk image created."; \
	else \
		echo "Using existing disk image at $(DISK_IMG)"; \
	fi
	@echo "Running in QEMU ($(ARCH)) with storage + user-mode networking..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk \
		-netdev user,id=net0 -device e1000,netdev=net0,mac=52:54:00:12:34:56 | tee serial.log

# Print current architecture configuration
arch-info:
	@echo "Current architecture: $(ARCH)"
	@echo "QEMU system: qemu-system-$(QEMU_ARCH)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

# Phony targets
.PHONY: all iso run run-nographic run-debug run-s run-sn run-sn-user clean arch-info
