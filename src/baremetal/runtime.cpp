#include <cstddef>
#include <cstdint>

extern "C" {
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
#ifdef RSHARC_ENABLE_UNWIND
extern char __EH_FRAME_BEGIN__;
void __register_frame(void*);
#endif
extern volatile std::uint64_t tohost;
}

extern "C" void rsharc_run_constructors() {
#ifdef RSHARC_ENABLE_UNWIND
  __register_frame(&__EH_FRAME_BEGIN__);
#endif
  for (auto constructor = __init_array_start;
       constructor != __init_array_end;
       ++constructor) {
    (*constructor)();
  }
}

extern "C" [[noreturn]] void rsharc_exit(int code) {
  tohost = (static_cast<std::uint64_t>(static_cast<unsigned int>(code)) << 1U) | 1U;
  asm volatile("nop");
  for (;;) {
  }
}

extern "C" [[noreturn]] void rsharc_trap(std::uintptr_t, std::uintptr_t) {
  rsharc_exit(0x7f);
}

extern "C" [[noreturn]] void abort() { rsharc_exit(0x70); }
