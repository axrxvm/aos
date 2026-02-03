/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_init.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/*
 * Init System Commands
 */

#include <command.h>
#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <init.h>
#include <init_service.h>

// Forward declaration
extern void kprint(const char* str);

// Command: initctl - init system control
static void cmd_initctl(const char* args) {
    if (!args || *args == '\0') {
        kprint("Usage: initctl <start|stop|restart|status|list> [service_name]");
        return;
    }
    
    char operation[32] = {0};
    char service[64] = {0};
    
    // Parse arguments
    int offset = 0;
    while (args[offset] && args[offset] != ' ') {
        operation[offset] = args[offset];
        offset++;
    }
    
    // Skip spaces
    while (args[offset] && args[offset] == ' ') {
        offset++;
    }
    
    // Get service name if provided
    int svc_idx = 0;
    while (args[offset] && args[offset] != ' ' && svc_idx < 63) {
        service[svc_idx] = args[offset];
        offset++;
        svc_idx++;
    }
    
    if (strcmp(operation, "start") == 0) {
        if (service[0] == '\0') {
            kprint("Usage: initctl start <service_name>");
            return;
        }
        if (init_start_service(service) == 0) {
            vga_puts("Service started: ");
            kprint(service);
        } else {
            vga_puts("Failed to start service: ");
            kprint(service);
        }
    } else if (strcmp(operation, "stop") == 0) {
        if (service[0] == '\0') {
            kprint("Usage: initctl stop <service_name>");
            return;
        }
        if (init_stop_service(service) == 0) {
            vga_puts("Service stopped: ");
            kprint(service);
        } else {
            vga_puts("Failed to stop service: ");
            kprint(service);
        }
    } else if (strcmp(operation, "restart") == 0) {
        if (service[0] == '\0') {
            kprint("Usage: initctl restart <service_name>");
            return;
        }
        if (init_restart_service(service) == 0) {
            vga_puts("Service restarted: ");
            kprint(service);
        } else {
            vga_puts("Failed to restart service: ");
            kprint(service);
        }
    } else if (strcmp(operation, "status") == 0) {
        if (service[0] == '\0') {
            kprint("Usage: initctl status <service_name>");
            return;
        }
        init_service_status(service);
    } else if (strcmp(operation, "list") == 0) {
        init_list_services();
    } else {
        vga_puts("Unknown operation: ");
        kprint(operation);
    }
}

// Command: runlevel - get/set runlevel
static void cmd_runlevel(const char* args) {
    if (!args || *args == '\0') {
        // Display current runlevel
        runlevel_t current = init_get_runlevel();
        vga_puts("Current runlevel: ");
        
        char buf[16];
        itoa((int)current, buf, 10);
        vga_puts(buf);
        
        vga_puts(" - ");
        switch (current) {
            case RUNLEVEL_BOOT:
                kprint("Boot");
                break;
            case RUNLEVEL_SINGLE:
                kprint("Single user");
                break;
            case RUNLEVEL_MULTI:
                kprint("Multi-user");
                break;
            case RUNLEVEL_SHUTDOWN:
                kprint("Shutdown");
                break;
            default:
                kprint("Unknown");
        }
    } else {
        // Set new runlevel
        int level = atoi(args);
        if (level < 0 || level > 3) {
            kprint("Invalid runlevel (0-3)");
            return;
        }
        
        vga_puts("Switching to runlevel ");
        char buf[8];
        itoa(level, buf, 10);
        vga_puts(buf);
        kprint("");
        
        init_set_runlevel((runlevel_t)level);
    }
}

// Command: servicestat - show service statistics
static void cmd_servicestat(const char* args) {
    (void)args; // Unused
    vga_puts("Service Status Report:\n");
    vga_puts("======================\n");
    init_list_services();
    vga_puts("\nTo see details: initctl status <service_name>\n");
}

// Command module registration
void cmd_module_init_register(void) {
    command_register_with_category(
        "initctl",
        "<start|stop|restart|status|list> [service]",
        "Control system services",
        "Init",
        cmd_initctl
    );
    
    command_register_with_category(
        "runlevel",
        "[level]",
        "Get or set runlevel",
        "Init",
        cmd_runlevel
    );
    
    command_register_with_category(
        "servicestat",
        "",
        "Show service status",
        "Init",
        cmd_servicestat
    );
}
