/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_process.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
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

static void cmd_procs(const char* args) {
    (void)args;
    
    kprint("Active Tasks:");
    kprint("TID   STATE     PRIORITY  NAME");
    kprint("----  --------  --------  ----------------");
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = process_get_by_pid(i);
        if (proc && proc->state != PROCESS_DEAD) {
            char line[80];
            char pid_str[8], pri_str[8];
            itoa(proc->pid, pid_str, 10);
            itoa(proc->priority, pri_str, 10);
            
            const char* state_str;
            switch (proc->state) {
                case PROCESS_READY:    state_str = "READY   "; break;
                case PROCESS_RUNNING:  state_str = "RUNNING "; break;
                case PROCESS_BLOCKED:  state_str = "BLOCKED "; break;
                case PROCESS_SLEEPING: state_str = "SLEEPING"; break;
                case PROCESS_ZOMBIE:   state_str = "ZOMBIE  "; break;
                default:               state_str = "UNKNOWN "; break;
            }
            
            strcpy(line, pid_str);
            strcat(line, "     ");
            strcat(line, state_str);
            strcat(line, "  ");
            strcat(line, pri_str);
            strcat(line, "         ");
            strcat(line, proc->name);
            
            kprint(line);
        }
    }
}

static void cmd_terminate(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: terminate <task_id>");
        return;
    }
    
    int pid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        pid = pid * 10 + (args[i] - '0');
        i++;
    }
    
    if (pid <= 0) {
        kprint("Error: Invalid task ID");
        return;
    }
    
    process_t* proc = process_get_by_pid(pid);
    if (!proc || proc->state == PROCESS_DEAD) {
        kprint("Error: Task not found");
        return;
    }
    
    if (process_kill(pid, MSG_TERMINATE) == 0) {
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
        kprint("Usage: await <task_id>");
        kprint("Wait for a child task to complete");
        return;
    }
    
    int pid = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        pid = pid * 10 + (args[i] - '0');
        i++;
    }
    
    if (pid <= 0) {
        kprint("Error: Invalid task ID");
        return;
    }
    
    process_t* target = process_get_by_pid(pid);
    if (!target || target->state == PROCESS_DEAD) {
        kprint("Error: Task not found");
        return;
    }
    
    kprint("Waiting for task to complete...");
    
    int status;
    int result = process_waitpid(pid, &status, 0);
    
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
    command_register_with_category("terminate", "<task_id>", "Terminate task by ID", "Process", cmd_terminate);
    command_register_with_category("pause", "<milliseconds>", "Pause execution", "Process", cmd_pause);
    command_register_with_category("await", "<task_id>", "Wait for task completion", "Process", cmd_await);
    command_register_with_category("show", "<filename>", "Display file contents", "Process", cmd_show);
    command_register_with_category("chanmake", "", "Create communication channel", "Process", cmd_chanmake);
    command_register_with_category("chaninfo", "", "Display channel information", "Process", cmd_chaninfo);
}
