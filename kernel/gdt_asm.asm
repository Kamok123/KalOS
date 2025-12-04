bits 64

global load_gdt

load_gdt:
    lgdt [rdi]      ; Load GDT from the descriptor passed in RDI
    
    ; Reload CS register using far return
    push 0x08       ; Push code segment (Index 1 * 8)
    lea rax, [rel .reload_CS]
    push rax        ; Push this value to the stack
    retfq           ; Perform a far return

.reload_CS:
    ; Reload data segment registers
    mov ax, 0x10    ; 0x10 is a stand-in for your data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

global load_tss
load_tss:
    mov ax, 0x18    ; 0x18 is the offset of our TSS (Index 3 * 8)
    ltr ax
    ret
