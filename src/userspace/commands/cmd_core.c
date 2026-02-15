/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_core.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <version.h>
#include <arch.h>
#include <serial.h>
#include <io.h>
#include <acpi.h>
#include <shell.h>
#include <panic.h>  // For test panic command

// Forward declarations
extern void kprint(const char *str);
extern volatile uint32_t system_ticks;
extern const command_t* command_get_all(void);
extern uint32_t command_get_count(void);

// Global shutdown state (exported for shell.c)
volatile uint32_t shutdown_scheduled_tick = 0; // 0 = not scheduled
volatile uint32_t shutdown_message_last_tick = 0;

// Helper function to compare strings (case-insensitive)
static int strcasecmp_simple(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        // Convert to lowercase
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
        s1++; s2++;
    }
    return *s1 == *s2;
}

// Core commands
static void cmd_help(const char* args) {
    const command_t* commands = command_get_all();
    uint32_t num_commands = command_get_count();
    
    // Categories to display in order
    const char* categories[] = {
        "System",
        "Filesystem",
        "Memory",
        "Process",
        "Network",
        "User",
        "Security",
        "Environment",
        "Modules",
        "Partition",
        "Init",
        "Graphics",
        "General"
    };
    uint32_t num_categories = 13;
    
    // Check if user wants a specific category
    if (args && *args) {
        // Skip leading whitespace
        while (*args == ' ' || *args == '\t') args++;
        
        if (*args) {
            // Show commands for specific category
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_puts("===========================================\n");
            vga_puts("          aOS Command Reference\n");
            vga_puts("===========================================\n");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_puts("\n");
            
            int found_category = 0;
            for (uint32_t cat = 0; cat < num_categories; cat++) {
                if (shell_is_cancelled()) {
                    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                    kprint("\nCommand cancelled.");
                    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    return;
                }
                
                if (strcasecmp_simple(args, categories[cat])) {
                    found_category = 1;
                    vga_puts("[");
                    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                    vga_puts(categories[cat]);
                    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    vga_puts(" Commands]\n\n");
                    
                    int found_commands = 0;
                    for (uint32_t i = 0; i < num_commands; i++) {
                        if (shell_is_cancelled()) {
                            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                            kprint("\nCommand cancelled.");
                            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                            return;
                        }
                        
                        const char* cmd_category = commands[i].category ? commands[i].category : "General";
                        
                        if (strcasecmp_simple(cmd_category, categories[cat])) {
                            found_commands = 1;
                            vga_puts("  ");
                            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                            vga_puts(commands[i].name);
                            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                            
                            if (commands[i].syntax && commands[i].syntax[0] != '\0') {
                                vga_puts(" ");
                                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                                vga_puts(commands[i].syntax);
                                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                            }
                            kprint("");
                            
                            vga_puts("    ");
                            vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
                            vga_puts(commands[i].description);
                            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                            kprint("");
                        }
                    }
                    
                    if (!found_commands) {
                        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
                        kprint("  No commands in this category.");
                        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    }
                    break;
                }
            }
            
            if (!found_category) {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_puts("Unknown category: ");
                vga_puts(args);
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                kprint("\nUse 'help' to see all available categories.");
            }
            return;
        }
    }
    
    // No arguments - show categories only
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("===========================================\n");
    vga_puts("          aOS Command Reference\n");
    vga_puts("===========================================\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Available command categories:\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("\n");
    
    // Count commands in each category and display
    for (uint32_t cat = 0; cat < num_categories; cat++) {
        if (shell_is_cancelled()) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            kprint("\nCommand cancelled.");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            return;
        }
        
        int count = 0;
        for (uint32_t i = 0; i < num_commands; i++) {
            const char* cmd_category = commands[i].category ? commands[i].category : "General";
            if (strcasecmp_simple(cmd_category, categories[cat])) {
                count++;
            }
        }
        
        if (count > 0) {
            vga_puts("  ");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_puts(categories[cat]);
            vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            vga_puts(" (");
            char buf[16];
            itoa(count, buf, 10);
            vga_puts(buf);
            vga_puts(" command");
            if (count != 1) vga_puts("s");
            vga_puts(")");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            kprint("");
        }
    }
    
    vga_puts("\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Type 'help [category]' to see commands in that category.\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts("Example: help system\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_version(const char* args) {
    (void)args; // Unused
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint(AOS_VERSION);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_clear(const char* args) {
    (void)args; // Unused
    vga_clear();
}

static void cmd_echo(const char* args) {
    if (!args || !*args) {
        kprint("");
        return;
    }
    
    // Parse flags
    int no_newline = 0;
    int interpret_escapes = 0;
    const char* ptr = args;
    
    // Process flags
    while (*ptr == '-') {
        ptr++;
        while (*ptr && *ptr != ' ') {
            if (*ptr == 'n') {
                no_newline = 1;
            } else if (*ptr == 'e') {
                interpret_escapes = 1;
            } else if (*ptr == 'c') {
                // Clear screen flag
                vga_clear();
            }
            ptr++;
        }
        // Skip spaces after flag
        while (*ptr == ' ') ptr++;
        
        // If no more flags, break
        if (*ptr != '-') break;
    }
    
    // Print the text
    if (*ptr) {
        if (interpret_escapes) {
            // Process escape sequences
            while (*ptr) {
                if (*ptr == '\\' && *(ptr + 1)) {
                    ptr++;
                    switch (*ptr) {
                        case 'n': vga_putc('\n'); break;
                        case 't': vga_putc('\t'); break;
                        case 'r': vga_putc('\r'); break;
                        case 'b': vga_putc('\b'); break;
                        case '\\': vga_putc('\\'); break;
                        case 'e': vga_putc('\x1B'); break; // ESC character
                        case '0': vga_putc('\0'); break;
                        default: vga_putc(*ptr); break;
                    }
                } else {
                    vga_putc(*ptr);
                }
                ptr++;
            }
        } else {
            vga_puts(ptr);
        }
    }
    
    if (!no_newline) {
        kprint("");
    }
}

static void cmd_uptime(const char* args) {
    (void)args; // Unused
    char buf[64];
    uint32_t current_ticks = arch_timer_get_ticks();
    uint32_t pit_freq_hz = arch_timer_get_frequency();
    if (pit_freq_hz == 0) {
        // Fallback for very early boot / unexpected init order.
        pit_freq_hz = 100;
    }
    uint32_t total_seconds = current_ticks / pit_freq_hz;
    uint32_t hours = total_seconds / 3600;
    uint32_t remainder_seconds = total_seconds % 3600;
    uint32_t minutes = remainder_seconds / 60;
    uint32_t seconds = remainder_seconds % 60;
    uint32_t ms = ((current_ticks % pit_freq_hz) * 1000) / pit_freq_hz;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("System Uptime: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    itoa(hours, buf, 10); vga_puts(buf); vga_puts("h ");
    itoa(minutes, buf, 10); vga_puts(buf); vga_puts("m ");
    itoa(seconds, buf, 10); vga_puts(buf); vga_puts("s");
    if (current_ticks > 0 && total_seconds == 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa(ms, buf, 10); vga_puts(buf);
        vga_puts("ms)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    }
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts(" (Total Ticks: ");
    itoa(current_ticks, buf, 10); vga_puts(buf); vga_puts(")");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
}

static void cmd_reboot(const char* args) {
    (void)args; // Unused
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    kprint("Rebooting...");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    acpi_reboot();  // Try ACPI reboot first, falls back to keyboard controller
}

static void cmd_halt(const char* args) {
    (void)args; // Unused
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    kprint("System Halted.");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    asm volatile("cli; hlt");
}

static void cmd_poweroff(const char* args) {
    uint32_t delay_seconds = 20; // Default 20 seconds
    
    // Parse arguments
    if (args && *args) {
        const char* ptr = args;
        
        // Skip spaces
        while (*ptr == ' ') ptr++;
        
        // Check for cancel flag
        if (*ptr == '-' && *(ptr + 1) == 'c') {
            if (shutdown_scheduled_tick > 0) {
                shutdown_scheduled_tick = 0;
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                kprint("Shutdown cancelled.");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            } else {
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
                kprint("No shutdown scheduled.");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            return;
        }
        
        // Check for time format (+seconds or now)
        if (*ptr == '+') {
            ptr++;
            delay_seconds = atoi(ptr);
        } else if (strncmp(ptr, "now", 3) == 0) {
            delay_seconds = 0;
        } else {
            // Try to parse as delay in seconds
            int parsed = atoi(ptr);
            if (parsed > 0) {
                delay_seconds = parsed;
            }
        }
    }
    
    // If immediate shutdown, do it now
    if (delay_seconds == 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("Powering off via ACPI...");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        acpi_shutdown();
        return;
    }
    
    // Schedule shutdown
    uint32_t pit_freq_hz = arch_timer_get_frequency();
    shutdown_scheduled_tick = arch_timer_get_ticks() + (delay_seconds * pit_freq_hz);
    shutdown_message_last_tick = 0;
    
    // Display shutdown message
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("Broadcast message: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts("System shutdown scheduled in ");
    char buf[16];
    itoa(delay_seconds, buf, 10);
    vga_puts(buf);
    vga_puts(" second");
    if (delay_seconds != 1) vga_puts("s");
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("Run 'shutdown -c' to cancel.");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
}

// Test panic command to demonstrate KRM
static void cmd_test_panic(const char* args) {
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_puts("WARNING: This will trigger a kernel panic and enter KRM.\n");
    vga_puts("Press any key to continue or Ctrl+C to cancel...\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    // Simple wait (in real implementation, would wait for keypress)
    for (volatile int i = 0; i < 50000000; i++);
    
    // Trigger a panic with the provided message or a default
    const char* msg = (args && *args) ? args : "Test panic triggered by user command";
    panic(msg);
}

void cmd_module_core_register(void) {
    command_register_with_category("help", "[category]", "Display all available commands organized by category", "System", cmd_help);
    command_register_with_category("version", "", "Display operating system version information", "System", cmd_version);
    command_register_with_category("clear", "", "Clear the screen and reset cursor position", "System", cmd_clear);
    command_register_with_category("echo", "[-n] [-e] [-c] <text>", "Echo text to screen (-n: no newline, -e: interpret escapes, -c: clear first)", "System", cmd_echo);
    command_register_with_category("uptime", "", "Display system uptime", "System", cmd_uptime);
    command_register_with_category("reboot", "", "Reboot the system", "System", cmd_reboot);
    command_register_with_category("halt", "", "Halt the system", "System", cmd_halt);
    command_register_with_category("shutdown", "[-c] [+seconds|now] [message]", "Power off system (default: 20s, -c: cancel)", "System", cmd_poweroff);
    command_register_with_category("poweroff", "[-c] [+seconds|now] [message]", "Alias for shutdown", "System", cmd_poweroff);
    command_register_with_category("testpanic", "[message]", "Trigger a test panic to demonstrate KRM (WARNING: will crash)", "System", cmd_test_panic);
}
