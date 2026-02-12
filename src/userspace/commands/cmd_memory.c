/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_memory.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <pmm.h>
#include <vmm.h>
#include <process.h>
#include <shell.h>

// Forward declarations
extern void kprint(const char *str);
extern uint32_t total_memory_kb;
extern address_space_t* kernel_address_space;
extern page_directory_t* current_directory;

static void cmd_mem(const char* args) {
    (void)args;
    char buf[32];
    if (total_memory_kb > 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("Total Memory: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        itoa(total_memory_kb / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" MB");
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa(total_memory_kb, buf, 10);
        vga_puts(buf);
        vga_puts(" KB)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("Memory information not available.");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

static void cmd_vmm(const char* args) {
    (void)args;
    kprint("Virtual Memory Manager Status:");
    if (kernel_address_space) {
        vmm_print_stats(kernel_address_space);
        kprint("Paging enabled: Yes");
        vga_puts("Current page directory: 0x");
        char buf[12];
        itoa(current_directory->physical_addr, buf, 10);
        kprint(buf);
    } else {
        kprint("VMM not initialized");
    }
}

static void cmd_test_page(const char* args) {
    (void)args;
    kprint("Testing page allocation...");
    void *page1 = alloc_page();
    void *page2 = alloc_page();
    
    char buf[12];
    vga_puts("Physical page 1: 0x"); itoa((uint32_t)page1, buf, 10); kprint(buf);
    vga_puts("Physical page 2: 0x"); itoa((uint32_t)page2, buf, 10); kprint(buf);
    
    if (page1 && page2) {
        kprint("Page allocation test passed!");
        free_page(page1);
        free_page(page2);
        kprint("Pages freed.");
    } else {
        kprint("Page allocation test failed!");
    }
}

static void cmd_showmem(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("Memory Usage:");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    uint32_t total_frames = pmm_get_total_frames();
    uint32_t used_frames = pmm_get_used_frames();
    uint32_t free_frames = total_frames - used_frames;
    
    char num_str[16];
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Physical: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa(used_frames * 4, num_str, 10);
    vga_puts(num_str);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" used, ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    itoa(free_frames * 4, num_str, 10);
    vga_puts(num_str);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" available");
    kprint("");
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Kernel Heap: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("2 MB");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts(" allocated (0x500000 - 0x700000)");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    process_t* current = process_get_current();
    if (current) {
        kprint("");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("Current Task: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts(current->name);
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (TID ");
        itoa(current->pid, num_str, 10);
        vga_puts(num_str);
        vga_puts(")");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
}

void cmd_module_memory_register(void) {
    command_register_with_category("mem", "", "Display system memory", "Memory", cmd_mem);
    command_register_with_category("vmm", "", "Display virtual memory status", "Memory", cmd_vmm);
    command_register_with_category("test-page", "", "Test page allocation", "Memory", cmd_test_page);
    command_register_with_category("showmem", "", "Display memory usage", "Memory", cmd_showmem);
}
