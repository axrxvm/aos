/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_process.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <process.h>
#include <ipc.h>

extern void kprint(const char *str);

typedef struct {
    uint32_t total;
    uint32_t schedulable;
} procs_stats_t;

static const char* proc_state_to_string(process_state_t state) {
    switch (state) {
        case PROCESS_READY: return "READY";
        case PROCESS_RUNNING: return "RUNNING";
        case PROCESS_BLOCKED: return "BLOCKED";
        case PROCESS_SLEEPING: return "SLEEP";
        case PROCESS_ZOMBIE: return "ZOMBIE";
        case PROCESS_DEAD: return "DEAD";
        default: return "UNKNOWN";
    }
}

static void append_padded(char* dst, uint32_t cap, const char* text, uint32_t width) {
    uint32_t len = strlen(dst);
    if (len >= cap - 1) {
        return;
    }

    const char* src = text ? text : "";
    uint32_t i = 0;
    while (src[i] && (len + i) < cap - 1) {
        dst[len + i] = src[i];
        i++;
    }
    len += i;

    while (i < width && len < cap - 1) {
        dst[len++] = ' ';
        i++;
    }
    dst[len] = '\0';
}

static int cmd_procs_cb(process_t* proc, void* ctx) {
    procs_stats_t* stats = (procs_stats_t*)ctx;
    if (stats) {
        stats->total++;
        if (proc->schedulable) {
            stats->schedulable++;
        }
    }

    char line[160];
    char tid_str[12];
    char pri_str[12];
    char ring_str[12];

    itoa(proc->pid, tid_str, 10);
    itoa(proc->priority, pri_str, 10);
    if (proc->privilege_level == 0) {
        strcpy(ring_str, "k0");
    } else {
        strcpy(ring_str, "u3");
    }

    line[0] = '\0';
    append_padded(line, sizeof(line), tid_str, 6);
    append_padded(line, sizeof(line), process_task_type_name(proc->task_type), 11);
    append_padded(line, sizeof(line), proc_state_to_string(proc->state), 11);
    append_padded(line, sizeof(line), pri_str, 5);
    append_padded(line, sizeof(line), ring_str, 6);
    append_padded(line, sizeof(line), proc->schedulable ? "yes" : "no", 7);
    append_padded(line, sizeof(line), proc->name, 0);

    kprint(line);
    return 0;
}

static void cmd_procs(const char* args) {
    (void)args;
    procs_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    kprint("Active Tasks:");
    kprint("TID   TYPE       STATE      PRI  RING  SCHED  NAME");
    kprint("----  ---------  ---------  ---  ----  -----  ----------------");
    process_for_each(cmd_procs_cb, &stats);

    char summary[128];
    char total_str[16];
    char sched_str[16];
    itoa(stats.total, total_str, 10);
    itoa(stats.schedulable, sched_str, 10);
    strcpy(summary, "Total tasks: ");
    strcat(summary, total_str);
    strcat(summary, " (schedulable: ");
    strcat(summary, sched_str);
    strcat(summary, ")");
    kprint(summary);
}

static void cmd_terminate(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: terminate <tid>");
        return;
    }
    
    int tid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        tid = tid * 10 + (args[i] - '0');
        i++;
    }
    
    if (tid <= 0) {
        kprint("Error: Invalid TID");
        return;
    }
    
    process_t* proc = process_get_by_pid(tid);
    if (!proc || proc->state == PROCESS_DEAD) {
        kprint("Error: Task not found");
        return;
    }
    
    if (process_kill(tid, MSG_TERMINATE) == 0) {
        kprint("Task terminated successfully");
    } else {
        kprint("Error: Failed to terminate task");
    }
}

static void cmd_pause(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: pause <milliseconds>");
        return;
    }
    
    int ms = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        ms = ms * 10 + (args[i] - '0');
        i++;
    }
    
    if (ms <= 0) {
        kprint("Error: Invalid duration");
        return;
    }
    
    kprint("Pausing...");
    process_sleep(ms);
    kprint("Resumed");
}

static void cmd_show(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: show <filename>");
        return;
    }
    
    while (*args == ' ') args++;
    
    int fd = sys_open(args, O_RDONLY);
    if (fd < 0) {
        kprint("Error: Cannot open file");
        return;
    }
    
    char buffer[256];
    int bytes_read;
    
    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        vga_puts(buffer);
    }
    
    sys_close(fd);
    vga_puts("\n");
}

static void cmd_chanmake(const char* args) {
    (void)args;
    
    int channel_id = channel_create();
    if (channel_id < 0) {
        kprint("Error: Failed to create channel");
        return;
    }
    
    char line[80];
    char id_str[16];
    strcpy(line, "Channel created: ID ");
    itoa(channel_id, id_str, 10);
    strcat(line, id_str);
    kprint(line);
}

static void cmd_chaninfo(const char* args) {
    (void)args;
    kprint("Communication Channels:");
    kprint("Use 'chanmake' to create a new channel");
    kprint("Channels enable inter-task communication");
}

static void cmd_await(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: await <tid>");
        kprint("Wait for a child task to complete");
        return;
    }
    
    int tid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        tid = tid * 10 + (args[i] - '0');
        i++;
    }
    
    if (tid <= 0) {
        kprint("Error: Invalid TID");
        return;
    }
    
    process_t* target = process_get_by_pid(tid);
    if (!target || target->state == PROCESS_DEAD) {
        kprint("Error: Task not found");
        return;
    }
    
    kprint("Waiting for task to complete...");
    
    int status;
    int result = process_waitpid(tid, &status, 0);
    
    if (result < 0) {
        kprint("Error: Failed to wait for task (may not be a child)");
        return;
    }
    
    char line[80];
    char pid_str[16], status_str[16];
    itoa(result, pid_str, 10);
    itoa(status, status_str, 10);
    
    strcpy(line, "Task ");
    strcat(line, pid_str);
    strcat(line, " completed with status: ");
    strcat(line, status_str);
    kprint(line);
}

void cmd_module_process_register(void) {
    command_register_with_category("procs", "", "List active tasks", "Process", cmd_procs);
    command_register_with_category("terminate", "<tid>", "Terminate task by ID", "Process", cmd_terminate);
    command_register_with_category("pause", "<milliseconds>", "Pause execution", "Process", cmd_pause);
    command_register_with_category("await", "<tid>", "Wait for task completion", "Process", cmd_await);
    command_register_with_category("show", "<filename>", "Display file contents", "Process", cmd_show);
    command_register_with_category("chanmake", "", "Create communication channel", "Process", cmd_chanmake);
    command_register_with_category("chaninfo", "", "Display channel information", "Process", cmd_chaninfo);
}
