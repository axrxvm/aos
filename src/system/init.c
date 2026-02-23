/*
 * === AOS HEADER BEGIN ===
 * src/system/init.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
 * Init System Implementation
 */

#include <init.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <vga.h>
#include <arch.h>
#include <process.h>

// Maximum number of services
#define MAX_SERVICES 32

// Global init configuration
static init_config_t init_config = {
    .current_runlevel = RUNLEVEL_BOOT,
    .max_services = MAX_SERVICES,
    .verbose_mode = true,
};

// Service registry
static service_t* service_registry[MAX_SERVICES] = {0};
static uint32_t registered_services = 0;

static service_t* find_service(const char* name);

// Helper function to print init messages
static void init_log(const char* message) {
    if (init_config.verbose_mode) {
        serial_puts("[INIT] ");
        serial_puts(message);
        serial_puts("\n");
    }
}

// Initialize the init system
void init_system(void) {
    init_log("Init system starting...");
    memset(service_registry, 0, sizeof(service_registry));
    registered_services = 0;
    init_log("Init system ready.");
}

// Set the current runlevel
void init_set_runlevel(runlevel_t level) {
    if (level != init_config.current_runlevel) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Switching to runlevel %d", level);
        init_log(buf);
        
        // Stop services not in the new runlevel
        for (uint32_t i = 0; i < registered_services; i++) {
            if (service_registry[i] && 
                service_registry[i]->state == SERVICE_RUNNING &&
                !(service_registry[i]->runlevels & (1 << level))) {
                init_stop_service(service_registry[i]->name);
            }
        }
        
        init_config.current_runlevel = level;
        
        // Start services for the new runlevel
        init_start_runlevel(level);
    }
}

// Get the current runlevel
runlevel_t init_get_runlevel(void) {
    return init_config.current_runlevel;
}

// Register a service
int init_register_service(service_t* service) {
    if (!service || registered_services >= MAX_SERVICES) {
        init_log("Failed to register service: too many services or null service");
        return -1;
    }
    
    // Check if service already registered
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i] && strcmp(service_registry[i]->name, service->name) == 0) {
            init_log("Service already registered");
            return -1;
        }
    }
    
    // Initialize service state
    service->state = SERVICE_STOPPED;
    service->tid = 0;
    service->start_time = 0;
    service->restart_count = 0;
    
    // Register the service
    service_registry[registered_services++] = service;
    
    char buf[96];
    snprintf(buf, sizeof(buf), "Registered service: %s", service->name);
    init_log(buf);
    
    return 0;
}

int init_service_attach_task(const char* service_name, uint32_t tid) {
    service_t* service = find_service(service_name);
    if (!service) {
        return -1;
    }

    service->tid = tid;
    return 0;
}

// Find a service by name
static service_t* find_service(const char* name) {
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i] && strcmp(service_registry[i]->name, name) == 0) {
            return service_registry[i];
        }
    }
    return NULL;
}

// Start a service by name
int init_start_service(const char* name) {
    service_t* service = find_service(name);
    if (!service) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Service not found: %s", name);
        init_log(buf);
        return -1;
    }
    
    if (service->state == SERVICE_RUNNING) {
        return 0; // Already running
    }
    
    if (!service->start_fn) {
        init_log("Service has no start function");
        return -1;
    }
    
    char buf[96];
    snprintf(buf, sizeof(buf), "Starting service: %s", name);
    init_log(buf);
    
    // Call the service start function
    service->start_fn();
    if (service->tid == 0) {
        char task_name[96];
        snprintf(task_name, sizeof(task_name), "svc:%s", service->name);
        pid_t tid = process_register_kernel_task(task_name, TASK_TYPE_SERVICE, PRIORITY_NORMAL);
        if (tid > 0) {
            service->tid = (uint32_t)tid;
        }
    } else {
        process_mark_task_state((pid_t)service->tid, PROCESS_RUNNING);
    }
    service->state = SERVICE_RUNNING;
    service->start_time = arch_timer_get_ticks();
    service->restart_count = 0;
    
    snprintf(buf, sizeof(buf), "Service started: %s", name);
    init_log(buf);
    
    return 0;
}

// Stop a service by name
int init_stop_service(const char* name) {
    service_t* service = find_service(name);
    if (!service) {
        return -1;
    }
    
    if (service->state == SERVICE_STOPPED) {
        return 0; // Already stopped
    }
    
    if (!service->stop_fn) {
        service->state = SERVICE_STOPPED;
        if (service->tid != 0) {
            process_finish_kernel_task((pid_t)service->tid, 0);
            service->tid = 0;
        }
        return 0;
    }
    
    char buf[96];
    snprintf(buf, sizeof(buf), "Stopping service: %s", name);
    init_log(buf);
    
    // Call the service stop function
    service->stop_fn();
    service->state = SERVICE_STOPPED;
    if (service->tid != 0) {
        process_finish_kernel_task((pid_t)service->tid, 0);
    }
    service->tid = 0;
    
    snprintf(buf, sizeof(buf), "Service stopped: %s", name);
    init_log(buf);
    
    return 0;
}

// Restart a service by name
int init_restart_service(const char* name) {
    service_t* service = find_service(name);
    if (!service) {
        return -1;
    }
    
    init_stop_service(name);
    return init_start_service(name);
}

// Get service status
service_state_t init_get_service_state(const char* name) {
    service_t* service = find_service(name);
    if (service) {
        return service->state;
    }
    return SERVICE_STOPPED;
}

// List all registered services
void init_list_services(void) {
    vga_puts("Registered Services:\n");
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i]) {
            service_t* svc = service_registry[i];
            vga_puts("  ");
            vga_puts(svc->name);
            vga_puts(" - ");
            vga_puts(svc->description);
            vga_puts(" [");
            
            switch (svc->state) {
                case SERVICE_RUNNING:
                    vga_puts("RUNNING");
                    break;
                case SERVICE_STOPPED:
                    vga_puts("STOPPED");
                    break;
                case SERVICE_FAILED:
                    vga_puts("FAILED");
                    break;
                default:
                    vga_puts("UNKNOWN");
            }
            
            vga_puts("]\n");
            if (svc->tid != 0) {
                vga_puts("    TID: ");
                char tid_buf[16];
                itoa((int)svc->tid, tid_buf, 10);
                vga_puts(tid_buf);
                vga_puts("\n");
            }
        }
    }
}

// Start all services for a given runlevel
void init_start_runlevel(runlevel_t level) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Starting runlevel %d services", level);
    init_log(buf);
    
    // Sort services by priority and start them
    service_t* to_start[MAX_SERVICES];
    uint32_t count = 0;
    
    // Collect services for this runlevel
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i] && 
            (service_registry[i]->runlevels & (1 << level)) &&
            service_registry[i]->state != SERVICE_RUNNING) {
            to_start[count++] = service_registry[i];
        }
    }
    
    // Sort by priority (simple bubble sort)
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = 0; j < count - 1 - i; j++) {
            if (to_start[j]->priority > to_start[j + 1]->priority) {
                service_t* temp = to_start[j];
                to_start[j] = to_start[j + 1];
                to_start[j + 1] = temp;
            }
        }
    }
    
    // Start services in priority order
    for (uint32_t i = 0; i < count; i++) {
        init_start_service(to_start[i]->name);
    }
    
    snprintf(buf, sizeof(buf), "Runlevel %d initialization complete", level);
    init_log(buf);
}

// Stop all services
void init_shutdown(void) {
    init_log("Shutting down services...");
    
    // Collect running services
    service_t* to_stop[MAX_SERVICES];
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i] && service_registry[i]->state == SERVICE_RUNNING) {
            to_stop[count++] = service_registry[i];
        }
    }
    
    // Stop services in reverse priority order
    for (uint32_t i = count; i > 0; i--) {
        init_stop_service(to_stop[i - 1]->name);
    }
    
    init_log("All services stopped.");
}

// Check and restart failed services
void init_check_services(void) {
    for (uint32_t i = 0; i < registered_services; i++) {
        if (service_registry[i]) {
            service_t* service = service_registry[i];
            if (service->type == SERVICE_TYPE_DAEMON && 
                service->auto_restart &&
                service->state == SERVICE_FAILED &&
                service->restart_count < 3) {
                
                char buf[96];
                snprintf(buf, sizeof(buf), "Restarting failed service: %s (attempt %d/3)",
                        service->name, service->restart_count + 1);
                init_log(buf);
                
                service->restart_count++;
                init_start_service(service->name);
            }
        }
    }
}

// Print service status
void init_service_status(const char* name) {
    service_t* service = find_service(name);
    if (!service) {
        vga_puts("Service not found: ");
        vga_puts(name);
        vga_puts("\n");
        return;
    }
    
    vga_puts("Service: ");
    vga_puts(service->name);
    vga_puts("\n");
    vga_puts("  Description: ");
    vga_puts(service->description);
    vga_puts("\n");
    vga_puts("  State: ");
    
    switch (service->state) {
        case SERVICE_RUNNING:
            vga_puts("RUNNING");
            break;
        case SERVICE_STOPPED:
            vga_puts("STOPPED");
            break;
        case SERVICE_FAILED:
            vga_puts("FAILED");
            break;
        default:
            vga_puts("UNKNOWN");
    }
    
    vga_puts("\n");
    vga_puts("  Type: ");
    
    switch (service->type) {
        case SERVICE_TYPE_SYSTEM:
            vga_puts("SYSTEM");
            break;
        case SERVICE_TYPE_DAEMON:
            vga_puts("DAEMON");
            break;
        case SERVICE_TYPE_ONESHOT:
            vga_puts("ONESHOT");
            break;
        default:
            vga_puts("UNKNOWN");
    }
    
    vga_puts("\n");
    if (service->tid != 0) {
        vga_puts("  TID: ");
        char tid_buf[16];
        itoa((int)service->tid, tid_buf, 10);
        vga_puts(tid_buf);
        vga_puts("\n");
    }
}

// Enable verbose output
void init_set_verbose(bool verbose) {
    init_config.verbose_mode = verbose;
}
