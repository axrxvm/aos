/*
 * === AOS HEADER BEGIN ===
 * src/kernel/kernel.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>
#include <arch.h>              // Architecture-independent interface
#include <arch_paging.h>       // Architecture-independent paging interface
#include <vmm.h>               // For virtual memory manager
#include <fs/vfs.h>            // For Virtual File System
#include <fs/ramfs.h>          // For RAM-based filesystem
#include <fs/simplefs.h>       // For storage-based filesystem
#include <fs/fat32.h>          // For FAT32 filesystem
#include <fs/devfs.h>          // For device pseudo-files
#include <fs/procfs.h>         // For process pseudo-files
#include <dev/ata.h>           // For ATA/IDE disk driver
#include <serial.h>
#include <io.h>
#include <memory.h>
#include <pmm.h>
#include <boot_info.h>
#include <panic.h>
#include <multiboot.h>
#include <vga.h>
#include <keyboard.h>  // For keyboard_init(), keyboard_get_scancode(), scancode_to_char()
#include <dev/mouse.h> // For mouse driver
#include <stdbool.h>
#include <string.h>    // For strcmp(), strncmp(), memset()
#include <stdlib.h>    // For itoa()
#include <version.h>   // For version constants
#include <user.h>      // For user management
#include <fs_layout.h> // For filesystem layout
#include <userspace_init.h> // For userspace initialization
#include <process.h>   // For process management
#include <syscall.h>   // For system calls
#include <ipc.h>       // For inter-process communication
#include <partition.h> // For partition management
#include <envars.h>    // For environment variables
#include <time_subsystem.h> // For timezone-aware wall clock sync
#include <kmodule.h>   // For kernel modules
#include <init.h>      // For init system
#include <init_service.h> // For service management
#include <sandbox.h>   // For process sandboxing (v0.7.3)
#include <fileperm.h>  // For file permissions (v0.7.3)
#include <net/net.h>   // For networking (v0.8.0)
#include <net/loopback.h> // For loopback interface (v0.8.0)
#include <net/arp.h>   // For ARP protocol (v0.8.0)
#include <net/ipv4.h>  // For IPv4 protocol (v0.8.0)
#include <net/icmp.h>  // For ICMP protocol (v0.8.0)
#include <net/udp.h>   // For UDP protocol (v0.8.0)
#include <net/tcp.h>   // For TCP protocol (v0.8.0)
#include <net/dhcp.h>  // For DHCP client (v0.8.0)
#include <net/dns.h>   // For DNS resolver (v0.8.5)
#include <net/http.h>  // For HTTP client (v0.8.5)
#include <net/tls.h>   // For TLS/SSL support (v0.8.5)
#include <net/ftp.h>   // For FTP client (v0.8.5)
#include <net/netconfig.h> // For network configuration (v0.8.0)
#include <net/nat.h>   // For NAT support (v0.8.1)
#include <dev/pci.h>   // For PCI bus (v0.8.0)
#include <dev/e1000.h> // For e1000 NIC driver (v0.8.0)
#include <dev/pcnet.h> // For PCnet NIC driver (v0.8.1)
#include <acpi.h>      // For ACPI power management (v0.8.2)
#include <apm.h>       // For aOS Package Manager (v0.8.5)
#include <krm.h>       // For Kernel Recovery Mode (v0.8.8)

// Simple kernel print function (prints to VGA for now)
// Ensure vga_puts is available and initialized before kprint is used extensively.
void kprint(const char *str) {
    vga_puts(str);
    vga_puts("\n");
}

// Global variables accessible to command handlers
uint32_t total_memory_kb = 0;
int unformatted_disk_detected = 0;  // Flag for unformatted disk detection
int simplefs_mounted = 0;  // Flag to track if SimpleFS was successfully mounted
extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

static void register_component_task(const char* name, task_type_t type, int priority) {
    pid_t tid = process_register_kernel_task(name, type, priority);
    if (tid > 0) {
        serial_puts("[TASK] Registered ");
        serial_puts(name);
        serial_puts(" as TID ");
        char tid_buf[12];
        itoa(tid, tid_buf, 10);
        serial_puts(tid_buf);
        serial_puts("\n");
    }
}

void kernel_main(uint32_t multiboot_magic, void *raw_boot_info) {
    // Initialize Kernel Recovery Mode FIRST - before any other subsystems
    // This ensures KRM is always available if anything goes wrong anywhere
    krm_init();
    
    // Initialize CPU-specific features (GDT, segment selectors, etc.)
    arch_cpu_init();
    serial_init(); // Initialize serial port early for status messages
    serial_puts("Welcome - aOS Kernel - Version " AOS_VERSION_SHORT "\n");
    serial_puts("CPU Initialized (");
    serial_puts(arch_get_name());
    serial_puts(")\n");

    // Initialize interrupt system (IDT, PIC, etc.)
    arch_interrupts_init();
    serial_puts("Interrupt System Initialized.\n");

    // NOTE: Don't enable interrupts yet - wait until after paging is initialized
    
    // Initialize device drivers
    keyboard_init(); // Initialize keyboard driver (polling mode).
    serial_puts("Keyboard Initialized (polling mode).\n");
    
    mouse_init(); // Initialize PS/2 mouse driver
    serial_puts("Mouse Initialized (polling mode).\n");


    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC &&
        multiboot_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        serial_puts("Invalid boot magic: 0x");
        serial_put_uint32(multiboot_magic);
        serial_puts("\nExpected 0x");
        serial_put_uint32(MULTIBOOT_BOOTLOADER_MAGIC);
        serial_puts(" (Multiboot1) or 0x");
        serial_put_uint32(MULTIBOOT2_BOOTLOADER_MAGIC);
        serial_puts(" (Multiboot2)\n");
        panic("Invalid Multiboot magic number!");
    }

    if (!raw_boot_info) {
        panic("Bootloader did not provide boot info pointer!");
    }

    boot_info_init(multiboot_magic, raw_boot_info);
    const multiboot_info_t* multiboot_info = boot_info_get_multiboot();
    const boot_runtime_info_t* boot_runtime = boot_info_get_runtime();

    if (boot_runtime->protocol == BOOT_PROTOCOL_UNKNOWN) {
        panic("Unsupported boot protocol!");
    }

    // Check for mmap presence (bit 6) for print_memory_info, and mem_lower/upper (bit 0) for total_memory_kb
    if (multiboot_info && (multiboot_info->flags & (1 << 6))) {
        print_memory_info(multiboot_info); // Assumes this uses the memory map
    } else {
        kprint("Memory map not available via Multiboot.");
    }
    boot_info_print_serial();


    vga_init(); // Initialize VGA text mode.
    vga_clear();
    
    // Pass multiboot info to VGA driver for VBE information
    if (multiboot_info) {
        vga_set_multiboot_info((multiboot_info_t*)multiboot_info);
    }
    
    // Center the ASCII art (VGA is 80 columns, art is ~20 chars wide)
    // Calculate starting position for centering
    vga_set_position(8, 30); // Row 8, Column 30 (roughly centered)
    vga_set_color(0x02); // Green text on black background
    vga_puts("         ___  ____  ");
    vga_set_position(9, 30);
    vga_puts("   __ _ / _ \\/ ___| ");
    vga_set_position(10, 30);
    vga_puts("  / _` | | | \\___ \\ ");
    vga_set_position(11, 30);
    vga_puts(" | (_| | |_| |___) |");
    vga_set_position(12, 30);
    vga_puts("  \\__,_|\\___/|____/ ");
    
    vga_set_position(14, 34); // Center the version text too
    vga_set_color(0x0F); // White text on black background
    vga_puts("Version: ");
    vga_puts(AOS_VERSION_SHORT);
    
    vga_set_position(16, 37); // Center "Loading..."
    vga_set_color(0x0E); // Yellow
    vga_puts("Loading...");
    vga_set_color(0x0F); // Back to white
    
    serial_puts("\n=== aOS Boot Sequence ===\n");
    
    // Detect VBE/VESA graphics capabilities
    serial_puts("Detecting VBE/VESA graphics...\n");
    if (vga_detect_vbe()) {
        serial_puts("[OK] VBE 2.0+ graphics support detected\n");
        serial_puts("     Graphics modes available:\n");
        serial_puts("     - 320x200x256, 640x480, 800x600, 1024x768\n");
        serial_puts("     - Hex color support (#RRGGBB)\n");
        serial_puts("     - RGB/RGBA color formats\n");
        serial_puts("     - Hardware acceleration ready\n");
    } else {
        serial_puts("[WARN] VBE not available, using legacy VGA only\n");
    }
    
    // Check bit 0 for mem_lower/mem_upper validity
    if (multiboot_info && (multiboot_info->flags & (1 << 0))) {
         total_memory_kb = (multiboot_info->mem_lower + multiboot_info->mem_upper);
         serial_puts("Memory detection successful\n");
    } else {
        serial_puts("Using fallback memory detection\n");
    }

    // Always initialize PMM (with fallback if needed)
    init_pmm(total_memory_kb * 1024); // init_pmm expects total memory in bytes.
    // Reserve the full loaded kernel image (code/data/bss/boot tables) so PMM
    // does not hand out frames that back active kernel state.
    pmm_reserve_region((uint32_t)(uintptr_t)&__kernel_start, (uint32_t)(uintptr_t)&__kernel_end);
    
    // Initialize paging system
    init_paging();
    serial_puts("Paging system initialized.\n");
    
    // Initialize Virtual Memory Manager
    init_vmm();
    serial_puts("Virtual Memory Manager initialized.\n");
    
    // ============================================================
    // TEST CODE: Intentional crash to test KRM 
    // Uncomment to test KRM functionality
    // ============================================================
    // serial_puts("WARNING: Triggering test crash in 3...\n");
    // for (volatile int i = 0; i < 50000000; i++);
    // serial_puts("2...\n");
    // for (volatile int i = 0; i < 50000000; i++);
    // serial_puts("1...\n");
    // for (volatile int i = 0; i < 50000000; i++);
    // serial_puts("BOOM! Dereferencing NULL pointer...\n");
    // volatile int* null_ptr = (int*)0x0;
    // *null_ptr = 42; // Triggers page fault -> KRM
    // ============================================================
    
    // Initialize PCI subsystem (v0.8.0)
    pci_init();
    
    // Initialize ACPI subsystem (v0.8.0) - for power management and shutdown
    serial_puts("Initializing ACPI subsystem...\n");
    if (acpi_init() == 0) {
        serial_puts("ACPI initialized, enabling...\n");
        acpi_enable();
    } else {
        serial_puts("ACPI init failed (may be unavailable), using fallback methods\n");
    }
    
    // Initialize networking subsystem (v0.8.0)
    serial_puts("Initializing networking subsystem...\n");
    
    net_init();
    serial_puts("net_init complete\n");
    
    loopback_init();
    serial_puts("loopback_init complete\n");
    
    arp_init();
    serial_puts("arp_init complete\n");
    
    ipv4_init();
    serial_puts("ipv4_init complete\n");
    
    icmp_init();
    serial_puts("icmp_init complete\n");
    
    udp_init();
    serial_puts("udp_init complete\n");
    
    tcp_init();
    serial_puts("tcp_init complete\n");
    
    dhcp_init();
    serial_puts("dhcp_init complete\n");
    
    dns_init();
    serial_puts("dns_init complete\n");
    
    http_init();
    serial_puts("http_init complete\n");
    
    tls_init();
    serial_puts("tls_init complete\n");
    
    ftp_init();
    serial_puts("ftp_init complete\n");
    
    netconfig_init();
    serial_puts("netconfig_init complete\n");
    
    nat_init();  // Initialize NAT subsystem
    serial_puts("nat_init complete\n");
    
    serial_puts("Networking subsystem initialized.\n");
    
    // Initialize network interface drivers
    // Try e1000 first, then PCnet 
    e1000_init();
    pcnet_init();  // PCnet-PCI II / PCnet-FAST III driver (
    
    // Initialize Virtual File System
    serial_puts("About to initialize VFS...\n");
    vfs_init();
    serial_puts("VFS initialized successfully.\n");
    
    // Initialize ATA driver
    serial_puts("About to initialize ATA driver...\n");
    ata_init();
    serial_puts("ATA driver initialized successfully.\n");
    
    // Initialize SimpleFS driver
    serial_puts("About to initialize SimpleFS...\n");
    simplefs_init();
    serial_puts("SimpleFS initialized successfully.\n");
    
    // Initialize FAT32 driver
    serial_puts("About to initialize FAT32...\n");
    fat32_init();
    serial_puts("FAT32 initialized successfully.\n");
    
    // Try to mount SimpleFS as root filesystem
    serial_puts("About to mount root filesystem...\n");
    if (ata_drive_available()) {
        // Try to mount existing SimpleFS first, then FAT32
        if (vfs_mount(NULL, "/", "simplefs", 0) != VFS_OK) {
            serial_puts("SimpleFS mount failed - trying FAT32...\n");
            if (vfs_mount(NULL, "/", "fat32", 0) != VFS_OK) {
                serial_puts("FAT32 mount failed - disk appears unformatted\n");
                serial_puts("Falling back to ramfs. Use 'format' command to initialize disk.\n");
                unformatted_disk_detected = 1;  // Set flag for shell notification
                simplefs_mounted = 0;  // No filesystem mounted
                goto use_ramfs;
            } else {
                serial_puts("FAT32 mounted successfully.\n");
                simplefs_mounted = 1;  // Disk mounted (FAT32 or SimpleFS enables LOCAL mode)
            }
        } else {
            serial_puts("SimpleFS mounted successfully.\n");
            simplefs_mounted = 1;  // Disk mounted (FAT32 or SimpleFS enables LOCAL mode)
        }
    } else {
        serial_puts("No ATA drive available, using ramfs\n");
        simplefs_mounted = 0;  // No disk, using ramfs
        goto use_ramfs;
    }
    
    goto mount_done;
    
use_ramfs:
    // Initialize and mount ramfs as fallback
    serial_puts("About to initialize ramfs...\n");
    ramfs_init();
    serial_puts("Ramfs initialized successfully.\n");
    
    serial_puts("Mounting ramfs as root filesystem...\n");
    if (vfs_mount(NULL, "/", "ramfs", 0) != VFS_OK) {
        panic("Failed to mount root filesystem!");
    }
    serial_puts("Ramfs mounted.\n");
    
mount_done:
    serial_puts("Root filesystem mounted.\n");
    
    // Detect filesystem mode for layout initialization
    // Use LOCAL mode if any disk filesystem (SimpleFS or FAT32) was successfully mounted
    int fs_mode = simplefs_mounted ? FS_MODE_LOCAL : FS_MODE_ISO;
    
    // Initialize filesystem layout (create standard directories)
    serial_puts("Initializing filesystem layout...\n");
    fs_layout_init(fs_mode);
    serial_puts("Filesystem layout initialized.\n");

    // Initialize aOS Package Manager (v0.8.5) - requires filesystem to be ready
    serial_puts("Initializing aOS Package Manager...\n");
    apm_init();
    serial_puts("APM initialized.\n");

    // Mount virtual filesystems for devices and process data
    devfs_init();
    if (vfs_mount(NULL, FS_DEV_DIR, "devfs", 0) == VFS_OK) {
        serial_puts("devfs mounted at /dev\n");
    } else {
        serial_puts("devfs mount failed\n");
    }

    procfs_init();
    if (vfs_mount(NULL, FS_PROC_DIR, "procfs", 0) == VFS_OK) {
        serial_puts("procfs mounted at /proc\n");
    } else {
        serial_puts("procfs mount failed\n");
    }
    
    // Initialize user management system
    serial_puts("Initializing user management...\n");
    user_init();
    serial_puts("User management initialized.\n");
    
    // Try to load user database if in local mode
    if (fs_mode == FS_MODE_LOCAL) {
        serial_puts("Attempting to load user database...\n");
        if (user_load_database(USER_DATABASE_PATH) != 0) {
            serial_puts("No existing user database, using defaults\n");
            // Save default database
            user_save_database(USER_DATABASE_PATH);
        } else {
            // Database loaded successfully, ensure root has admin flag
            user_t* root = user_find_by_uid(UID_ROOT);
            if (root) {
                root->flags |= USER_FLAG_ADMIN;
                serial_puts("Verified root admin privileges\n");
            }
        }
    } else {
        serial_puts("Running in ISO mode, user database will not persist\n");
    }
    
    // Initialize file permission system (v0.7.3)
    serial_puts("Initializing file permissions...\n");
    fileperm_init();
    serial_puts("File permission system initialized.\n");
    
    // Initialize sandbox system (v0.7.3)
    serial_puts("Initializing sandbox (Cage) system...\n");
    sandbox_init();
    serial_puts("Sandbox system initialized.\n");
    
    // Initialize process manager
    serial_puts("Initializing process manager...\n");
    init_process_manager();
    serial_puts("Process manager initialized.\n");
    register_component_task("kernel.core", TASK_TYPE_KERNEL, PRIORITY_HIGH);
    register_component_task("driver.keyboard", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("driver.mouse", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("driver.pci", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("driver.ata", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("driver.e1000", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("driver.pcnet", TASK_TYPE_DRIVER, PRIORITY_NORMAL);
    register_component_task("subsystem.memory", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    register_component_task("subsystem.vfs", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    register_component_task("subsystem.network", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    register_component_task("subsystem.time", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    register_component_task("subsystem.security", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    
    // Initialize system call interface
    serial_puts("Initializing system calls...\n");
    init_syscalls();
    serial_puts("System calls initialized.\n");
    
    // Initialize IPC subsystem
    serial_puts("Initializing IPC...\n");
    init_ipc();
    serial_puts("IPC initialized.\n");
    
    // Initialize partition manager
    serial_puts("Initializing partition manager...\n");
    init_partitions();
    serial_puts("Partition manager initialized.\n");
    
    // Initialize environment variables
    serial_puts("Initializing environment variables...\n");
    envars_init();
    serial_puts("Environment variables initialized.\n");

    // Initialize time subsystem (timezone config + wall clock state)
    serial_puts("Initializing time subsystem...\n");
    time_subsystem_init();
    serial_puts("Time subsystem initialized.\n");
    
    // Initialize init system
    serial_puts("Initializing init system...\n");
    init_system();
    serial_puts("Init system initialized.\n");
    register_component_task("subsystem.init", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);
    
    // Register and start default system services
    serial_puts("Registering default system services...\n");
    init_default_services();
    serial_puts("Default services registered.\n");
    
    // Start boot-level services
    serial_puts("Starting boot-level services...\n");
    init_start_runlevel(RUNLEVEL_BOOT);
    serial_puts("Boot services started.\n");
    
    // Initialize kernel module system
    serial_puts("Initializing kernel module system...\n");
    init_kmodules();
    serial_puts("Kernel module system initialized.\n");
    register_component_task("subsystem.kmodule", TASK_TYPE_SUBSYSTEM, PRIORITY_HIGH);

    serial_puts("Loading startup kernel modules from APM...\n");
    if (apm_load_startup_modules() == 0) {
        serial_puts("Startup kernel modules loaded.\n");
    } else {
        serial_puts("Some startup kernel modules failed to load.\n");
    }
    
    // NOW it's safe to enable interrupts - paging is fully initialized
    serial_puts("Enabling interrupts...\n");
    arch_enable_interrupts();
    serial_puts("Interrupts enabled.\n");
    
    // Initialize system timer (100 Hz)
    serial_puts("Initializing system timer...\n");
    arch_timer_init(100);
    serial_puts("System timer initialized.\n");
    
    // Switch to multi-user runlevel and start multi-user services
    serial_puts("Starting multi-user services...\n");
    init_set_runlevel(RUNLEVEL_MULTI);
    serial_puts("Multi-user mode enabled.\n");
    
    // ================================================================
    // KERNEL INITIALIZATION COMPLETE
    // Transferring control to userspace
    // ================================================================
    serial_puts("\n=== Kernel Initialization Complete ===\n");
    serial_puts("Kernel is now idle. Launching userspace...\n\n");
    
    // Initialize userspace subsystems (command registry, shell, etc.)
    register_component_task("subsystem.userspace", TASK_TYPE_SUBSYSTEM, PRIORITY_NORMAL);
    userspace_init();
    
    // Launch userspace shell in the bootstrap execution context.
    userspace_run();
    
    // Should never reach here
    serial_puts("ERROR: Userspace returned to kernel! We are doomed to misery\n");
    panic("Kernel idle loop exited unexpectedly");
    
    // Old shell code (keeping for reference)
    /*
    serial_puts("Displaying shell prompt...\n");
    kprint("Type a command (help to get started):");
    display_prompt();

    char input_buffer[256];
    memset(input_buffer, 0, sizeof(input_buffer)); // Clear buffer initially
    uint32_t input_pos = 0;

    // Store prompt start position to prevent backspacing into it.
    uint8_t prompt_end_row = vga_get_row();
    uint8_t prompt_end_col = vga_get_col();
    uint8_t current_row = prompt_end_row;
    uint8_t current_col = prompt_end_col;
    update_cursor(current_row, current_col); // Make sure cursor is visible after prompt.

    while (1) {
        uint8_t scancode = keyboard_get_scancode(); // Polls keyboard
        if (scancode) {
            // Handle arrow keys for scrolling through history
            if (scancode == 0x48) { // UP arrow key
                vga_scroll_up_view(); // Scroll view up to see older content
                continue;
            } else if (scancode == 0x50) { // DOWN arrow key
                vga_scroll_down(); // Scroll view down to see newer content
                continue;
            }
            
            if (scancode == 0x1C) { // Enter key scancode (PS/2 set 1)
                input_buffer[input_pos] = '\0'; // Null-terminate the input string.
                kprint(""); // Move to new line for command output.

                if (input_pos > 0) { // If command is not empty
                    execute_command(input_buffer);
                }
                // Reset for next command.
                memset(input_buffer, 0, sizeof(input_buffer)); // memset is now included via string.h
                input_pos = 0;
                display_prompt();
                prompt_end_row = vga_get_row(); // Update prompt end position
                prompt_end_col = vga_get_col();
                current_row = prompt_end_row;
                current_col = prompt_end_col;
                update_cursor(current_row, current_col);

            } else if (scancode == 0x0E) { // Backspace scancode (0x0E)
                if (input_pos > 0) { // Only if there's something to delete in the buffer
                    bool can_backspace = false;
                    // Check if cursor is beyond the initial prompt position for this line
                    if (current_row > prompt_end_row) {
                        can_backspace = true; // Cursor is on a line below the prompt's original line
                    } else if (current_row == prompt_end_row && current_col > prompt_end_col) {
                        can_backspace = true; // Cursor is on the same line as prompt, but after it
                    }

                    if (can_backspace) {
                        input_pos--;
                        // Use the improved VGA backspace function
                        vga_backspace();
                        // Update our local cursor tracking
                        current_row = vga_get_row();
                        current_col = vga_get_col();
                    }
                }
            } else { // Printable character
                char char_to_add = scancode_to_char(scancode);
                if (char_to_add != 0 && input_pos < (sizeof(input_buffer) - 1)) {
                    if (current_col >= VGA_WIDTH) { // If at end of line, wrap
                        current_col = 0;
                        current_row++;
                        // A proper scroll would be needed if current_row >= VGA_HEIGHT
                        // For now, assume vga_scroll_down or similar handles it if called.
                        if (current_row >= VGA_HEIGHT) {
                             input_pos--; // Don't save the char that would cause scroll
                             continue;
                        }
                    }
                    input_buffer[input_pos++] = char_to_add;
                    vga_putc(char_to_add);
                    current_col++;
                    update_cursor(current_row, current_col);
                }
            }
        }
    }
    */
}

// No need for this now
/*
uint8_t vga_get_col_at_prompt_line(uint8_t row) {
    if (row == vga_get_row()) {
        return strlen("aOS> ");
    }
    return 0;
}
*/

// Basic scroll down - this is also conceptual and needs proper implementation
// This function is not used in the refined logic above.
/*
void vga_scroll_down() {
    // ... placeholder ...
}
*/
