/*
 * === AOS HEADER BEGIN ===
 * include/init.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
 * Init System - Service Management and Initialization Control
 */

#ifndef INIT_H
#define INIT_H

#include <stdint.h>
#include <stdbool.h>

// Service states
typedef enum {
    SERVICE_STOPPED = 0,
    SERVICE_RUNNING = 1,
    SERVICE_FAILED = 2,
} service_state_t;

// Service types
typedef enum {
    SERVICE_TYPE_SYSTEM = 0,    // Core system service
    SERVICE_TYPE_DAEMON = 1,    // Background daemon
    SERVICE_TYPE_ONESHOT = 2,   // Run once at startup
} service_type_t;

// Service control levels (runlevels)
typedef enum {
    RUNLEVEL_BOOT = 0,      // Boot time initialization
    RUNLEVEL_SINGLE = 1,    // Single user mode
    RUNLEVEL_MULTI = 2,     // Multi-user mode
    RUNLEVEL_SHUTDOWN = 3,  // System shutdown
} runlevel_t;

// Service structure
typedef struct {
    const char* name;                // Service name (e.g., "syslogd", "getty")
    const char* description;         // Human-readable description
    service_type_t type;             // Type of service
    uint32_t runlevels;              // Bitmask of runlevels (1 << RUNLEVEL_X)
    uint32_t priority;               // Priority (0=highest, 255=lowest)
    void (*start_fn)(void);          // Function to start the service
    void (*stop_fn)(void);           // Function to stop the service
    uint32_t pid;                    // Process ID (for daemons)
    service_state_t state;           // Current state
    uint32_t start_time;             // Time service was started (in ticks)
    uint32_t restart_count;          // Number of restart attempts
    bool auto_restart;               // Whether to auto-restart on failure
} service_t;

// Init system configuration
typedef struct {
    runlevel_t current_runlevel;
    uint32_t max_services;
    bool verbose_mode;
} init_config_t;

// Initialize the init system
void init_system(void);

// Set the current runlevel
void init_set_runlevel(runlevel_t level);

// Get the current runlevel
runlevel_t init_get_runlevel(void);

// Register a service
int init_register_service(service_t* service);

// Start a service by name
int init_start_service(const char* name);

// Stop a service by name
int init_stop_service(const char* name);

// Restart a service by name
int init_restart_service(const char* name);

// Get service status
service_state_t init_get_service_state(const char* name);

// List all registered services
void init_list_services(void);

// Start all services for a given runlevel
void init_start_runlevel(runlevel_t level);

// Stop all services
void init_shutdown(void);

// Check and restart failed services (called by watchdog)
void init_check_services(void);

// Print service status
void init_service_status(const char* name);

// Enable verbose output
void init_set_verbose(bool verbose);

#endif // INIT_H
