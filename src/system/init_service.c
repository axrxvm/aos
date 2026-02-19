/*
 * === AOS HEADER BEGIN ===
 * src/system/init_service.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
 * Init Service Registry Implementation - Built-in and Script-based Services
 */

#include <init_service.h>
#include <init.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <fs/vfs.h>
#include <time_subsystem.h>

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
    serial_puts("VGA console service started\n");
}

static void service_vga_stop(void) {
    serial_puts("VGA console service stopped\n");
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

// Time synchronization service
static void service_timesync_start(void) {
    serial_puts("Time sync service started\n");
    if (time_sync_now() == 0) {
        serial_puts("Time sync service: wall clock synchronized\n");
    } else {
        serial_puts("Time sync service: synchronization failed\n");
    }
}

static void service_timesync_stop(void) {
    serial_puts("Time sync service stopped\n");
}


// Init.d Script Execution

static const char* operation_to_string(service_operation_t operation) {
    switch (operation) {
        case SERVICE_OP_START: return "start";
        case SERVICE_OP_STOP: return "stop";
        case SERVICE_OP_RESTART: return "restart";
        case SERVICE_OP_STATUS: return "status";
        case SERVICE_OP_RELOAD: return "reload";
        default: return "unknown";
    }
}

static int execute_service_operation(const char* service_name, service_operation_t operation) {
    switch (operation) {
        case SERVICE_OP_START:
            return init_start_service(service_name);
        case SERVICE_OP_STOP:
            return init_stop_service(service_name);
        case SERVICE_OP_RESTART:
        case SERVICE_OP_RELOAD:
            return init_restart_service(service_name);
        case SERVICE_OP_STATUS:
            init_service_status(service_name);
            return 0;
        default:
            return -1;
    }
}

static int script_extract_service_name(const char* script,
                                       uint32_t script_len,
                                       char* out_name,
                                       uint32_t out_name_size) {
    if (!script || !out_name || out_name_size == 0) {
        return -1;
    }

    uint32_t i = 0;
    while (i < script_len) {
        uint32_t line_start = i;
        while (line_start < script_len &&
               (script[line_start] == ' ' || script[line_start] == '\t')) {
            line_start++;
        }

        if (line_start + 8 < script_len &&
            strncmp(script + line_start, "service=", 8) == 0) {
            const char* value_start = script + line_start + 8;
            uint32_t value_len = 0;
            while ((line_start + 8 + value_len) < script_len) {
                char c = value_start[value_len];
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '#') {
                    break;
                }
                value_len++;
            }

            if (value_len > 0) {
                if (value_len >= out_name_size) {
                    value_len = out_name_size - 1;
                }
                memcpy(out_name, value_start, value_len);
                out_name[value_len] = '\0';
                return 0;
            }
        }

        while (i < script_len && script[i] != '\n') {
            i++;
        }
        if (i < script_len) {
            i++;
        }
    }

    return -1;
}

// Execute an init.d style service script
// In a real filesystem implementation, this would load and parse shell scripts
int init_script_exec(const char* script_name, service_operation_t operation) {
    if (!script_name || !*script_name) {
        return SERVICE_SCRIPT_FAILED;
    }

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

    char script_buf[1024];
    uint32_t total_read = 0;
    while (total_read < sizeof(script_buf) - 1) {
        int n = vfs_read(fd, script_buf + total_read, sizeof(script_buf) - 1 - total_read);
        if (n <= 0) {
            break;
        }
        total_read += (uint32_t)n;
    }
    script_buf[total_read] = '\0';
    vfs_close(fd);

    char service_name[64];
    strncpy(service_name, script_name, sizeof(service_name) - 1);
    service_name[sizeof(service_name) - 1] = '\0';

    script_extract_service_name(script_buf, total_read, service_name, sizeof(service_name));

    int ret = execute_service_operation(service_name, operation);
    if (ret != 0) {
        serial_puts("[INIT] Script operation failed: ");
        serial_puts(script_name);
        serial_puts(" (");
        serial_puts(operation_to_string(operation));
        serial_puts(")\n");
        return SERVICE_SCRIPT_FAILED;
    }

    serial_puts("[INIT] Script operation complete: ");
    serial_puts(script_name);
    serial_puts(" -> ");
    serial_puts(service_name);
    serial_puts(" (");
    serial_puts(operation_to_string(operation));
    serial_puts(")\n");
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

    fd = vfs_open("/etc/init.d", O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        serial_puts("[INIT] Failed to open /etc/init.d for listing\n");
        return;
    }

    serial_puts("[INIT] Loading init.d scripts...\n");

    dirent_t entry;
    uint32_t loaded_count = 0;

    while (vfs_readdir(fd, &entry) == VFS_OK) {
        if (entry.name[0] == '\0') {
            continue;
        }
        if ((entry.name[0] == '.' && entry.name[1] == '\0') ||
            (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0')) {
            continue;
        }

        serial_puts("[INIT] Found script: ");
        serial_puts(entry.name);
        serial_puts("\n");
        loaded_count++;
    }

    vfs_close(fd);

    char count_buf[16];
    itoa(loaded_count, count_buf, 10);
    serial_puts("[INIT] init.d scripts discovered: ");
    serial_puts(count_buf);
    serial_puts("\n");
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
    service->tid = 0;
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

    init_register_builtin_service(
        "timesync",
        "Timezone-aware wall clock synchronization",
        SERVICE_TYPE_DAEMON,
        multi_level,
        6,
        service_timesync_start,
        service_timesync_stop,
        true
    );
    
    serial_puts("[INIT] Default services registered.\n");
}
