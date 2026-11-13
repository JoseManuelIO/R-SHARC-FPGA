#include <cmath>
#include <cstdio>
#include <cstdint>
#include "ACC_Controller.h"
#include "acc_channel.hpp"
#include "acc_config.hpp"
#include "debug_levels.hpp"

DebugLevels global_debug_levels;

#ifdef RSHARC_ACC_CVA6_CHANNEL
extern "C" { alignas(64) volatile ACCMailbox rsharc_acc_mailbox{}; }
extern "C" std::size_t rsharc_heap_high_water();
extern "C" std::size_t rsharc_heap_capacity();

// CVA6-specific CSR: bypass D-cache only during host exchange. The stock
// runner's RAM is cacheable; fence alone cannot make host writes coherent.
// Restore the previous setting before entering the MPC. No RTL/PMA changes.
static std::uint64_t exchange_begin() {
  std::uint64_t previous;
  asm volatile("fence rw,rw\ncsrrw %0, 0x7c1, zero\nfence rw,rw"
               : "=r"(previous) :: "memory");
  return previous;
}
static void exchange_end(std::uint64_t previous) {
  asm volatile("fence rw,rw\ncsrw 0x7c1, %0\nfence rw,rw"
               :: "r"(previous) : "memory");
}
static bool receive(ACCStepInput& input) {
  const auto previous = exchange_begin();
  rsharc_acc_mailbox.state = 1;
  asm volatile("fence rw,rw" ::: "memory");
  while (rsharc_acc_mailbox.state != 2) {}
  asm volatile("fence rw,rw" ::: "memory");
  auto* dst = reinterpret_cast<unsigned char*>(&input);
  const auto* src = reinterpret_cast<const volatile unsigned char*>(&rsharc_acc_mailbox.input);
  for (std::size_t i = 0; i < sizeof(input); ++i) dst[i] = src[i];
  exchange_end(previous);
  return true;
}
static bool send(const ACCCaptureRecord& output) {
  const auto previous = exchange_begin();
  const auto* src = reinterpret_cast<const unsigned char*>(&output);
  auto* dst = reinterpret_cast<volatile unsigned char*>(&rsharc_acc_mailbox.output);
  for (std::size_t i = 0; i < sizeof(output); ++i) dst[i] = src[i];
  asm volatile("fence rw,rw" ::: "memory");
  rsharc_acc_mailbox.state = 3;
  asm volatile("fence rw,rw" ::: "memory");
  while (rsharc_acc_mailbox.state != 0) {}
  exchange_end(previous);
  return true;
}
#elif defined(__riscv)
extern "C" volatile std::uint64_t tohost, fromhost;
extern "C" std::size_t rsharc_heap_high_water();
extern "C" std::size_t rsharc_heap_capacity();
// Spike HTIF syscall device. read/write are handled by fesvr, without an OS.
static bool transfer(std::uint64_t operation, void* data, std::size_t size) {
  auto* cursor = static_cast<unsigned char*>(data);
  while (size) {
    alignas(64) volatile std::uint64_t request[8] = {
      operation, operation == 63 ? 0ULL : 1ULL,
      reinterpret_cast<std::uintptr_t>(cursor), size, 0, 0, 0, 0};
    while (tohost) {}
    asm volatile("fence rw,rw" ::: "memory");
    tohost = reinterpret_cast<std::uintptr_t>(request);
    while (!fromhost) {}
    asm volatile("fence rw,rw" ::: "memory");
    fromhost = 0;
    const auto count = static_cast<std::int64_t>(request[0]);
    if (count <= 0 || static_cast<std::uint64_t>(count) > size) return false;
    cursor += count;
    size -= count;
  }
  return true;
}
static bool receive(ACCStepInput& input) { return transfer(63, &input, sizeof(input)); }
static bool send(ACCCaptureRecord& output) { return transfer(64, &output, sizeof(output)); }
#else
static bool receive(ACCStepInput& input) {
  return std::fread(&input, sizeof(input), 1, stdin) == 1;
}
static bool send(const ACCCaptureRecord& output) {
  return std::fwrite(&output, sizeof(output), 1, stdout) == 1 && std::fflush(stdout) == 0;
}
#endif

static std::uint64_t cycle() {
  std::uint64_t value = 0;
#ifdef __riscv
  asm volatile("rdcycle %0" : "=r"(value) :: "memory");
#endif
  return value;
}
static std::uint64_t instret() {
  std::uint64_t value = 0;
#ifdef __riscv
  asm volatile("rdinstret %0" : "=r"(value) :: "memory");
#endif
  return value;
}

int main() {
  const auto config = nlohmann::json::parse(rsharc_acc::config_json);
  global_debug_levels.from_json(config.at("==== Debgugging Levels ===="));
  ACC_Controller controller(config);  // One solver instance for the entire run.
  for (int k = 0; k < RSHARC_ACC_CLOSED_LOOP_MAX_STEPS; ++k) {
    ACCStepInput input{};
    if (!receive(input) || input.k != k || !std::isfinite(input.t)) return 88;
    for (double v : input.x) if (!std::isfinite(v)) return 88;
    for (double v : input.w) if (!std::isfinite(v)) return 88;
    for (double v : input.u_previous) if (!std::isfinite(v)) return 88;
    const auto c0 = cycle(), i0 = instret();
    const auto result = controller.calculateStep(input);
    const auto i1 = instret(), c1 = cycle();
    ACCCaptureRecord output{};
    output.k = k;
    output.controller_status = result.controller_status;
    output.solver_status = result.solver_status;
    output.iterations = result.iterations;
    output.is_feasible = result.is_feasible;
    output.u[0] = result.u[0]; output.u[1] = result.u[1];
    output.objective = result.objective;
    output.primal_residual = result.constraint_error;
    output.dual_residual = result.dual_residual;
    output.adaptive_rho_interval = result.adaptive_rho_interval;
    output.rho_updates = result.rho_updates;
    output.rho_estimate = result.rho_estimate;
    output.rho_final = result.rho_final;
    output.cycles = c1 - c0; output.instret = i1 - i0;
#ifdef __riscv
    output.reserved[0] = rsharc_heap_high_water();
    output.reserved[1] = rsharc_heap_capacity();
#endif
    if (!send(output)) return 89;
  }
  return 0;
}
