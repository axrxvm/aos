/*
 * === AOS HEADER BEGIN ===
 * include/init_service.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
 * Init System Service Registry - init.d style service scripts
 */

#ifndef INIT_SERVICE_H
#define INIT_SERVICE_H

#include <init.h>

// Service script status codes
typedef enum {
    SERVICE_SCRIPT_SUCCESS = 0,
    SERVICE_SCRIPT_NOT_FOUND = 127,
    SERVICE_SCRIPT_FAILED = 1,
} service_script_status_t;

// Service script operations
typedef enum {
    SERVICE_OP_START = 0,
    SERVICE_OP_STOP = 1,
    SERVICE_OP_RESTART = 2,
    SERVICE_OP_STATUS = 3,
    SERVICE_OP_RELOAD = 4,
} service_operation_t;

// Script execution context
typedef struct {
    const char* script_path;      // Path to init.d script
    service_operation_t operation; // Operation to perform
    int exit_code;                // Exit code from script
} service_script_context_t;

// Execute an init.d style service script
int init_script_exec(const char* script_name, service_operation_t operation);

// Load services from init.d directory
void init_load_scripts(void);

// Register a built-in service
int init_register_builtin_service(const char* name, 
                                  const char* description,
                                  service_type_t type,
                                  uint32_t runlevels,
                                  uint32_t priority,
                                  void (*start_fn)(void),
                                  void (*stop_fn)(void),
                                  bool auto_restart);

// Initialize default system services
void init_default_services(void);

#endif // INIT_SERVICE_H
