/*
 * === AOS HEADER BEGIN ===
 * src/system/bgtask.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.1
 * === AOS HEADER END ===
 */

#include <bgtask.h>
#include <bug_report.h>
#include <process.h>
#include <serial.h>
#include <string.h>
#include <time_subsystem.h>

#define BGTASK_MAX_JOBS 32
#define BGTASK_WORKER_PRIORITY PRIORITY_REALTIME

typedef enum {
    BGTASK_KIND_GENERIC = 0,
    BGTASK_KIND_REPORT_DELIVERY = 1,
    BGTASK_KIND_TIMESYNC = 2,
} bgtask_job_kind_t;

typedef struct {
    char name[32];
    bgtask_job_fn_t fn;
    void* arg;
    bgtask_job_cleanup_t cleanup;
    bgtask_job_kind_t kind;
} bgtask_job_t;

static bgtask_job_t g_queue[BGTASK_MAX_JOBS];
static uint32_t g_head = 0;
static uint32_t g_tail = 0;
static uint32_t g_count = 0;

static uint8_t g_initialized = 0;
static uint8_t g_service_enabled = 0;
static uint8_t g_worker_running = 0;
static uint8_t g_report_job_pending = 0;
static uint8_t g_timesync_job_pending = 0;
static pid_t g_worker_pid = -1;

static const char* job_kind_name(bgtask_job_kind_t kind) {
    switch (kind) {
        case BGTASK_KIND_REPORT_DELIVERY: return "report";
        case BGTASK_KIND_TIMESYNC: return "timesync";
        default: return "generic";
    }
}

static void bgtask_log_queue_state(const char* prefix) {
    char line[160];
    snprintf(line, sizeof(line),
             "[BGTASK] %s q=%u head=%u tail=%u svc=%u run=%u report_pending=%u timesync_pending=%u\n",
             prefix ? prefix : "state",
             (unsigned)g_count,
             (unsigned)g_head,
             (unsigned)g_tail,
             (unsigned)g_service_enabled,
             (unsigned)g_worker_running,
             (unsigned)g_report_job_pending,
             (unsigned)g_timesync_job_pending);
    serial_puts(line);
}

static void bgtask_lock(void) {
    process_set_preempt_disabled(1);
}

static void bgtask_unlock(void) {
    process_set_preempt_disabled(0);
}

static int enqueue_locked(const bgtask_job_t* job) {
    if (!job || g_count >= BGTASK_MAX_JOBS) {
        return -1;
    }

    g_queue[g_tail] = *job;
    g_tail = (g_tail + 1) % BGTASK_MAX_JOBS;
    g_count++;
    return 0;
}

static int enqueue_front_locked(const bgtask_job_t* job) {
    if (!job || g_count >= BGTASK_MAX_JOBS) {
        return -1;
    }

    g_head = (g_head + BGTASK_MAX_JOBS - 1) % BGTASK_MAX_JOBS;
    g_queue[g_head] = *job;
    g_count++;
    return 0;
}

static int dequeue_locked(bgtask_job_t* out) {
    if (!out || g_count == 0) {
        return -1;
    }

    *out = g_queue[g_head];
    g_head = (g_head + 1) % BGTASK_MAX_JOBS;
    g_count--;
    return 0;
}

static int report_delivery_job(void* arg) {
    (void)arg;
    serial_puts("[BGTASK] Running report-delivery job\n");
    int rc = bug_report_process_pending();
    if (rc == 0) {
        serial_puts("[BGTASK] Report delivery complete\n");
        return 0;
    }
    serial_puts("[BGTASK] Report delivery deferred, will retry\n");
    return 1;
}

static int timesync_job(void* arg) {
    (void)arg;
    serial_puts("[BGTASK] Running timesync job\n");
    if (time_sync_now() == 0) {
        serial_puts("[BGTASK] Time sync completed\n");
        return 0;
    }

    serial_puts("[BGTASK] Time sync failed, will retry\n");
    return 1;
}

static void bgtask_worker_main(void) {
    char line[128];
    process_set_current_identity("bgtaskd", TASK_TYPE_SERVICE, BGTASK_WORKER_PRIORITY, 0);
    snprintf(line, sizeof(line), "[BGTASK] Worker thread online pid=%d priority=%d\n",
             process_getpid(), BGTASK_WORKER_PRIORITY);
    serial_puts(line);

    while (1) {
        bgtask_job_t job;
        int have_job = 0;

        if (!g_service_enabled) {
            process_sleep(250);
            continue;
        }

        bgtask_lock();
        if (dequeue_locked(&job) == 0) {
            have_job = 1;
            snprintf(line, sizeof(line),
                     "[BGTASK] Dequeued job '%s' kind=%s q_remaining=%u\n",
                     job.name,
                     job_kind_name(job.kind),
                     (unsigned)g_count);
            serial_puts(line);
        }
        bgtask_unlock();

        if (!have_job) {
            process_sleep(100);
            continue;
        }

        int rc = -1;
        if (job.fn) {
            rc = job.fn(job.arg);
        }

        if (rc > 0) {
            int requeued = 0;
            bgtask_lock();
            if (job.kind == BGTASK_KIND_REPORT_DELIVERY) {
                requeued = (enqueue_front_locked(&job) == 0) ? 1 : 0;
            } else {
                requeued = (enqueue_locked(&job) == 0) ? 1 : 0;
            }
            if (!requeued && job.kind == BGTASK_KIND_REPORT_DELIVERY) {
                g_report_job_pending = 0;
            } else if (!requeued && job.kind == BGTASK_KIND_TIMESYNC) {
                g_timesync_job_pending = 0;
            }
            bgtask_unlock();

            snprintf(line, sizeof(line),
                     "[BGTASK] Job '%s' requested retry (requeued=%d)\n",
                     job.name, requeued);
            serial_puts(line);

            if (!requeued && job.cleanup) {
                job.cleanup(job.arg);
            }
            process_sleep(2000);
            continue;
        }

        if (job.kind == BGTASK_KIND_REPORT_DELIVERY) {
            bgtask_lock();
            g_report_job_pending = 0;
            bgtask_unlock();
        } else if (job.kind == BGTASK_KIND_TIMESYNC) {
            bgtask_lock();
            g_timesync_job_pending = 0;
            bgtask_unlock();
        }

        if (job.cleanup) {
            job.cleanup(job.arg);
        }

        process_yield();
    }
}

void bgtask_init(void) {
    if (g_initialized) {
        return;
    }

    memset(g_queue, 0, sizeof(g_queue));
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_service_enabled = 0;
    g_worker_running = 0;
    g_report_job_pending = 0;
    g_timesync_job_pending = 0;
    g_worker_pid = -1;
    g_initialized = 1;
    bgtask_log_queue_state("init complete");
}

int bgtask_service_start(void) {
    if (!g_initialized) {
        bgtask_init();
    }

    g_service_enabled = 1;
    bgtask_log_queue_state("service start requested");

    if (g_worker_running && g_worker_pid > 0) {
        char line[96];
        snprintf(line, sizeof(line), "[BGTASK] Worker already active pid=%d\n", g_worker_pid);
        serial_puts(line);
        return g_worker_pid;
    }

    pid_t pid = process_create_kernel_thread("bgtaskd", bgtask_worker_main, BGTASK_WORKER_PRIORITY);
    if (pid < 0) {
        serial_puts("[BGTASK] Failed to start worker thread\n");
        return -1;
    }

    g_worker_pid = pid;
    g_worker_running = 1;
    char line[128];
    snprintf(line, sizeof(line),
             "[BGTASK] Service started, worker pid=%d priority=%d\n",
             pid, BGTASK_WORKER_PRIORITY);
    serial_puts(line);
    bgtask_log_queue_state("after worker spawn");

    // Kick the scheduler immediately so the new worker can run at least once.
    serial_puts("[BGTASK] Kicking scheduler for worker first-run\n");
    process_yield();
    serial_puts("[BGTASK] Scheduler kick returned\n");
    return pid;
}

void bgtask_service_stop(void) {
    g_service_enabled = 0;
    serial_puts("[BGTASK] Service paused\n");
}

int bgtask_queue_job(const char* name, bgtask_job_fn_t fn, void* arg, bgtask_job_cleanup_t cleanup) {
    bgtask_job_t job;

    if (!fn) {
        return -1;
    }
    if (!g_initialized) {
        bgtask_init();
    }

    memset(&job, 0, sizeof(job));
    if (name && *name) {
        strncpy(job.name, name, sizeof(job.name) - 1);
        job.name[sizeof(job.name) - 1] = '\0';
    } else {
        strncpy(job.name, "job", sizeof(job.name) - 1);
        job.name[sizeof(job.name) - 1] = '\0';
    }
    job.fn = fn;
    job.arg = arg;
    job.cleanup = cleanup;
    job.kind = BGTASK_KIND_GENERIC;

    bgtask_lock();
    int rc = enqueue_locked(&job);
    if (rc == 0) {
        char line[128];
        snprintf(line, sizeof(line),
                 "[BGTASK] Enqueued generic job '%s' q=%u\n",
                 job.name, (unsigned)g_count);
        serial_puts(line);
    }
    bgtask_unlock();

    if (rc != 0) {
        serial_puts("[BGTASK] Failed to enqueue generic job (queue full)\n");
    }
    return rc;
}

int bgtask_queue_report_delivery(void) {
    bgtask_job_t job;

    if (!g_initialized) {
        bgtask_init();
    }

    bgtask_lock();
    if (g_report_job_pending) {
        serial_puts("[BGTASK] Report delivery already queued; skipping duplicate\n");
        bgtask_unlock();
        return 0;
    }

    memset(&job, 0, sizeof(job));
    strncpy(job.name, "report-delivery", sizeof(job.name) - 1);
    job.name[sizeof(job.name) - 1] = '\0';
    job.fn = report_delivery_job;
    job.arg = NULL;
    job.cleanup = NULL;
    job.kind = BGTASK_KIND_REPORT_DELIVERY;

    // Report delivery is urgent: place it at the front of the queue.
    if (enqueue_front_locked(&job) != 0) {
        serial_puts("[BGTASK] Failed to enqueue report delivery job (queue full)\n");
        bgtask_unlock();
        return -1;
    }

    g_report_job_pending = 1;
    {
        char line[160];
        snprintf(line, sizeof(line),
                 "[BGTASK] Queued report delivery at front q=%u svc=%u run=%u\n",
                 (unsigned)g_count,
                 (unsigned)g_service_enabled,
                 (unsigned)g_worker_running);
        serial_puts(line);
    }
    bgtask_unlock();

    // If worker is already active, prompt scheduler to pick it quickly.
    if (g_service_enabled && g_worker_running) {
        serial_puts("[BGTASK] Kicking scheduler after report enqueue\n");
        process_yield();
        serial_puts("[BGTASK] Post-enqueue scheduler kick returned\n");
    }
    return 0;
}

int bgtask_queue_timesync(void) {
    bgtask_job_t job;

    if (!g_initialized) {
        bgtask_init();
    }

    bgtask_lock();
    if (g_timesync_job_pending) {
        serial_puts("[BGTASK] Timesync already queued; skipping duplicate\n");
        bgtask_unlock();
        return 0;
    }

    memset(&job, 0, sizeof(job));
    strncpy(job.name, "timesync", sizeof(job.name) - 1);
    job.name[sizeof(job.name) - 1] = '\0';
    job.fn = timesync_job;
    job.arg = NULL;
    job.cleanup = NULL;
    job.kind = BGTASK_KIND_TIMESYNC;

    if (enqueue_locked(&job) != 0) {
        serial_puts("[BGTASK] Failed to enqueue timesync job (queue full)\n");
        bgtask_unlock();
        return -1;
    }

    g_timesync_job_pending = 1;
    {
        char line[128];
        snprintf(line, sizeof(line), "[BGTASK] Queued timesync job q=%u\n", (unsigned)g_count);
        serial_puts(line);
    }
    bgtask_unlock();
    return 0;
}

uint32_t bgtask_pending_count(void) {
    uint32_t count;
    bgtask_lock();
    count = g_count;
    bgtask_unlock();
    return count;
}
