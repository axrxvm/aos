/*
 * === AOS HEADER BEGIN ===
 * src/userspace/userspace_init.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * USERSPACE INITIALIZATION - RING 3 SHELL LOADER
 *
 * The ring 3 shell (ubin/aosh.c) is compiled as a standalone flat binary
 * at 0x08048000 and embedded into the kernel via objcopy. At boot we:
 *   1. Allocate user-accessible pages for the binary
 *   2. Copy the embedded payload there
 *   3. Allocate a user stack
 *   4. Enter ring 3 via iret — the shell takes over
 */

#include <userspace_init.h>
#include <command_registry.h>
#include <shell.h>
#include <user.h>
#include <serial.h>
#include <vga.h>
#include <fs_layout.h>
#include <process.h>
#include <fd.h>
#include <string.h>
#include <vmm.h>

/* Symbols injected by objcopy from the embedded aosh.bin payload */
extern char _binary_aosh_bin_start[];
extern char _binary_aosh_bin_end[];

// Simple hex printer for serial debug
static void print_hex(const char* prefix, uint32_t value) {
    char buf[50];
    int i = 0;
    const char* p = prefix;
    while (*p && i < 40) buf[i++] = *p++;
    buf[i++] = '0'; buf[i++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) {
        int digit = (value >> shift) & 0xF;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    }
    buf[i++] = '\n'; buf[i] = '\0';
    serial_puts(buf);
}

void userspace_init(void) {
    serial_puts("=== Userspace Initialization ===\n");

    serial_puts("Initializing command registry...\n");
    init_commands();
    serial_puts("Command registry initialized.\n");

    serial_puts("Initializing shell subsystem...\n");
    shell_init();
    serial_puts("Shell initialized.\n");

    serial_puts("=== Userspace Ready ===\n");
}

void userspace_run(void) {
    serial_puts("=== Starting Ring 3 Userspace Shell ===\n");

    extern process_t* process_get_current(void);
    extern void* vmm_alloc_at(address_space_t *as, uint32_t virtual_addr, size_t size, uint32_t flags);
    extern void enter_usermode(uint32_t entry_point, uint32_t user_stack, int argc, char** argv);
    extern address_space_t* kernel_address_space;

    process_t* current = process_get_current();
    if (!current) {
        serial_puts("ERROR: No current process — falling back to ring 0 shell\n");
        userspace_run_legacy();
        return;
    }

    /* --- Compute embedded binary size --- */
    uint32_t bin_size = (uint32_t)(_binary_aosh_bin_end - _binary_aosh_bin_start);
    uint32_t code_pages = (bin_size + 4095) / 4096;  /* round up to pages */
    
    /* Add extra pages for BSS segment (static uninitialized data like history arrays)
     * Binary: ~8.8 KB, BSS: ~12.8 KB (history[50][256]), plus stack buffer headroom */
    code_pages += 5;  /* Add 5 extra pages (20 KB) for BSS and safety margin */
    uint32_t code_alloc = code_pages * 4096;

    print_hex("Shell binary size: ", bin_size);
    print_hex("Pages needed:      ", code_pages);

    if (bin_size == 0) {
        serial_puts("ERROR: Embedded shell binary is empty!\n");
        userspace_run_legacy();
        return;
    }

    /* --- Step 1: Allocate user code pages at 0x08048000 --- */
    uint32_t user_code_addr = VMM_USER_CODE_START;  /* 0x08048000 */
    void* user_code = vmm_alloc_at(kernel_address_space, user_code_addr,
                                    code_alloc,
                                    VMM_PRESENT | VMM_WRITE | VMM_USER);
    if (!user_code) {
        serial_puts("ERROR: Failed to allocate user code pages!\n");
        userspace_run_legacy();
        return;
    }

    /* --- Step 2: Copy the shell binary --- */
    memcpy((void*)user_code_addr, _binary_aosh_bin_start, bin_size);
    serial_puts("Shell binary copied to user pages.\n");

    /* --- Step 3: Allocate user stack (16 KB, USER-accessible) --- */
    uint32_t stack_top   = 0xBFFFFFF0;  /* 16-byte aligned */
    uint32_t stack_pages = 4;           /* 16 KB */
    uint32_t stack_base  = 0xC0000000 - (stack_pages * 4096);  /* 0xBFFFC000 */

    void* user_stack = vmm_alloc_at(kernel_address_space,
                                     stack_base, stack_pages * 4096,
                                     VMM_PRESENT | VMM_WRITE | VMM_USER);
    if (!user_stack) {
        serial_puts("ERROR: Failed to allocate user stack!\n");
        userspace_run_legacy();
        return;
    }
    current->user_stack = stack_top;

    print_hex("Code  @ ", user_code_addr);
    print_hex("Stack @ ", stack_top);

    /* --- Step 4: Allocate a kernel stack for ring 3 ↔ ring 0 transitions ---
     * When the scheduler preempts this process and later reschedules it,
     * it calls arch_set_kernel_stack(current->kernel_stack) to restore
     * TSS.esp0. If kernel_stack is 0, TSS.esp0 = 0 → next INT/IRQ
     * from ring 3 writes to address 0 → triple fault.
     */
    void* kstack_mem = kmalloc(8192);  /* 8 KB kernel stack */
    if (!kstack_mem) {
        serial_puts("ERROR: Failed to allocate kernel stack!\n");
        userspace_run_legacy();
        return;
    }
    current->kernel_stack = (uint32_t)kstack_mem + 8192;  /* stack top */
    print_hex("KStk  @ ", current->kernel_stack);

    /* Set TSS.esp0 to match — enter_usermode no longer does this */
    extern void set_kernel_stack(uint32_t stack);
    set_kernel_stack(current->kernel_stack);

    /* --- Step 5: Enter ring 3 (never returns) --- */
    serial_puts("Entering ring 3 — handing control to userspace shell.\n");
    enter_usermode(user_code_addr, stack_top, 0, (void*)0);

    /* Should never get here */
    serial_puts("ERROR: Returned from ring 3!\n");
    userspace_run_legacy();
}

// Legacy ring 0 shell fallback
void userspace_run_legacy(void) {
    serial_puts("Starting legacy ring 0 shell...\n");

    while (1) {
        if (shell_login() == 0) {
            shell_run();

            if (fs_layout_get_mode() == FS_MODE_LOCAL) {
                serial_puts("Saving user database...\n");
                user_save_database(USER_DATABASE_PATH);
            }
            vga_clear();
        } else {
            vga_puts("\nLogin failed. Please wait...\n\n");
            for (volatile int i = 0; i < 100000000; i++);
        }
    }
}

