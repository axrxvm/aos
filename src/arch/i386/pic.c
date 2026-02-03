/*
 * === AOS HEADER BEGIN ===
 * ./src/arch/i386/pic.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <arch/i386/pic.h>
#include <io.h>

void pic_init(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, 0x20); /* Master PIC vector offset */
    io_wait();
    outb(PIC2_DATA, 0x28); /* Slave PIC vector offset */
    io_wait();

    outb(PIC1_DATA, 4); /* Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100) */
    io_wait();
    outb(PIC2_DATA, 2); /* Tell Slave PIC its cascade identity (0000 0010) */
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, a1); /* Restore saved masks */
    outb(PIC2_DATA, a2);

    outb(PIC1_DATA, 0xFF); /* Mask all on master */
    outb(PIC2_DATA, 0xFF); /* Mask all on slave */
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) { /* If IRQ is from slave PIC */
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI); /* Always send EOI to master PIC */
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    outb(port, inb(port) | (1 << irq));
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    outb(port, inb(port) & ~(1 << irq));
}
