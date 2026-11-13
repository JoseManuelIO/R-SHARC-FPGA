#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <nlopt.h>
#include "NLMPCController.h"
#include "cartpole_channel.hpp"
#include "cartpole_config.hpp"
#include "debug_levels.hpp"

DebugLevels global_debug_levels;

#ifdef RSHARC_CARTPOLE_CVA6_CHANNEL
extern "C" { alignas(64) volatile CartpoleMailbox rsharc_cartpole_mailbox{}; }
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
static bool receive(CartpoleStepInput& input) {
  const auto previous = exchange_begin();
  rsharc_cartpole_mailbox.state = 1;
  asm volatile("fence rw,rw" ::: "memory");
  while (rsharc_cartpole_mailbox.state != 2) {}
  asm volatile("fence rw,rw" ::: "memory");
  auto* dst = reinterpret_cast<unsigned char*>(&input);
  const auto* src = reinterpret_cast<const volatile unsigned char*>(&rsharc_cartpole_mailbox.input);
  for (std::size_t i = 0; i < sizeof(input); ++i) dst[i] = src[i];
  exchange_end(previous);
  return true;
}
static bool send(const CartpoleStepOutput& output) {
  const auto previous = exchange_begin();
  const auto* src = reinterpret_cast<const unsigned char*>(&output);
  auto* dst = reinterpret_cast<volatile unsigned char*>(&rsharc_cartpole_mailbox.output);
  for (std::size_t i = 0; i < sizeof(output); ++i) dst[i] = src[i];
  asm volatile("fence rw,rw" ::: "memory");
  rsharc_cartpole_mailbox.state = 3;
  asm volatile("fence rw,rw" ::: "memory");
  while (rsharc_cartpole_mailbox.state != 0) {}
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
static bool receive(CartpoleStepInput& input) { return transfer(63, &input, sizeof(input)); }
static bool send(CartpoleStepOutput& output) { return transfer(64, &output, sizeof(output)); }
#else
static bool receive(CartpoleStepInput& input) {
  return std::fread(&input, sizeof(input), 1, stdin) == 1;
}
static bool send(const CartpoleStepOutput& output) {
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

// As in the original service: seed each solve with the host's previous control.
class CartpoleServiceController final : public NLMPCController {
 public:
  using NLMPCController::NLMPCController;
  void seedControl(const uVec& previous) { control = previous; }
};

#ifndef RSHARC_CARTPOLE_MAX_STEPS
#define RSHARC_CARTPOLE_MAX_STEPS 64
#endif
static_assert(RSHARC_CARTPOLE_MAX_STEPS >= 1 && RSHARC_CARTPOLE_MAX_STEPS <= 64);
static_assert(TNX == 4 && TNU == 1 && TNDU == 1 && TNY == 1);

int main() {
  try {
    nlopt_srand(0);
    // Silence construction logs as well as solve logs on the binary channel.
    mpc::Logger::instance().setLevel(mpc::Logger::log_level::NONE);
    const auto config = nlohmann::json::parse(rsharc_cartpole::config_json);
    auto controller = std::make_unique<CartpoleServiceController>(config);
    for (std::uint64_t k = 0; k < RSHARC_CARTPOLE_MAX_STEPS; ++k) {
      CartpoleStepInput input{};
      if (!receive(input) || input.k != k || !std::isfinite(input.t)) return 88;
      for (double v : input.x) if (!std::isfinite(v)) return 88;
      for (double v : input.w) if (!std::isfinite(v)) return 88;
      for (double v : input.u_previous) if (!std::isfinite(v)) return 88;
      xVec state;
      wVec disturbance;
      uVec previous;
      for (int i = 0; i < TNX; ++i) state(i) = input.x[i];
      disturbance(0) = input.w[0];
      previous(0) = input.u_previous[0];
      controller->seedControl(previous);
      const auto c0 = cycle(), i0 = instret();
      controller->calculateControl(static_cast<int>(k), input.t, state, disturbance);
      const auto i1 = instret(), c1 = cycle();
      const auto control = controller->getLatestControl();
      if (!control.allFinite()) return 90;
      const auto metadata = controller->getLatestMetadata();
      CartpoleStepOutput output{};
      output.k = k;
      output.solver_status = metadata.at("solver_status").get<int>();
      output.is_feasible = metadata.at("is_feasible").get<bool>();
      output.cost_available = metadata.at("cost_available").get<bool>();
      output.u = control(0);
      if (output.cost_available) output.cost = metadata.at("cost").get<double>();
      output.cycles = c1 - c0;
      output.instret = i1 - i0;
#ifdef __riscv
      output.heap_peak = rsharc_heap_high_water();
      output.heap_capacity = rsharc_heap_capacity();
#endif
      if (!send(output)) return 89;
    }
    return 0;
  } catch (const std::exception&) {
    return 90;
  }
}
