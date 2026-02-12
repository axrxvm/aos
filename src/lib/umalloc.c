/*
 * === AOS HEADER BEGIN ===
 * src/lib/umalloc.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <umalloc.h>
#include <syscall.h>
#include <string.h>

static mem_block_t* heap_start = NULL;
static uint32_t heap_size = 0;

void umem_init(void) {
    // Request initial heap from kernel
    heap_start = (mem_block_t*)sys_sbrk(UMEM_POOL_SIZE);
    if (heap_start == (void*)-1) {
        heap_start = NULL;
        return;
    }
    
    heap_size = UMEM_POOL_SIZE;
    
    // Initialize first block
    heap_start->size = UMEM_POOL_SIZE - sizeof(mem_block_t);
    heap_start->is_free = 1;
    heap_start->next = NULL;
}

void* umalloc(size_t size) {
    if (!heap_start) {
        umem_init();
        if (!heap_start) return NULL;
    }
    
    if (size == 0) return NULL;
    
    // Align size to 8 bytes
    size = (size + 7) & ~7;
    
    // Find free block (first fit)
    mem_block_t* current = heap_start;
    mem_block_t* prev = NULL;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            // Found suitable block
            current->is_free = 0;
            
            // Split block if it's much larger
            if (current->size >= size + sizeof(mem_block_t) + 32) {
                mem_block_t* new_block = (mem_block_t*)((char*)current + sizeof(mem_block_t) + size);
                new_block->size = current->size - size - sizeof(mem_block_t);
                new_block->is_free = 1;
                new_block->next = current->next;
                
                current->size = size;
                current->next = new_block;
            }
            
            return (void*)((char*)current + sizeof(mem_block_t));
        }
        
        prev = current;
        current = current->next;
    }
    
    // No suitable block found, request more memory
    uint32_t expand_size = size + sizeof(mem_block_t);
    if (expand_size < UMEM_POOL_SIZE) {
        expand_size = UMEM_POOL_SIZE;
    }
    
    void* new_mem = sys_sbrk(expand_size);
    if (new_mem == (void*)-1) {
        return NULL;  // Out of memory
    }
    
    // Create new block
    mem_block_t* new_block = (mem_block_t*)new_mem;
    new_block->size = expand_size - sizeof(mem_block_t);
    new_block->is_free = 0;
    new_block->next = NULL;
    
    // Link to end of list
    if (prev) {
        prev->next = new_block;
    }
    
    heap_size += expand_size;
    
    // Split if needed
    if (new_block->size >= size + sizeof(mem_block_t) + 32) {
        mem_block_t* split_block = (mem_block_t*)((char*)new_block + sizeof(mem_block_t) + size);
        split_block->size = new_block->size - size - sizeof(mem_block_t);
        split_block->is_free = 1;
        split_block->next = NULL;
        
        new_block->size = size;
        new_block->next = split_block;
    }
    
    return (void*)((char*)new_block + sizeof(mem_block_t));
}

void ufree(void* ptr) {
    if (!ptr || !heap_start) return;
    
    // Get block header
    mem_block_t* block = (mem_block_t*)((char*)ptr - sizeof(mem_block_t));
    
    // Validate block
    if (block < heap_start || (char*)block >= (char*)heap_start + heap_size) {
        return;  // Invalid pointer
    }
    
    block->is_free = 1;
    
    // Coalesce with next block if free
    if (block->next && block->next->is_free) {
        block->size += sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
    }
    
    // Coalesce with previous block if free
    mem_block_t* current = heap_start;
    while (current && current->next != block) {
        current = current->next;
    }
    
    if (current && current->is_free) {
        current->size += sizeof(mem_block_t) + block->size;
        current->next = block->next;
    }
}

void* urealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return umalloc(new_size);
    }
    
    if (new_size == 0) {
        ufree(ptr);
        return NULL;
    }
    
    // Get current block
    mem_block_t* block = (mem_block_t*)((char*)ptr - sizeof(mem_block_t));
    
    if (block->size >= new_size) {
        return ptr;  // Current block is large enough
    }
    
    // Allocate new block and copy
    void* new_ptr = umalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    memcpy(new_ptr, ptr, block->size);
    ufree(ptr);
    
    return new_ptr;
}

void umem_stats(uint32_t* total, uint32_t* used, uint32_t* free_blocks) {
    if (!heap_start) {
        if (total) *total = 0;
        if (used) *used = 0;
        if (free_blocks) *free_blocks = 0;
        return;
    }
    
    uint32_t used_mem = 0;
    uint32_t free_count = 0;
    
    mem_block_t* current = heap_start;
    while (current) {
        if (!current->is_free) {
            used_mem += current->size + sizeof(mem_block_t);
        } else {
            free_count++;
        }
        current = current->next;
    }
    
    if (total) *total = heap_size;
    if (used) *used = used_mem;
    if (free_blocks) *free_blocks = free_count;
}
