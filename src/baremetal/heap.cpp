#include <cstddef>
#include <cstdint>

extern "C" {
extern char __heap_start;
extern char __heap_end;
extern volatile std::uint64_t tohost;
[[noreturn]] void rsharc_exit(int code);
}

namespace {
char* heap_break = &__heap_start;
char* heap_high_water = &__heap_start;
}

extern "C" void* _sbrk(std::ptrdiff_t increment) {
  char* const previous = heap_break;
  if (increment >= 0) {
    if (increment > (&__heap_end - heap_break)) {
      return reinterpret_cast<void*>(-1);
    }
  } else if (-increment > (heap_break - &__heap_start)) {
    return reinterpret_cast<void*>(-1);
  }
  heap_break += increment;
  if (heap_break > heap_high_water) {
    heap_high_water = heap_break;
  }
  return previous;
}

extern "C" std::size_t rsharc_heap_high_water() {
  return static_cast<std::size_t>(heap_high_water - &__heap_start);
}

extern "C" std::size_t rsharc_heap_capacity() {
  return static_cast<std::size_t>(&__heap_end - &__heap_start);
}

extern "C" int rsharc_console_write(const char* text, int length) {
#ifdef RSHARC_BAREMETAL_DISABLE_CONSOLE
  // CVA6 veri-testharness observes tohost only for program termination; it
  // does not acknowledge the per-character HTIF protocol used by Spike.
  (void)text;
  return length;
#else
  constexpr std::uint64_t console_write = (std::uint64_t{1} << 56U) |
                                          (std::uint64_t{1} << 48U);
  for (int index = 0; index < length; ++index) {
    while (tohost != 0) {
    }
    tohost = console_write | static_cast<unsigned char>(text[index]);
    while (tohost != 0) {
    }
  }
  return length;
#endif
}

extern "C" int _close(int) { return -1; }
extern "C" int _fstat(int, void*) { return 0; }
extern "C" int _isatty(int) { return 1; }
extern "C" long _lseek(int, long, int) { return 0; }
extern "C" int _read(int, char*, int) { return 0; }
extern "C" int _write(int, const char* text, int length) {
  return rsharc_console_write(text, length);
}
extern "C" int _kill(int, int) { return -1; }
extern "C" int _getpid() { return 1; }
extern "C" int _open(const char*, int, int) { return -1; }

struct RsharcTimeval {
  long tv_sec;
  long tv_usec;
};

extern "C" int _gettimeofday(RsharcTimeval* time, void*) {
  if (time != nullptr) {
    time->tv_sec = 0;
    time->tv_usec = 0;
  }
  return 0;
}

extern "C" [[noreturn]] void _exit(int code) { rsharc_exit(code); }

extern "C" void* __dso_handle = nullptr;
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
extern "C" [[noreturn]] void __cxa_pure_virtual();
extern "C" [[noreturn]] void abort();

extern "C" [[noreturn]] void __cxa_pure_virtual() { abort(); }
