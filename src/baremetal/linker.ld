OUTPUT_ARCH(riscv)
ENTRY(_start)

RSHARC_RAM_ORIGIN = DEFINED(RSHARC_RAM_ORIGIN) ? RSHARC_RAM_ORIGIN : 0x80000000;
RSHARC_RAM_LENGTH = DEFINED(RSHARC_RAM_LENGTH) ? RSHARC_RAM_LENGTH : 0x10000000;
RSHARC_STACK_SIZE = DEFINED(RSHARC_STACK_SIZE) ? RSHARC_STACK_SIZE : 0x00020000;

MEMORY {
  RAM (rwx) : ORIGIN = RSHARC_RAM_ORIGIN, LENGTH = RSHARC_RAM_LENGTH
}

SECTIONS {
  . = ORIGIN(RAM);

  .text.init : {
    KEEP(*(.text.init))
  } > RAM

  .tohost ALIGN(0x1000) : {
    KEEP(*(.tohost))
  } > RAM

  .text ALIGN(0x1000) : {
    *(.text .text.*)
  } > RAM

  .rodata ALIGN(16) : {
    *(.rodata .rodata.*)
    . = ALIGN(8);
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
  } > RAM

  .eh_frame_hdr ALIGN(8) : {
    *(.eh_frame_hdr)
  } > RAM

  .eh_frame ALIGN(8) : {
    __EH_FRAME_BEGIN__ = .;
    KEEP(*(.eh_frame))
    LONG(0)
  } > RAM

  .gcc_except_table ALIGN(8) : {
    KEEP(*(.gcc_except_table .gcc_except_table.*))
  } > RAM

  .data ALIGN(16) : {
    __global_pointer$ = . + 0x800;
    *(.data .data.*)
    *(.sdata .sdata.*)
  } > RAM

  .bss ALIGN(16) (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    *(.sbss .sbss.*)
    __bss_end = .;
  } > RAM

  . = ALIGN(16);
  __heap_start = .;
  __heap_end = ORIGIN(RAM) + LENGTH(RAM) - RSHARC_STACK_SIZE;
  __stack_bottom = __heap_end;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);

  ASSERT(__heap_start <= __heap_end, "image exceeds RAM budget")
}
