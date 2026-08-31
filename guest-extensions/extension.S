/* Freestanding i386 payload injected into volatile guest RAM. */

.equ GE_BASE,             0x00ff0000
.equ GE_MAGIC,            GE_BASE + 0x00
.equ GE_VERSION,          GE_BASE + 0x04
.equ GE_SHELL_CMD_ADD,    GE_BASE + 0x08
.equ GE_PUT_VALUE,        GE_BASE + 0x0c
.equ GE_NETSTART,         GE_BASE + 0x10
.equ GE_IP_RESOURCE,      GE_BASE + 0x14
.equ GE_MASK_RESOURCE,    GE_BASE + 0x18
.equ GE_GW_RESOURCE,      GE_BASE + 0x1c
.equ GE_STARTUP_ENABLE,   GE_BASE + 0x20
.equ GE_STARTUP_IP,       GE_BASE + 0x24
.equ GE_STARTUP_MASK,     GE_BASE + 0x28
.equ GE_STARTUP_GW,       GE_BASE + 0x2c
.equ GE_ORIGINAL_LENGTH,  GE_BASE + 0x30
.equ GE_ORIGINAL_BYTES,   GE_BASE + 0x34
.equ GE_INSTALLED,        GE_BASE + 0x44

.section .control,"aw",@progbits
.long 0x58454750                 /* "PGEX", filled/validated by host */
.long 1
.fill 17, 4, 0

.section .text,"ax",@progbits
.global p2k_ge_bootstrap
.type p2k_ge_bootstrap,@function
p2k_ge_bootstrap:
    pushfl
    pushal

    cmpl $0, GE_INSTALLED
    jne 1f
    pushl $0                       /* disabled */
    pushl $p2k_ge_setip
    pushl $p2k_ge_setip_name
    pushl $0                       /* visible */
    call *GE_SHELL_CMD_ADD
    addl $16, %esp
    movl $1, GE_INSTALLED
1:
    cmpl $0, GE_STARTUP_ENABLE
    je 2f
    pushl GE_STARTUP_IP
    pushl GE_IP_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
    pushl GE_STARTUP_MASK
    pushl GE_MASK_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
    pushl GE_STARTUP_GW
    pushl GE_GW_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
2:
    /* Restore the intercepted prologue before entering real netstart. */
    cld
    movl GE_NETSTART, %edi
    movl $GE_ORIGINAL_BYTES, %esi
    movl GE_ORIGINAL_LENGTH, %ecx
    rep movsb
    popal
    popfl
    call *GE_NETSTART
    ret

/* unsigned parse_ipv4(const char *s); CF=1 on malformed input. */
.type p2k_ge_parse_ipv4,@function
p2k_ge_parse_ipv4:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp), %esi
    xorl %edi, %edi
    movl $4, %ecx
3:
    xorl %eax, %eax
    xorl %ebx, %ebx
4:
    movzbl (%esi), %edx
    cmpb $'0', %dl
    jb 5f
    cmpb $'9', %dl
    ja 5f
    imull $10, %eax, %eax
    subl $'0', %edx
    addl %edx, %eax
    cmpl $255, %eax
    ja 9f
    incl %esi
    incl %ebx
    jmp 4b
5:
    testl %ebx, %ebx
    jz 9f
    shll $8, %edi
    orl %eax, %edi
    decl %ecx
    jz 7f
    cmpb $'.', (%esi)
    jne 9f
    incl %esi
    jmp 3b
7:
    cmpb $0, (%esi)
    jne 9f
    movl %edi, %eax
    clc
    jmp 10f
9:
    stc
10:
    popl %edi
    popl %esi
    popl %ebx
    leave
    ret

/* XINU shell ABI: (stdin, stdout, stderr, argc, argv). */
.global p2k_ge_setip
.type p2k_ge_setip,@function
p2k_ge_setip:
    pushl %ebp
    movl %esp, %ebp
    subl $12, %esp
    cmpl $4, 20(%ebp)
    jne 19f
    movl 24(%ebp), %edx
    pushl 4(%edx)
    call p2k_ge_parse_ipv4
    addl $4, %esp
    jc 19f
    movl %eax, -4(%ebp)
    movl 24(%ebp), %edx
    pushl 8(%edx)
    call p2k_ge_parse_ipv4
    addl $4, %esp
    jc 19f
    movl %eax, -8(%ebp)
    movl 24(%ebp), %edx
    pushl 12(%edx)
    call p2k_ge_parse_ipv4
    addl $4, %esp
    jc 19f
    movl %eax, -12(%ebp)

    pushl -4(%ebp)
    pushl GE_IP_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
    pushl -8(%ebp)
    pushl GE_MASK_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
    pushl -12(%ebp)
    pushl GE_GW_RESOURCE
    call *GE_PUT_VALUE
    addl $8, %esp
    xorl %eax, %eax
    leave
    ret
19:
    movl $1, %eax
    leave
    ret

p2k_ge_setip_name:
    .asciz "setip"
