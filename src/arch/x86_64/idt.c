/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/idt.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch/x86_64/idt.h>
#include <serial.h>

#define IDT_ENTRIES 256
#define KERNEL_CS   0x08

static idt_entry_t idt_entries[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

static void idt_set_gate(uint8_t num, uint64_t base, uint8_t flags) {
    idt_entries[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt_entries[num].selector = KERNEL_CS;
    idt_entries[num].ist = 0;
    idt_entries[num].flags = flags;
    idt_entries[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt_entries[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt_entries[num].zero = 0;
}

void init_idt(void) {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base = (uint64_t)&idt_entries;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_entries[i].offset_low = 0;
        idt_entries[i].selector = 0;
        idt_entries[i].ist = 0;
        idt_entries[i].flags = 0;
        idt_entries[i].offset_mid = 0;
        idt_entries[i].offset_high = 0;
        idt_entries[i].zero = 0;
    }

    void (*vectors[])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    };

    for (uint8_t i = 0; i < 48; i++) {
        idt_set_gate(i, (uint64_t)vectors[i], 0x8E);
    }

    idt_set_gate(128, (uint64_t)isr128, 0xEE);

    idt_load((uint64_t)&idt_ptr);
    serial_puts("IDT Initialized and Loaded (x86_64).\n");
}
