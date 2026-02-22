# ==== Architecture Selection ====
# Default architecture (can be overridden: make ARCH=i386)
ARCH ?= i386
ALL_ISO_ARCHES = i386 x86_64
ARCH_EXPLICIT := $(filter command line environment,$(origin ARCH))

# ==== Variables ====
CC = gcc
AS = nasm
LD = ld

SRC_DIR = src
BUILD_ROOT = build
BUILD_DIR = $(BUILD_ROOT)/$(ARCH)
INCLUDE_DIR = include
OBJ_DIR = $(BUILD_DIR)/obj
LINKER_SCRIPT = linker.ld
GRUB_CFG = boot/grub/grub.cfg
BUILD_AOSH = 1
UBIN_LINKER_SCRIPT = ubin.ld
AOSH_PAYLOAD_FORMAT = elf32-i386
AOSH_PAYLOAD_ARCH = i386

# Architecture-specific configuration
ifeq ($(ARCH),i386)
    ARCH_CFLAGS = -m32 -DARCH_I386 -DARCH_HAS_IO_PORTS -DARCH_HAS_SEGMENTATION -fno-pic -fno-pie
    ARCH_ASFLAGS = -f elf32
    ARCH_LDFLAGS = -melf_i386
    QEMU_ARCH = i386
else ifeq ($(ARCH),x86_64)
    ARCH_CFLAGS = -m64 -DARCH_X86_64 -DARCH_HAS_IO_PORTS -mno-red-zone -fno-pic -fno-pie -fcf-protection=none
    ARCH_ASFLAGS = -f elf64
    ARCH_LDFLAGS = -melf_x86_64
    QEMU_ARCH = x86_64
    LINKER_SCRIPT = linker_x86_64.ld
    GRUB_CFG = boot/grub/grub_x86_64.cfg
    BUILD_AOSH = 1
    UBIN_LINKER_SCRIPT = ubin_x86_64.ld
    AOSH_PAYLOAD_FORMAT = elf64-x86-64
    AOSH_PAYLOAD_ARCH = i386:x86-64
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
COMMON_C_SOURCES = $(shell find $(SRC_DIR) -name '*.c' ! -path '$(SRC_DIR)/arch/*')
COMMON_S_SOURCES = $(shell find $(SRC_DIR) -name '*.s' ! -path '$(SRC_DIR)/arch/*')
ARCH_C_SOURCES = $(shell find $(SRC_DIR)/arch/$(ARCH) -name '*.c' 2>/dev/null)
ARCH_S_SOURCES = $(shell find $(SRC_DIR)/arch/$(ARCH) -name '*.s' 2>/dev/null)
C_SOURCES = $(COMMON_C_SOURCES) $(ARCH_C_SOURCES)
S_SOURCES = $(COMMON_S_SOURCES) $(ARCH_S_SOURCES)

# Object File Generation
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
S_OBJECTS = $(patsubst $(SRC_DIR)/%.s,$(OBJ_DIR)/%.o,$(S_SOURCES))
ALL_OBJECTS = $(C_OBJECTS) $(S_OBJECTS)

KERNEL_ELF = $(BUILD_DIR)/kernel.elf
ISO_DIR = $(BUILD_DIR)/iso
GRUB_DIR = $(ISO_DIR)/boot/grub
ISO = $(BUILD_DIR)/aOS.iso
DISK_IMG = disk.img

# === Userspace binary build ===
UBIN_DIR = ubin
UBIN_CFLAGS = $(ARCH_CFLAGS) -ffreestanding -nostdlib -fno-pic -fno-pie -O2 -Wall -Wextra
AOSH_SRC = $(UBIN_DIR)/aosh.c
AOSH_OBJ = $(BUILD_DIR)/ubin/aosh.o
AOSH_ELF = $(BUILD_DIR)/ubin/aosh.elf
AOSH_BIN = $(BUILD_DIR)/ubin/aosh.bin
AOSH_PAYLOAD = $(BUILD_DIR)/ubin/aosh_payload.o

# === Bootloader binary embedding (written by in-OS installer) ===
BOOTLOADER_SRC_DIR = bootloader
BOOTLOADER_BUILD_DIR = $(BUILD_DIR)/bootloader
ABL_MBR_ASM = $(BOOTLOADER_SRC_DIR)/abl_mbr.asm
ABL_STAGE2_ASM = $(BOOTLOADER_SRC_DIR)/abl_stage2.asm
ABL_MBR_BIN = $(BOOTLOADER_BUILD_DIR)/abl_mbr.bin
ABL_STAGE2_BIN = $(BOOTLOADER_BUILD_DIR)/abl_stage2.bin
ABL_MBR_OBJ = $(BOOTLOADER_BUILD_DIR)/abl_mbr_payload.o
ABL_STAGE2_OBJ = $(BOOTLOADER_BUILD_DIR)/abl_stage2_payload.o
KERNEL_BOOT_DEPS = $(ABL_MBR_OBJ) $(ABL_STAGE2_OBJ)
KERNEL_BOOT_OBJS = $(ABL_MBR_OBJ) $(ABL_STAGE2_OBJ)

ifeq ($(BUILD_AOSH),1)
KERNEL_EXTRA_DEPS = $(AOSH_PAYLOAD)
KERNEL_EXTRA_OBJS = $(AOSH_PAYLOAD)
else
KERNEL_EXTRA_DEPS =
KERNEL_EXTRA_OBJS =
endif

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

# === Userspace binary targets ===
$(AOSH_OBJ): $(AOSH_SRC)
	@echo "Compiling userspace shell: $<..."
	@mkdir -p $(dir $@)
	$(CC) $(UBIN_CFLAGS) -c $< -o $@

$(AOSH_ELF): $(AOSH_OBJ) $(UBIN_LINKER_SCRIPT)
	@echo "Linking userspace shell..."
	$(LD) $(ARCH_LDFLAGS) -T $(UBIN_LINKER_SCRIPT) -o $@ $(AOSH_OBJ)

$(AOSH_BIN): $(AOSH_ELF)
	@echo "Creating userspace flat binary..."
	objcopy -O binary $< $@

$(AOSH_PAYLOAD): $(AOSH_BIN)
	@echo "Embedding userspace binary in kernel..."
	cd $(dir $<) && objcopy -I binary -O $(AOSH_PAYLOAD_FORMAT) -B $(AOSH_PAYLOAD_ARCH) \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$(notdir $<) $(notdir $@)

# === Bootloader embedding targets ===
$(ABL_MBR_BIN): $(ABL_MBR_ASM)
	@echo "Assembling custom MBR stage..."
	@mkdir -p $(dir $@)
	$(AS) -f bin $< -o $@

$(ABL_STAGE2_BIN): $(ABL_STAGE2_ASM)
	@echo "Assembling custom stage2 loader..."
	@mkdir -p $(dir $@)
	$(AS) -f bin $< -o $@

$(ABL_MBR_OBJ): $(ABL_MBR_BIN)
	@echo "Embedding MBR stage in kernel..."
	cd $(dir $<) && objcopy -I binary -O $(AOSH_PAYLOAD_FORMAT) -B $(AOSH_PAYLOAD_ARCH) \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$(notdir $<) $(notdir $@)

$(ABL_STAGE2_OBJ): $(ABL_STAGE2_BIN)
	@echo "Embedding stage2 loader in kernel..."
	cd $(dir $<) && objcopy -I binary -O $(AOSH_PAYLOAD_FORMAT) -B $(AOSH_PAYLOAD_ARCH) \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		$(notdir $<) $(notdir $@)

# Link ELF (kernel + embedded userspace payload)
$(KERNEL_ELF): $(LINKER_SCRIPT) $(ALL_OBJECTS) $(KERNEL_EXTRA_DEPS) $(KERNEL_BOOT_DEPS)
	@echo "Creating object file directories..."
	@mkdir -p $(sort $(dir $(ALL_OBJECTS)))
	@echo "Linking kernel ELF..."
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $(KERNEL_ELF) $(ALL_OBJECTS) $(KERNEL_EXTRA_OBJS) $(KERNEL_BOOT_OBJS)

# Create the ISO with the kernel and grub configuration
iso-arch: $(KERNEL_ELF) $(GRUB_CFG)
	@echo "Creating ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp $(GRUB_CFG) $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)
	@echo "ISO created at $(ISO)"

iso-all:
	@echo "ARCH not explicitly set. Building ISOs for: $(ALL_ISO_ARCHES)"
	@set -e; \
	for arch in $(ALL_ISO_ARCHES); do \
		echo ""; \
		echo "=== Building ISO for $$arch ==="; \
		$(MAKE) ARCH=$$arch iso-arch; \
	done

ifneq ($(ARCH_EXPLICIT),)
iso: iso-arch
else
iso: iso-all
endif

# Run the ISO using QEMU (graphical mode)
run-vga: iso-arch
	@echo "Running in QEMU ($(ARCH))..."
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -enable-kvm 2>/dev/null || qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d
	@echo "QEMU exited."

# Run with VGA + serial logs
run: iso-arch
	@echo "Running in QEMU ($(ARCH)) with VGA + serial logs..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio | tee serial.log

# Run serial-only (no graphics, pure console)
run-nographic: iso-arch
	@echo "Running in QEMU ($(ARCH), serial-only)..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -nographic | tee serial.log

# Debug run with more verbose output
run-debug: iso-arch
	@echo "Running in QEMU ($(ARCH)) with debugging..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio -d guest_errors,unimp | tee serial.log

# Run with attached storage device (creates 1000MB disk image if not exists)
run-s: iso-arch
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 100MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=100 2>/dev/null; \
		echo "Disk image created."; \
	else \
		echo "Using existing disk image at $(DISK_IMG)"; \
	fi
	@echo "Running in QEMU ($(ARCH)) with VGA + serial logs + storage..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio -drive file=$(DISK_IMG),format=raw,index=0,media=disk | tee serial.log

# Run with storage and networking enabled (e1000 NIC + TAP networking)
run-sn: iso-arch
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 100MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=100 2>/dev/null; \
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
	rm -rf $(BUILD_ROOT)
	@echo "Cleaned up build directories."

# Run with user-mode networking (no sudo required, but limited)
run-sn-user: iso-arch
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 100MB disk image at $(DISK_IMG)..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=100 2>/dev/null; \
		echo "Disk image created."; \
	else \
		echo "Using existing disk image at $(DISK_IMG)"; \
	fi
	@echo "Running in QEMU ($(ARCH)) with storage + user-mode networking..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -cdrom $(ISO) -m 128M -boot d -serial stdio \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk \
		-netdev user,id=net0 -device e1000,netdev=net0,mac=52:54:00:12:34:56 | tee serial.log

# Run from disk image only (no ISO attached)
run-disk:
	@echo "Checking for disk image..."
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Error: $(DISK_IMG) not found. Create it and run install from ISO mode first."; \
		exit 1; \
	fi
	@echo "Running in QEMU ($(ARCH)) from disk only..."
	@echo "Serial output will be saved to serial.log"
	qemu-system-$(QEMU_ARCH) -m 128M -boot c -serial stdio \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk | tee serial.log

# Print current architecture configuration
arch-info:
	@echo "Current architecture: $(ARCH)"
	@echo "QEMU system: qemu-system-$(QEMU_ARCH)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

# Phony targets
.PHONY: all iso iso-arch iso-all run run-vga run-nographic run-debug run-s run-sn run-sn-user run-disk clean arch-info
