OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(p2k_ge_bootstrap)

PHDRS
{
  control PT_LOAD FLAGS(6);
  code PT_LOAD FLAGS(5);
}

SECTIONS
{
  . = 0x00ff0000;
  .control : { KEEP(*(.control)) } :control
  . = 0x00ff0100;
  .text : { *(.text*) *(.rodata*) } :code
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
