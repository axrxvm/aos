/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_apm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * DEVELOPER_NOTE_BLOCK
 * Module Overview:
 * - This file is part of the aOS production kernel/userspace codebase.
 * - Review public symbols in this unit to understand contracts with adjacent modules.
 * - Keep behavior-focused comments near non-obvious invariants, state transitions, and safety checks.
 * - Avoid changing ABI/data-layout assumptions without updating dependent modules.
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

static void apm_print_banner(void) {
    vga_puts("\n+------------------------------------------+\n");
    vga_puts("|              aOS Package Manager        |\n");
    vga_puts("+------------------------------------------+\n");
}

static void apm_print_main_help(void) {
    apm_print_banner();
    vga_puts("Usage: apm <command> [options]\n\n");
    vga_puts("Core:\n");
    vga_puts("  update                           Refresh repository index\n\n");
    vga_puts("Kernel modules:\n");
    vga_puts("  kmodule list                     List available modules\n");
    vga_puts("  kmodule list --installed         List installed modules\n");
    vga_puts("  kmodule info <name>              Show module details\n");
    vga_puts("  kmodule install|i <name>         Install module\n");
    vga_puts("  kmodule load|l <name> [--auto]   Load module (optional autoload)\n");
    vga_puts("  kmodule unload|x <name>          Unload module\n");
    vga_puts("  kmodule remove|u|delete|d <name> Remove installed module\n\n");
    vga_puts("Autoload:\n");
    vga_puts("  kmodule autoload list\n");
    vga_puts("  kmodule autoload enable <name>\n");
    vga_puts("  kmodule autoload disable <name>\n");
    vga_puts("  kmodule autoload load\n");
}

static void apm_print_kmodule_help(void) {
    apm_print_banner();
    vga_puts("Usage: apm kmodule <subcommand> [options]\n\n");
    vga_puts("Subcommands:\n");
    vga_puts("  list [--installed]               List modules\n");
    vga_puts("  info <name>                      Show module details\n");
    vga_puts("  install|i <name>                 Install module\n");
    vga_puts("  load|l <name> [--auto]           Load module\n");
    vga_puts("  unload|x <name>                  Unload module\n");
    vga_puts("  remove|u|delete|d <name>         Remove module\n");
    vga_puts("  autoload list                    List startup autoload entries\n");
    vga_puts("  autoload enable <name>           Enable startup autoload\n");
    vga_puts("  autoload disable <name>          Disable startup autoload\n");
    vga_puts("  autoload load                    Load startup modules now\n");
}

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
        apm_print_main_help();
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
            apm_print_kmodule_help();
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

        // Handle 'apm kmodule load <name> [--auto]'
        if (strcmp(subcmd, "load") == 0 || strcmp(subcmd, "l") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule load <module_name> [--auto]\n");
                return;
            }
            int enable_autoload = 0;
            for (int i = 3; i < argc; i++) {
                if (strcmp(argv[i], "--auto") == 0 || strcmp(argv[i], "--autoload") == 0) {
                    enable_autoload = 1;
                } else {
                    vga_puts("Unknown option: ");
                    vga_puts(argv[i]);
                    vga_puts("\n");
                    return;
                }
            }

            if (apm_load_module(argv[2]) == 0 && enable_autoload) {
                apm_set_module_autoload(argv[2], true);
            }
            return;
        }

        // Handle 'apm kmodule unload <name>' or 'apm kmodule x <name>'
        if (strcmp(subcmd, "unload") == 0 || strcmp(subcmd, "x") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule unload <module_name>\n");
                return;
            }
            apm_unload_module(argv[2]);
            return;
        }

        // Handle remove/delete aliases
        if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "u") == 0 ||
            strcmp(subcmd, "delete") == 0 || strcmp(subcmd, "d") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule remove <module_name>\n");
                return;
            }
            apm_remove_module(argv[2]);
            return;
        }

        // Handle autoload controls
        if (strcmp(subcmd, "autoload") == 0) {
            if (argc < 3) {
                vga_puts("Usage: apm kmodule autoload <list|enable|disable|load> [module_name]\n");
                return;
            }

            const char* action = argv[2];
            if (strcmp(action, "list") == 0) {
                apm_list_autoload_modules();
                return;
            }

            if (strcmp(action, "load") == 0) {
                apm_load_startup_modules();
                return;
            }

            if (strcmp(action, "enable") == 0 || strcmp(action, "on") == 0 || strcmp(action, "add") == 0) {
                if (argc < 4) {
                    vga_puts("Usage: apm kmodule autoload enable <module_name>\n");
                    return;
                }
                apm_set_module_autoload(argv[3], true);
                return;
            }

            if (strcmp(action, "disable") == 0 || strcmp(action, "off") == 0 ||
                strcmp(action, "remove") == 0 || strcmp(action, "rm") == 0) {
                if (argc < 4) {
                    vga_puts("Usage: apm kmodule autoload disable <module_name>\n");
                    return;
                }
                apm_set_module_autoload(argv[3], false);
                return;
            }

            vga_puts("Unknown autoload action: ");
            vga_puts(action);
            vga_puts("\n");
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
