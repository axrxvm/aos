/*
 * === AOS HEADER BEGIN ===
 * src/kernel/ipc.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <ipc.h>
#include <process.h>
#include <string.h>
#include <serial.h>
#include <pmm.h>
#include <vmm.h>

/*
 * Inter-process communication subsystem.
 *
 * Provides two IPC models:
 * - lightweight message signaling (`msg_*`) for control events
 * - byte-stream-like in-kernel channels (`channel_*`) for payload exchange
 *
 * Note: message queues/handlers are partially stubbed and intentionally marked
 * TODO until full per-process queueing lands.
 */

// Global IPC structures
static channel_t* channel_list = NULL;
static shared_region_t* region_list = NULL;
static int next_channel_id = 1;

typedef struct {
    pid_t pid;
    uint8_t active;
    msg_queue_t queue;
} process_msg_queue_t;

static process_msg_queue_t process_queues[MAX_PROCESSES];

static process_msg_queue_t* get_process_queue(pid_t pid, int create_if_missing) {
    process_msg_queue_t* free_slot = NULL;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_queues[i].active && process_queues[i].pid == pid) {
            return &process_queues[i];
        }
        if (!process_queues[i].active && !free_slot) {
            free_slot = &process_queues[i];
        }
    }

    if (!create_if_missing || !free_slot) {
        return NULL;
    }

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->pid = pid;
    free_slot->active = 1;
    return free_slot;
}

static int enqueue_message(process_msg_queue_t* queue_slot, const message_t* msg) {
    if (!queue_slot || !msg) {
        return -1;
    }

    msg_queue_t* queue = &queue_slot->queue;
    if (queue->count >= MAX_MESSAGES) {
        return -1;
    }

    queue->messages[queue->tail] = *msg;
    queue->tail = (queue->tail + 1) % MAX_MESSAGES;
    queue->count++;
    return 0;
}

static int dequeue_message(process_msg_queue_t* queue_slot, message_t* msg) {
    if (!queue_slot) {
        return -1;
    }

    msg_queue_t* queue = &queue_slot->queue;
    if (queue->count == 0) {
        return -1;
    }

    if (msg) {
        *msg = queue->messages[queue->head];
    }

    queue->head = (queue->head + 1) % MAX_MESSAGES;
    queue->count--;
    return 0;
}

// Initialize IPC subsystem
void init_ipc(void) {
    /* Initialize global IPC registries during kernel bootstrap. */
    serial_puts("Initializing IPC subsystem...\n");
    channel_list = NULL;
    region_list = NULL;
    next_channel_id = 1;
    memset(process_queues, 0, sizeof(process_queues));
    serial_puts("IPC subsystem initialized.\n");
}

// ===== Message Events Implementation =====

int msg_send(pid_t target_pid, int msg_num, uint32_t data) {
    /* Queue message for target process; dispatch happens on receiver side. */
    process_t* target = process_get_by_pid(target_pid);
    if (!target) {
        return -1;  // Process not found
    }

    process_msg_queue_t* queue_slot = get_process_queue(target->pid, 1);
    if (!queue_slot) {
        return -1;
    }

    message_t message;
    message.msg_num = msg_num;
    message.sender_pid = process_getpid();
    message.data = data;

    return enqueue_message(queue_slot, &message);
}

int msg_receive(message_t* msg) {
    process_t* current = process_get_current();
    if (!current || !msg) {
        return -1;
    }

    process_msg_queue_t* queue_slot = get_process_queue(current->pid, 1);
    if (!queue_slot) {
        return -1;
    }

    return dequeue_message(queue_slot, msg);
}

int msg_set_handler(int msg_num, msg_handler_t handler) {
    process_t* current = process_get_current();
    if (!current) {
        return -1;
    }

    if (msg_num < 0 || msg_num >= (int)(sizeof(((msg_queue_t*)0)->handlers) / sizeof(msg_handler_t))) {
        return -1;
    }

    process_msg_queue_t* queue_slot = get_process_queue(current->pid, 1);
    if (!queue_slot) {
        return -1;
    }

    queue_slot->queue.handlers[msg_num] = handler;
    return 0;
}

void msg_dispatch_pending(void) {
    process_t* current = process_get_current();
    if (!current) {
        return;
    }

    process_msg_queue_t* queue_slot = get_process_queue(current->pid, 0);
    if (!queue_slot) {
        return;
    }

    message_t msg;
    while (dequeue_message(queue_slot, &msg) == 0) {
        if (msg.msg_num >= 0 &&
            msg.msg_num < (int)(sizeof(queue_slot->queue.handlers) / sizeof(msg_handler_t)) &&
            queue_slot->queue.handlers[msg.msg_num]) {
            queue_slot->queue.handlers[msg.msg_num](msg.msg_num);
            continue;
        }

        switch (msg.msg_num) {
            case MSG_TERMINATE:
                process_kill(current->pid, 15);
                break;
            case MSG_SUSPEND:
                process_mark_task_state(current->pid, PROCESS_BLOCKED);
                break;
            case MSG_RESUME:
                process_mark_task_state(current->pid, PROCESS_READY);
                break;
            case MSG_INTERRUPT:
            case MSG_ALARM:
            case MSG_CHILD_EXIT:
            case MSG_USER1:
            case MSG_USER2:
            default:
                break;
        }
    }
}

// ===== Communication Channels Implementation =====

int channel_create(void) {
    /* Create kernel-resident channel object and return channel ID handle. */
    // Allocate new channel
    channel_t* channel = (channel_t*)kmalloc(sizeof(channel_t));
    if (!channel) {
        return -1;
    }
    
    memset(channel, 0, sizeof(channel_t));
    channel->id = next_channel_id++;
    channel->creator_pid = process_getpid();
    
    // Add to list
    channel->next = channel_list;
    channel_list = channel;
    
    return channel->id;
}

static channel_t* find_channel(int channel_id) {
    channel_t* current = channel_list;
    while (current) {
        if ((int)current->id == channel_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int channel_open(int channel_id, int mode) {
    channel_t* channel = find_channel(channel_id);
    if (!channel) {
        return -1;
    }
    
    if (mode & 0x01) {  // Read mode
        channel->reader_count++;
    }
    if (mode & 0x02) {  // Write mode
        channel->writer_count++;
    }
    
    return channel_id;  // Return as file descriptor
}

int channel_close(int channel_fd) {
    channel_t* channel = find_channel(channel_fd);
    if (!channel) {
        return -1;
    }
    
    // Decrement reference counts
    if (channel->reader_count > 0) channel->reader_count--;
    if (channel->writer_count > 0) channel->writer_count--;
    
    // Close if no more references
    if (channel->reader_count == 0 && channel->writer_count == 0) {
        channel->closed = 1;
    }
    
    return 0;
}

int channel_write(int channel_fd, const void* data, uint32_t size) {
    channel_t* channel = find_channel(channel_fd);
    if (!channel || channel->closed) {
        return -1;
    }
    
    // Calculate available space
    uint32_t available = CHANNEL_BUFFER_SIZE - channel->data_size;
    if (size > available) {
        size = available;  // Truncate to fit
    }
    
    if (size == 0) {
        return 0;  // Buffer full
    }
    
    // Write data in circular manner
    const char* src = (const char*)data;
    uint32_t written = 0;
    
    while (written < size) {
        channel->buffer[channel->write_pos] = src[written];
        channel->write_pos = (channel->write_pos + 1) % CHANNEL_BUFFER_SIZE;
        written++;
    }
    
    channel->data_size += written;
    return written;
}

int channel_read(int channel_fd, void* data, uint32_t size) {
    channel_t* channel = find_channel(channel_fd);
    if (!channel) {
        return -1;
    }
    
    // Check if data available
    if (channel->data_size == 0) {
        return 0;  // No data
    }
    
    // Read available data
    if (size > channel->data_size) {
        size = channel->data_size;
    }
    
    char* dest = (char*)data;
    uint32_t read_count = 0;
    
    while (read_count < size) {
        dest[read_count] = channel->buffer[channel->read_pos];
        channel->read_pos = (channel->read_pos + 1) % CHANNEL_BUFFER_SIZE;
        read_count++;
    }
    
    channel->data_size -= read_count;
    return read_count;
}

// ===== Shared Regions Implementation =====

int region_create(const char* name, uint32_t size, uint32_t permissions) {
    if (!name) return -1;
    
    // Check if already exists
    shared_region_t* current = region_list;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return -1;  // Already exists
        }
        current = current->next;
    }
    
    // Allocate region structure
    shared_region_t* region = (shared_region_t*)kmalloc(sizeof(shared_region_t));
    if (!region) return -1;
    
    memset(region, 0, sizeof(shared_region_t));
    strncpy(region->name, name, REGION_NAME_LEN - 1);
    region->size = size;
    region->permissions = permissions;
    region->owner_pid = process_getpid();
    region->ref_count = 1;
    
    // Allocate physical pages
    uint32_t pages = (size + 4095) / 4096;
    region->phys_addr = (uintptr_t)alloc_page();
    if (!region->phys_addr) {
        kfree(region);
        return -1;
    }
    
    // Allocate additional pages if needed
    for (uint32_t i = 1; i < pages; i++) {
        alloc_page();  // Allocate consecutive pages
    }
    
    // Add to list
    region->next = region_list;
    region_list = region;
    
    return 0;
}

static shared_region_t* find_region(const char* name) {
    shared_region_t* current = region_list;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int region_open(const char* name) {
    shared_region_t* region = find_region(name);
    if (!region) return -1;
    
    region->ref_count++;
    return 0;
}

int region_close(const char* name) {
    shared_region_t* region = find_region(name);
    if (!region) return -1;
    
    if (region->ref_count > 0) {
        region->ref_count--;
    }
    
    // Free if no more references
    if (region->ref_count == 0) {
        uint32_t pages = (region->size + 4095) / 4096;
        for (uint32_t i = 0; i < pages; i++) {
            free_page((void*)(region->phys_addr + ((uintptr_t)i * 4096U)));
        }
        
        // Remove from list
        shared_region_t** prev_ptr = &region_list;
        shared_region_t* current = region_list;
        while (current) {
            if (current == region) {
                *prev_ptr = current->next;
                kfree(current);
                break;
            }
            prev_ptr = &current->next;
            current = current->next;
        }
    }
    
    return 0;
}

void* region_map(const char* name) {
    shared_region_t* region = find_region(name);
    if (!region) return NULL;
    
    process_t* current = process_get_current();
    if (!current || !current->address_space) return NULL;
    
    // Map into process address space
    uintptr_t virt_addr = 0x50000000;  // Shared region base address
    uint32_t flags = VMM_PRESENT | VMM_USER;
    if (region->permissions & 0x02) {
        flags |= VMM_WRITE;
    }
    
    vmm_map_physical(current->address_space, virt_addr, 
                     region->phys_addr, region->size, flags);
    
    region->virt_addr = virt_addr;
    return (void*)virt_addr;
}

int region_unmap(const char* name) {
    shared_region_t* region = find_region(name);
    if (!region) return -1;
    
    process_t* current = process_get_current();
    if (!current || !current->address_space) return -1;
    
    vmm_unmap(current->address_space, region->virt_addr, region->size);
    return 0;
}
