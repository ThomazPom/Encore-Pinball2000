.code32
.global _start
_start:
    /* Reproduce the six overwritten clkint prologue bytes. */
    cli
    pushal
    movb $0x20, %al
    outb %al, $0x20

    /* Count every IRQ. Timestamp one consecutive pair out of every sixteen
       so each stored value remains a real single-IRQ interval while keeping
       the probe's cost negligible. */
    movl $0x00fe0000, %edi
    incl 0(%edi)
    movl 0(%edi), %ecx
    andl $0x0f, %ecx
    cmpl $0, %ecx
    je 2f
    cmpl $1, %ecx
    jne 1f
    rdtsc
    movl 4(%edi), %edx
    subl %edx, %eax
    movl 8(%edi), %ecx
    movl %ecx, %ebx
    andl $0x1fff, %ebx
    movl %eax, 16(%edi,%ebx,4)
    incl %ecx
    movl %ecx, 8(%edi)
    jmp 1f
2:
    rdtsc
    movl %eax, 4(%edi)
1:
    /* Restore the register state that existed after the original prologue. */
    movl 16(%esp), %ebx
    movl 20(%esp), %edx
    movl 24(%esp), %ecx
    movl 28(%esp), %eax
    movb $0x20, %al
    movl 0(%esp), %edi

    /* Patched by bench-qemu.py to clkint+6. */
    .byte 0xe9, 0, 0, 0, 0
