/*
 * === AOS HEADER BEGIN ===
 * include/ipc.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <process.h>

#define MSG_TERMINATE   1   // Request process termination
#define MSG_INTERRUPT   2   // Interrupt current operation
#define MSG_SUSPEND     3   // Suspend process execution
#define MSG_RESUME      4   // Resume suspended process
#define MSG_ALARM       5   // Timer alarm fired
#define MSG_CHILD_EXIT  6   // Child process exited
#define MSG_USER1       10  // User-defined message 1
#define MSG_USER2       11  // User-defined message 2

#define MAX_MESSAGES    32  // Max pending messages per process

// Message handler function type
typedef void (*msg_handler_t)(int msg_num);

// Message structure
typedef struct {
    int msg_num;
    pid_t sender_pid;
    uint32_t data;
} message_t;

// Message queue for a process
typedef struct {
    message_t messages[MAX_MESSAGES];
    int head;
    int tail;
    int count;
    msg_handler_t handlers[32];  // Custom handlers
} msg_queue_t;

// Communication Channels
#define MAX_CHANNELS    64
#define CHANNEL_BUFFER_SIZE 4096

typedef struct channel {
    uint32_t id;
    pid_t creator_pid;
    char buffer[CHANNEL_BUFFER_SIZE];
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t data_size;
    int reader_count;
    int writer_count;
    int closed;
    struct channel* next;
} channel_t;

// Shared Regions
#define MAX_REGIONS     32
#define REGION_NAME_LEN 32

typedef struct shared_region {
    char name[REGION_NAME_LEN];
    uint32_t virt_addr;
    uint32_t phys_addr;
    uint32_t size;
    pid_t owner_pid;
    uint32_t ref_count;
    uint32_t permissions;  // Read/Write flags
    struct shared_region* next;
} shared_region_t;

// Initialize IPC subsystem
void init_ipc(void);

// Message Events API
int msg_send(pid_t target_pid, int msg_num, uint32_t data);
int msg_receive(message_t* msg);
int msg_set_handler(int msg_num, msg_handler_t handler);
void msg_dispatch_pending(void);

// Communication Channels API
int channel_create(void);                        // Returns channel ID
int channel_open(int channel_id, int mode);      // Open for read/write
int channel_close(int channel_fd);
int channel_write(int channel_fd, const void* data, uint32_t size);
int channel_read(int channel_fd, void* data, uint32_t size);

// Shared Regions API
int region_create(const char* name, uint32_t size, uint32_t permissions);
int region_open(const char* name);
int region_close(const char* name);
void* region_map(const char* name);
int region_unmap(const char* name);

#endif // IPC_H
