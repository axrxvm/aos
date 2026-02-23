/*
 * === AOS HEADER BEGIN ===
 * include/bgtask.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.1
 * === AOS HEADER END ===
 */

#ifndef BGTASK_H
#define BGTASK_H

#include <stdint.h>

typedef int (*bgtask_job_fn_t)(void* arg);
typedef void (*bgtask_job_cleanup_t)(void* arg);

// Initializes queue state (idempotent).
void bgtask_init(void);

// Service lifecycle hooks.
// Returns worker PID when a worker is active, negative on failure.
int bgtask_service_start(void);
void bgtask_service_stop(void);

// Generic background job enqueue.
int bgtask_queue_job(const char* name, bgtask_job_fn_t fn, void* arg, bgtask_job_cleanup_t cleanup);

// Built-in background action: attempt pending crash/bug report delivery.
int bgtask_queue_report_delivery(void);

// Built-in background action: synchronize wall clock.
int bgtask_queue_timesync(void);

// Number of queued jobs.
uint32_t bgtask_pending_count(void);

#endif // BGTASK_H
