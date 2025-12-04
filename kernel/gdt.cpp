#include "gdt.h"

__attribute__((aligned(0x1000)))
static struct gdt_entry gdt[5]; // Null, Kernel Code, Kernel Data, TSS (Low), TSS (High)
static struct gdt_descriptor gdtr;

__attribute__((aligned(16)))
static struct tss_entry tss;

// Stack for the TSS (Privilege level 0 stack)
__attribute__((aligned(16)))
static uint8_t tss_stack[4096];

extern "C" void load_gdt(struct gdt_descriptor* gdtr);
extern "C" void load_tss(void);

void gdt_init() {
    // Zero the TSS first
    for (unsigned i = 0; i < sizeof(tss); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }
    
    // Setup TSS
    // We must set RSP0 to the top of our stack
    tss.rsp0 = (uint64_t)&tss_stack[sizeof(tss_stack)];
    tss.iomap_base = sizeof(tss); // No IOPB

    // Null descriptor
    gdt[0] = {0, 0, 0, 0, 0, 0};

    // Kernel Code (64-bit)
    gdt[1] = {
        .limit_low = 0xFFFF,
        .base_low = 0,
        .base_middle = 0,
        .access = 0x9A,      // Present, Ring0, Code, Readable
        .granularity = 0xAF, // 4K pages, 64-bit
        .base_high = 0
    };

    // Kernel Data (64-bit)
    gdt[2] = {
        .limit_low = 0xFFFF,
        .base_low = 0,
        .base_middle = 0,
        .access = 0x92,      // Present, Ring0, Data, Writable
        .granularity = 0xCF, // 4K pages, 32-bit limit
        .base_high = 0
    };

    // TSS Descriptor (16 bytes - takes 2 entries)
    uint64_t tss_base = (uint64_t)&tss;
    uint64_t tss_limit = sizeof(tss) - 1;

    // TSS Low Part
    gdt[3] = {
        .limit_low = (uint16_t)(tss_limit & 0xFFFF),
        .base_low = (uint16_t)(tss_base & 0xFFFF),
        .base_middle = (uint8_t)((tss_base >> 16) & 0xFF),
        .access = 0x89, // Present, Ring0, Available TSS
        .granularity = (uint8_t)(((tss_limit >> 16) & 0x0F)),
        .base_high = (uint8_t)((tss_base >> 24) & 0xFF)
    };

    // TSS High Part (upper 32 bits of base, plus reserved)
    // This is a raw 8 bytes: upper 32 of base, then 32 bits reserved (0)
    uint64_t* tss_high = (uint64_t*)&gdt[4];
    *tss_high = (tss_base >> 32) & 0xFFFFFFFF;

    gdtr.size = sizeof(gdt) - 1;
    gdtr.offset = (uint64_t)&gdt;

    load_gdt(&gdtr);
    load_tss();
}
