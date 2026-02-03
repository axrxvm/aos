/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/init_service.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/*
 * Init Service Registry Implementation - Built-in and Script-based Services
 */

#include <init_service.h>
#include <init.h>
#include <serial.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <fs/vfs.h>

// Storage for built-in services
static service_t builtin_services[16];
static uint32_t builtin_service_count = 0;


// Default System Service Implementations


// Serial console service
static void service_serial_start(void) {
    serial_puts("Serial console service started\n");
}

static void service_serial_stop(void) {
    serial_puts("Serial console service stopped\n");
}

// VGA console service
static void service_vga_start(void) {
    vga_puts("VGA console service started\n");
}

static void service_vga_stop(void) {
    vga_puts("VGA console service stopped\n");
}

// Keyboard service
static void service_keyboard_start(void) {
    serial_puts("Keyboard service started\n");
}

static void service_keyboard_stop(void) {
    serial_puts("Keyboard service stopped\n");
}

// Filesystem service (always running in multi-user)
static void service_filesystem_start(void) {
    serial_puts("Filesystem service started\n");
}

static void service_filesystem_stop(void) {
    serial_puts("Filesystem service stopped\n");
}

// Syslogd - system logging daemon
static void service_syslogd_start(void) {
    serial_puts("Syslogd service started\n");
}

static void service_syslogd_stop(void) {
    serial_puts("Syslogd service stopped\n");
}

// Cron daemon - scheduled task runner
static void service_crond_start(void) {
    serial_puts("Cron daemon service started\n");
}

static void service_crond_stop(void) {
    serial_puts("Cron daemon service stopped\n");
}

// Network service (placeholder)
static void service_network_start(void) {
    serial_puts("Network service started\n");
}

static void service_network_stop(void) {
    serial_puts("Network service stopped\n");
}


// Init.d Script Execution


// Execute an init.d style service script
// In a real filesystem implementation, this would load and parse shell scripts
int init_script_exec(const char* script_name, service_operation_t operation) {
    (void)operation;  // Suppress unused parameter warning
    
    char script_path[256];
    snprintf(script_path, sizeof(script_path), "/etc/init.d/%s", script_name);
    
    // Check if script exists by trying to open it
    int fd = vfs_open(script_path, O_RDONLY);
    if (fd < 0) {
        serial_puts("[INIT] Script not found: ");
        serial_puts(script_path);
        serial_puts("\n");
        return SERVICE_SCRIPT_NOT_FOUND;
    }
    
    vfs_close(fd);
    
    // TODO: In a full implementation, this would:
    // 1. Read the script file
    // 2. Parse shell script syntax
    // 3. Execute the appropriate operation handler
    // 4. Return exit code
    
    serial_puts("[INIT] Script execution not yet fully implemented: ");
    serial_puts(script_path);
    serial_puts("\n");
    
    return SERVICE_SCRIPT_SUCCESS;
}

// Load services from init.d directory
void init_load_scripts(void) {
    // Check if /etc/init.d exists by trying to open as directory
    int fd = vfs_open("/etc/init.d", O_RDONLY | O_DIRECTORY);
    
    if (fd < 0) {
        serial_puts("[INIT] No /etc/init.d directory found\n");
        return;
    }
    
    vfs_close(fd);
    
    // TODO: In a full implementation, this would:
    // 1. Read directory listing of /etc/init.d
    // 2. Parse service configuration from each file
    // 3. Register services with the init system
    // 4. Handle service dependencies
    
    serial_puts("[INIT] Loading init.d scripts...\n");
}

// Register a built-in service helper function
int init_register_builtin_service(const char* name,
                                  const char* description,
                                  service_type_t type,
                                  uint32_t runlevels,
                                  uint32_t priority,
                                  void (*start_fn)(void),
                                  void (*stop_fn)(void),
                                  bool auto_restart) {
    if (builtin_service_count >= 16) {
        serial_puts("[INIT] Too many built-in services\n");
        return -1;
    }
    
    // Create service structure
    service_t* service = &builtin_services[builtin_service_count++];
    service->name = name;
    service->description = description;
    service->type = type;
    service->runlevels = runlevels;
    service->priority = priority;
    service->start_fn = start_fn;
    service->stop_fn = stop_fn;
    service->auto_restart = auto_restart;
    service->state = SERVICE_STOPPED;
    service->pid = 0;
    service->start_time = 0;
    service->restart_count = 0;
    
    // Register with init system
    return init_register_service(service);
}

// Initialize default system services
void init_default_services(void) {
    serial_puts("[INIT] Registering default system services...\n");
    
    // Critical boot-time services (runlevel 0 - BOOT)
    uint32_t boot_level = (1 << RUNLEVEL_BOOT);
    
    init_register_builtin_service(
        "serial",
        "Serial console driver",
        SERVICE_TYPE_SYSTEM,
        boot_level | (1 << RUNLEVEL_SINGLE) | (1 << RUNLEVEL_MULTI),
        0,  // Highest priority
        service_serial_start,
        service_serial_stop,
        false
    );
    
    init_register_builtin_service(
        "vga",
        "VGA text mode console",
        SERVICE_TYPE_SYSTEM,
        boot_level | (1 << RUNLEVEL_SINGLE) | (1 << RUNLEVEL_MULTI),
        1,
        service_vga_start,
        service_vga_stop,
        false
    );
    
    init_register_builtin_service(
        "keyboard",
        "Keyboard input driver",
        SERVICE_TYPE_SYSTEM,
        (1 << RUNLEVEL_SINGLE) | (1 << RUNLEVEL_MULTI),
        2,
        service_keyboard_start,
        service_keyboard_stop,
        false
    );
    
    init_register_builtin_service(
        "filesystem",
        "Virtual filesystem manager",
        SERVICE_TYPE_SYSTEM,
        boot_level | (1 << RUNLEVEL_SINGLE) | (1 << RUNLEVEL_MULTI),
        3,
        service_filesystem_start,
        service_filesystem_stop,
        false
    );
    
    // System daemons (runlevel 2 - MULTI)
    uint32_t multi_level = (1 << RUNLEVEL_MULTI);
    
    init_register_builtin_service(
        "syslogd",
        "System logging daemon",
        SERVICE_TYPE_DAEMON,
        multi_level,
        10,
        service_syslogd_start,
        service_syslogd_stop,
        true  // Auto-restart on failure
    );
    
    init_register_builtin_service(
        "crond",
        "Cron task scheduler daemon",
        SERVICE_TYPE_DAEMON,
        multi_level,
        11,
        service_crond_start,
        service_crond_stop,
        true
    );
    
    init_register_builtin_service(
        "network",
        "Network interface manager",
        SERVICE_TYPE_DAEMON,
        multi_level,
        5,
        service_network_start,
        service_network_stop,
        true
    );
    
    serial_puts("[INIT] Default services registered.\n");
}
