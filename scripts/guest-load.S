.code32
.section .text
.global load_trampoline

/*
 * Installed temporarily by --bench-guest-load.  XINU's null process enters
 * here from its idle loop, which is the only process context in which it is
 * safe to call create()/resume() without guessing where GDB interrupted it.
 */
load_trampoline:
    pushfl
    pushal

    /* Restore the idle loop before resume() can reschedule us. */
    movl $IDLE_ORIGINAL, %eax
    movl %eax, IDLE_ADDR
    movb $IDLE_ORIGINAL_4, IDLE_ADDR + 4

    pushl $0
    pushl $load_name
    pushl $1
    pushl $4096
    pushl $load_worker
    call CREATE_ADDR
    addl $20, %esp

    pushl $0
    pushl %eax
    call RESUME_ADDR
    addl $8, %esp

    popal
    popfl
    jmp IDLE_ADDR

/* A real always-ready XINU process. IRQs remain enabled and higher-priority
 * game tasks preempt it normally. The data dependency prevents optimization
 * into an empty branch while avoiding guest devices and host I/O. */
load_worker:
    movl $0x13579bdf, %eax
1:
    movl $10000, %ecx
2:
    imull $1664525, %eax, %eax
    addl $1013904223, %eax
    roll $7, %eax
    decl %ecx
    jnz 2b
    pushl %eax
    call RESCHED_ADDR
    popl %eax
    jmp 1b

load_name:
    .asciz "benchload"
