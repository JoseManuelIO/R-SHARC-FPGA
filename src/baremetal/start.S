.section .text.init
.globl _start
.type _start, @function
_start:
  la sp, __stack_top
.option push
.option norelax
  la gp, __global_pointer$
.option pop

  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sd zero, 0(t0)
  addi t0, t0, 8
  j 1b
2:
  li t0, 0x6000
  csrs mstatus, t0

  la t0, trap_entry
  csrw mtvec, t0

  call rsharc_run_constructors
  call main
  call rsharc_exit
3:
  j 3b

.align 2
trap_entry:
  csrr a0, mcause
  csrr a1, mepc
  call rsharc_trap
4:
  j 4b

.section .tohost,"aw",@progbits
.align 6
.globl tohost
tohost:
  .dword 0
.align 6
.globl fromhost
fromhost:
  .dword 0
