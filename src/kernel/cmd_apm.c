/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_apm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#include <command.h>
#include <command_registry.h>
#include <apm.h>
#include <vga.h>
#include <string.h>
#include <serial.h>
#include <stdlib.h>

// Parse args string into argc/argv (max 8 args)
#define MAX_APM_ARGS 8

static int parse_args(const char* args, char* argv[], char* buffer, size_t bufsize) {
    if (!args || !argv || !buffer) return 0;
    
    int argc = 0;
    size_t pos = 0;
    
    // Skip leading whitespace
    while (*args && (*args == ' ' || *args == '\t')) args++;
    
    while (*args && argc < MAX_APM_ARGS && pos < bufsize - 1) {
        // Start of new argument
        argv[argc] = &buffer[pos];
        
        // Copy until whitespace or end
        while (*args && *args != ' ' && *args != '\t' && pos < bufsize - 1) {
            buffer[pos++] = *args++;
        }
        buffer[pos++] = '\0';
        argc++;
        
        // Skip whitespace between args
        while (*args && (*args == ' ' || *args == '\t')) args++;
    }
    
    return argc;
}

static void cmd_apm(const char* args) {
    char* argv[MAX_APM_ARGS];
    char buffer[256];
    
    //serial_puts("[CMD_APM] cmd_apm entry\n");
    
    int argc = parse_args(args, argv, buffer, sizeof(buffer));
    
    //serial_puts("[CMD_APM] parsed argc=");
    char num[16];
    itoa(argc, num, 10);
    //serial_puts(num);
    //serial_puts("\n");
    
    if (argc < 1) {
        vga_puts("Usage: apm <command> [options]\n");
        vga_puts("\nCommands:\n");
        vga_puts("  update                     - Update repository list\n");
        vga_puts("  kmodule list               - List available modules\n");
        vga_puts("  kmodule list --installed   - List installed modules\n");
        vga_puts("  kmodule info <name>        - Show module information\n");
        vga_puts("  kmodule install <name>     - Install a module\n");
        vga_puts("  kmodule i <name>           - Alias for install\n");
        vga_puts("  kmodule remove <name>      - Remove an installed module\n");
        vga_puts("  kmodule u <name>           - Alias for remove\n");
        return;
    }

    const char* cmd = argv[0];
    //serial_puts("[CMD_APM] cmd='");
    //serial_puts(cmd);
    //serial_puts("'\n");

    // Handle 'apm update'
    if (strcmp(cmd, "update") == 0) {
        serial_puts("[CMD_APM] Calling apm_update...\n");
        apm_update();
        return;
    }

    // Handle 'apm kmodule ...'
    if (strcmp(cmd, "kmodule") == 0) {
        if (argc < 2) {
            vga_puts("Usage: apm kmodule <subcommand> [options]\n");
            vga_puts("\nSubcommands:\n");
            vga_puts("  list [--installed]   - List modules\n");
            vga_puts("  info <name>          - Show module information\n");
            vga_puts("  install|i <name>     - Install a module\n");
            vga_puts("  remove|u <name>      - Remove an installed module\n");
            return;
        }

        const char* subcmd = argv[1];

        // Handle 'apm kmodule list'
        if (strcmp(subcmd, "list") == 0) {
            if (argc > 2 && strcmp(argv[2], "--installed") == 0) {
                apm_list_installed();
            } else {
                apm_list_available();
            }
            return;
        }

        // Handle 'apm kmodule info <name>'
        if (strcmp(subcmd, "info") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule info <module_name>\n");
                return;
            }
            apm_show_info(argv[2]);
            return;
        }

        // Handle 'apm kmodule install <name>' or 'apm kmodule i <name>'
        if (strcmp(subcmd, "install") == 0 || strcmp(subcmd, "i") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule install <module_name>\n");
                return;
            }
            apm_install_module(argv[2]);
            return;
        }

        // Handle 'apm kmodule remove <name>' or 'apm kmodule u <name>'
        if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "u") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule remove <module_name>\n");
                return;
            }
            apm_remove_module(argv[2]);
            return;
        }

        vga_puts("Unknown kmodule subcommand: ");
        vga_puts(subcmd);
        vga_puts("\n");
        return;
    }

    vga_puts("Unknown apm command: ");
    vga_puts(cmd);
    vga_puts("\n");
}

void cmd_module_apm_register(void) {
    command_register_with_category(
        "apm",
        "apm <command> [options]",
        "aOS Package Manager for kernel modules",
        "Package Management",
        cmd_apm
    );
}
