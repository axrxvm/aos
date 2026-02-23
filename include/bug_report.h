/*
 * === AOS HEADER BEGIN ===
 * include/bug_report.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.1
 * === AOS HEADER END ===
 */

#ifndef BUG_REPORT_H
#define BUG_REPORT_H

#include <stdint.h>
#include <arch/isr.h>

#define BUG_REPORT_ENDPOINT "http://api.aosproject.workers.dev/report"
#define BUG_REPORT_LEVEL_BUG "bug"
#define BUG_REPORT_LEVEL_CRASH "crash"
#define BUG_REPORT_LEVEL_ERROR "error"
#define BUG_REPORT_LEVEL_INFO "info"

typedef enum {
    BUG_BOOT_STAGE_UNKNOWN = 0,
    BUG_BOOT_STAGE_EARLY = 1,
    BUG_BOOT_STAGE_FS_READY = 2,
    BUG_BOOT_STAGE_SERVICES = 3,
    BUG_BOOT_STAGE_APM_MODULES = 4,
    BUG_BOOT_STAGE_USERSPACE = 5,
    BUG_BOOT_STAGE_STABLE = 6
} bug_boot_stage_t;

// Initialize subsystem once VFS is available.
void bug_report_init(void);

// Track boot lifecycle for panic recovery.
void bug_report_set_boot_stage(bug_boot_stage_t stage);
void bug_report_boot_success(void);

// Returns non-zero when previous boot ended with panic.
int bug_report_has_previous_panic(void);

// Performs rollback/recovery for previous panic state.
// Returns 1 if rollback was applied, 0 if no rollback was needed, negative on error.
int bug_report_recover_after_panic(void);

// Captures panic context and queues crash report (best effort, panic-safe).
void bug_report_capture_panic(registers_t* regs, const char* message, const char* file, uint32_t line);

// Queues a non-panic report payload on disk for background delivery.
// Returns 0 if queued successfully, negative on error.
int bug_report_submit(const char* level, const char* message, const char* stack, const char* context);

// Attempts delivery of queued report.
// Returns 0 if nothing pending or send succeeded, 1 if still pending, negative on hard error.
int bug_report_process_pending(void);

#endif // BUG_REPORT_H
