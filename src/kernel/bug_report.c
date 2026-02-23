/*
 * === AOS HEADER BEGIN ===
 * src/kernel/bug_report.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.1
 * === AOS HEADER END ===
 */

#include <bug_report.h>
#include <fs/vfs.h>
#include <fs_layout.h>
#include <net/http.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <version.h>
#include <arch.h>
#include <apm.h>

#define BUG_REPORT_DIR "/sys/log/bugreport"
#define BUG_REPORT_BOOT_STATE_FILE "/sys/log/bugreport/boot.state"
#define BUG_REPORT_PENDING_FILE "/sys/log/bugreport/pending.json"
#define BUG_REPORT_LAST_PANIC_FILE "/sys/log/bugreport/last_panic.txt"
#define BUG_REPORT_RECOVERY_FILE "/sys/log/bugreport/recovery.log"

static uint8_t g_initialized = 0;
static bug_boot_stage_t g_current_stage = BUG_BOOT_STAGE_UNKNOWN;
static int g_current_clean = 0;
static int g_current_panic = 0;

static bug_boot_stage_t g_prev_stage = BUG_BOOT_STAGE_UNKNOWN;
static int g_prev_clean = 1;
static int g_prev_panic = 0;

static volatile uint8_t g_capture_guard = 0;

static int bug_level_valid(const char* level) {
    if (!level || !*level) return 0;
    return (strcmp(level, BUG_REPORT_LEVEL_BUG) == 0 ||
            strcmp(level, BUG_REPORT_LEVEL_CRASH) == 0 ||
            strcmp(level, BUG_REPORT_LEVEL_ERROR) == 0 ||
            strcmp(level, BUG_REPORT_LEVEL_INFO) == 0);
}

static const char* bug_level_or_default(const char* level) {
    if (!bug_level_valid(level)) return BUG_REPORT_LEVEL_BUG;
    return level;
}

static const char* stage_to_string(bug_boot_stage_t stage) {
    switch (stage) {
        case BUG_BOOT_STAGE_EARLY: return "early";
        case BUG_BOOT_STAGE_FS_READY: return "fs_ready";
        case BUG_BOOT_STAGE_SERVICES: return "services";
        case BUG_BOOT_STAGE_APM_MODULES: return "apm_modules";
        case BUG_BOOT_STAGE_USERSPACE: return "userspace";
        case BUG_BOOT_STAGE_STABLE: return "stable";
        default: return "unknown";
    }
}

static int write_text_file(const char* path, const char* text) {
    if (!path || !text) return -1;

    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    int len = (int)strlen(text);
    int written = vfs_write(fd, text, (uint32_t)len);
    vfs_close(fd);
    return written == len ? 0 : -1;
}

static int read_text_file(const char* path, char* buffer, uint32_t size) {
    if (!path || !buffer || size == 0) return -1;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    int bytes = vfs_read(fd, buffer, size - 1);
    vfs_close(fd);
    if (bytes < 0) return -1;

    buffer[bytes] = '\0';
    return bytes;
}

static int ensure_bug_report_dirs(void) {
    vnode_t* node = vfs_resolve_path(FS_SYS_LOG_DIR);
    if (!node) {
        if (vfs_mkdir(FS_SYS_LOG_DIR) != VFS_OK) {
            return -1;
        }
    }

    node = vfs_resolve_path(BUG_REPORT_DIR);
    if (!node) {
        if (vfs_mkdir(BUG_REPORT_DIR) != VFS_OK) {
            return -1;
        }
    }

    return 0;
}

static int parse_state_value(const char* text, const char* key, int fallback) {
    char needle[32];
    if (!text || !key) return fallback;

    snprintf(needle, sizeof(needle), "%s=", key);
    const char* start = strstr(text, needle);
    if (!start) return fallback;
    start += strlen(needle);
    return atoi(start);
}

static void load_previous_state(void) {
    char state[128];
    int bytes = read_text_file(BUG_REPORT_BOOT_STATE_FILE, state, sizeof(state));
    if (bytes <= 0) {
        g_prev_stage = BUG_BOOT_STAGE_UNKNOWN;
        g_prev_clean = 1;
        g_prev_panic = 0;
        return;
    }

    g_prev_stage = (bug_boot_stage_t)parse_state_value(state, "stage", BUG_BOOT_STAGE_UNKNOWN);
    g_prev_clean = parse_state_value(state, "clean", 1) ? 1 : 0;
    g_prev_panic = parse_state_value(state, "panic", 0) ? 1 : 0;
}

static int save_current_state(void) {
    char state[128];
    snprintf(state, sizeof(state), "stage=%u\nclean=%u\npanic=%u\n",
             (unsigned)g_current_stage, (unsigned)g_current_clean, (unsigned)g_current_panic);
    return write_text_file(BUG_REPORT_BOOT_STATE_FILE, state);
}

static int build_report_payload(char* out, uint32_t out_size,
                               const char* level, const char* message,
                               const char* stack, const char* context) {
    if (!out || out_size == 0 || !message || !*message) return -1;

    int written = snprintf(out, out_size,
                           "aOS report\n"
                           "message: %s\n"
                           "level: %s\n"
                           "version: %s\n"
                           "device: %s\n"
                           "os: aOS\n"
                           "boot_stage: %s",
                           message,
                           bug_level_or_default(level),
                           AOS_VERSION_SHORT,
                           arch_get_name(),
                           stage_to_string(g_current_stage));
    if (written < 0 || (uint32_t)written >= out_size) {
        return -1;
    }

    if (stack && *stack) {
        int n = snprintf(out + written, out_size - (uint32_t)written,
                         "\nstack: %s", stack);
        if (n < 0 || (uint32_t)n >= (out_size - (uint32_t)written)) {
            return -1;
        }
        written += n;
    }

    if (context && *context) {
        int n = snprintf(out + written, out_size - (uint32_t)written,
                         "\ncontext: %s", context);
        if (n < 0 || (uint32_t)n >= (out_size - (uint32_t)written)) {
            return -1;
        }
    }
    return 0;
}

static int send_payload(const char* payload) {
    int result = -1;
    int send_rc = -1;
    http_request_t* req = NULL;
    http_response_t* resp = NULL;
    char line[256];

    if (!payload || !*payload) return -1;

    snprintf(line, sizeof(line), "[BUG] Sending report to %s (payload=%u bytes)\n",
             BUG_REPORT_ENDPOINT, (unsigned)strlen(payload));
    serial_puts(line);

    req = http_request_create(HTTP_METHOD_POST, BUG_REPORT_ENDPOINT);
    if (!req) {
        serial_puts("[BUG] Failed to allocate HTTP request\n");
        return -1;
    }

    resp = http_response_create();
    if (!resp) {
        serial_puts("[BUG] Failed to allocate HTTP response\n");
        http_request_free(req);
        return -1;
    }

    // Keep default HTTP client User-Agent (already contains "aOS").
    http_request_add_header(req, "Accept", "application/json");
    http_request_add_header(req, "Content-Type", "text/plain");
    if (http_request_set_body(req, (const uint8_t*)payload, (uint32_t)strlen(payload)) != 0) {
        serial_puts("[BUG] Failed to attach report payload body\n");
        http_response_free(resp);
        http_request_free(req);
        return -1;
    }

    send_rc = http_send(req, resp);
    snprintf(line, sizeof(line),
             "[BUG] Report send returned rc=%d status=%d body_len=%u\n",
             send_rc, resp->status_code, (unsigned)resp->body_len);
    serial_puts(line);

    if (send_rc == 0 && resp->status_code >= 200 && resp->status_code < 300) {
        result = 0;
        if (resp->body && resp->body_len > 0) {
            // Accept any 2xx unless server explicitly returns ok:false.
            // This avoids false negatives when formatting differs from exact string matches.
            const char* body = (const char*)resp->body;
            const char* ok_key = strstr(body, "\"ok\"");
            if (ok_key) {
                const char* p = ok_key + 4;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p == ':') {
                    p++;
                    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                    if (strncmp(p, "false", 5) == 0 || *p == '0') {
                        result = -1;
                        serial_puts("[BUG] Server response contains ok=false\n");
                    }
                }
            }
        }
    } else {
        serial_puts("[BUG] Report send failed (non-2xx or transport error)\n");
    }

    http_response_free(resp);
    http_request_free(req);
    return result;
}

void bug_report_set_boot_stage(bug_boot_stage_t stage) {
    g_current_stage = stage;
    if (g_initialized) {
        (void)save_current_state();
    }
}

void bug_report_init(void) {
    if (g_initialized) return;
    if (ensure_bug_report_dirs() != 0) {
        serial_puts("[BUG] Failed to initialize bug-report directory\n");
        return;
    }

    load_previous_state();
    {
        char line[192];
        snprintf(line, sizeof(line),
                 "[BUG] Init complete prev_stage=%s prev_clean=%d prev_panic=%d\n",
                 stage_to_string(g_prev_stage), g_prev_clean, g_prev_panic);
        serial_puts(line);
    }

    if (g_current_stage == BUG_BOOT_STAGE_UNKNOWN) {
        g_current_stage = BUG_BOOT_STAGE_EARLY;
    }
    g_current_clean = 0;
    g_current_panic = 0;
    g_initialized = 1;

    (void)save_current_state();
}

void bug_report_boot_success(void) {
    g_current_stage = BUG_BOOT_STAGE_STABLE;
    g_current_clean = 1;
    g_current_panic = 0;
    if (g_initialized) {
        (void)save_current_state();
    }
}

int bug_report_has_previous_panic(void) {
    return (g_prev_panic && !g_prev_clean) ? 1 : 0;
}

int bug_report_recover_after_panic(void) {
    int rollback_applied = 0;
    int rollback_result = 0;
    char context[256];
    char note[320];

    if (!g_initialized) return -1;
    if (!bug_report_has_previous_panic()) return 0;

    {
        char line[160];
        snprintf(line, sizeof(line),
                 "[BUG] Recovering panic: prev_stage=%s prev_clean=%d prev_panic=%d\n",
                 stage_to_string(g_prev_stage), g_prev_clean, g_prev_panic);
        serial_puts(line);
    }

    if (g_prev_stage == BUG_BOOT_STAGE_APM_MODULES) {
        rollback_result = apm_disable_all_autoload("panic recovery rollback");
        if (rollback_result == 0) {
            rollback_applied = 1;
        }
    }

    snprintf(note, sizeof(note),
             "Recovered previous panic (stage=%s, rollback=%s)\n",
             stage_to_string(g_prev_stage), rollback_applied ? "applied" : "not-needed");
    (void)write_text_file(BUG_REPORT_RECOVERY_FILE, note);

    snprintf(context, sizeof(context),
             "prev_stage=%s, prev_clean=%d, prev_panic=%d, rollback=%d",
             stage_to_string(g_prev_stage), g_prev_clean, g_prev_panic, rollback_applied);
    (void)bug_report_submit(BUG_REPORT_LEVEL_INFO,
                            "Recovered from previous kernel panic",
                            NULL, context);

    // Current boot starts from clean panic state.
    g_prev_panic = 0;
    g_prev_clean = 1;
    if (rollback_result < 0) {
        serial_puts("[BUG] Panic recovery rollback failed\n");
        return -1;
    }
    if (rollback_applied) {
        serial_puts("[BUG] Panic recovery applied rollback\n");
    } else {
        serial_puts("[BUG] Panic recovery required no rollback\n");
    }
    return rollback_applied;
}

void bug_report_capture_panic(registers_t* regs, const char* message, const char* file, uint32_t line) {
    char panic_stack[256];
    char panic_context[320];
    char payload[1792];
    char panic_note[512];

    if (g_capture_guard) {
        serial_puts("[BUG] Panic capture skipped (capture guard active)\n");
        return;
    }
    g_capture_guard = 1;

    g_current_clean = 0;
    g_current_panic = 1;

    if (g_initialized) {
        (void)save_current_state();
    }

    if (regs) {
        snprintf(panic_stack, sizeof(panic_stack),
                 "int=%u err=%u eip=0x%x esp=0x%x ebp=0x%x",
                 (unsigned)regs->int_no, (unsigned)regs->err_code,
                 (unsigned)regs->eip,
                 (unsigned)(((regs->cs & 0x3) != 0) ? regs->useresp : regs->esp_dummy),
                 (unsigned)regs->ebp);
    } else {
        snprintf(panic_stack, sizeof(panic_stack), "software panic (no register frame)");
    }

    snprintf(panic_context, sizeof(panic_context),
             "file=%s, line=%u, stage=%s",
             file ? file : "(unknown)", (unsigned)line, stage_to_string(g_current_stage));

    if (g_initialized) {
        snprintf(panic_note, sizeof(panic_note),
                 "message=%s\nlocation=%s:%u\nstage=%s\nstack=%s\n",
                 message ? message : "(null)",
                 file ? file : "(unknown)",
                 (unsigned)line,
                 stage_to_string(g_current_stage),
                 panic_stack);
        (void)write_text_file(BUG_REPORT_LAST_PANIC_FILE, panic_note);

        if (build_report_payload(payload, sizeof(payload),
                                 BUG_REPORT_LEVEL_CRASH,
                                 message ? message : "Kernel panic",
                                 panic_stack,
                                 panic_context) == 0) {
            (void)write_text_file(BUG_REPORT_PENDING_FILE, payload);
            serial_puts("[BUG] Panic report queued to pending.json\n");
        } else {
            serial_puts("[BUG] Panic report payload build failed\n");
        }
    } else {
        serial_puts("[BUG] Panic capture before bug-report init; pending write skipped\n");
    }

    g_capture_guard = 0;
}

int bug_report_submit(const char* level, const char* message, const char* stack, const char* context) {
    char payload[1792];
    char line[192];

    if (!message || !*message) {
        serial_puts("[BUG] Submit rejected: empty message\n");
        return -1;
    }
    if (!g_initialized) {
        serial_puts("[BUG] Submit rejected: subsystem not initialized\n");
        return -1;
    }
    if (ensure_bug_report_dirs() != 0) {
        serial_puts("[BUG] Submit failed: cannot ensure report directories\n");
        return -1;
    }

    snprintf(line, sizeof(line), "[BUG] Submit level=%s message_len=%u\n",
             bug_level_or_default(level), (unsigned)strlen(message));
    serial_puts(line);

    if (build_report_payload(payload, sizeof(payload), level, message, stack, context) != 0) {
        serial_puts("[BUG] Submit failed: payload build error\n");
        return -1;
    }

    int rc = write_text_file(BUG_REPORT_PENDING_FILE, payload);
    if (rc == 0) {
        serial_puts("[BUG] Submit queued payload to pending.json\n");
    } else {
        serial_puts("[BUG] Submit failed: pending.json write error\n");
    }
    return rc;
}

int bug_report_process_pending(void) {
    char payload[1792];
    int bytes;
    char line[192];

    if (!g_initialized) {
        serial_puts("[BUG] Process pending aborted: subsystem not initialized\n");
        return -1;
    }

    bytes = read_text_file(BUG_REPORT_PENDING_FILE, payload, sizeof(payload));
    if (bytes <= 0) {
        serial_puts("[BUG] No pending report to process\n");
        return 0;
    }

    snprintf(line, sizeof(line), "[BUG] Processing pending report bytes=%d\n", bytes);
    serial_puts(line);

    if (send_payload(payload) == 0) {
        serial_puts("[BUG] Pending report delivered\n");
        (void)vfs_unlink(BUG_REPORT_PENDING_FILE);
        return 0;
    }

    serial_puts("[BUG] Pending report delivery failed; will retry later\n");
    return 1;
}
