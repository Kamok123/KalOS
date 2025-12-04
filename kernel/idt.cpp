#include "idt.h"

__attribute__((aligned(0x10)))
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_descriptor idtr;

extern "C" void* isr_stub_table[];

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags) {
    struct idt_entry* descriptor = &idt[vector];

    descriptor->isr_low    = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs  = 0x08; // Kernel code segment offset
    descriptor->ist        = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid    = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high   = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved   = 0;
}

extern "C" void load_idt(struct idt_descriptor* idtr);

void idt_init() {
    idtr.size = sizeof(idt) - 1;
    idtr.offset = (uint64_t)&idt;

    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E); // 0x8E = Present, Ring0, Interrupt Gate
    }

    load_idt(&idtr);
    
    // Do not enable interrupts yet! We haven't remapped the PIC.
    // asm("sti");
}
