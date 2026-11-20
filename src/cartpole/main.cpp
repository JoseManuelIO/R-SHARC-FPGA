#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <nlopt.h>
#include "NLMPCController.h"
#include "cartpole_channel.hpp"
#include "cartpole_config.hpp"
#include "debug_levels.hpp"
#include "cva6_profile.h"

DebugLevels global_debug_levels;

extern "C" {
cva6_profile_sample cp_totals[CP_REGION_COUNT] = {};
uint64_t cp_calls[CP_REGION_COUNT] = {};
}

#ifdef RSHARC_CARTPOLE_CVA6_CHANNEL
extern "C" { alignas(64) volatile CartpoleMailbox rsharc_cartpole_mailbox{}; }
alignas(64) static volatile cva6_region_packet region_packet;
static_assert(sizeof(cva6_region_packet) == 536);
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
// static bool send(const CartpoleStepOutput& output) {
//   const auto previous = exchange_begin();
//   const auto* src = reinterpret_cast<const unsigned char*>(&output);
//   auto* dst = reinterpret_cast<volatile unsigned char*>(&rsharc_cartpole_mailbox.output);
//   for (std::size_t i = 0; i < sizeof(output); ++i) dst[i] = src[i];
//   asm volatile("fence rw,rw" ::: "memory");
//   rsharc_cartpole_mailbox.state = 3;
//   asm volatile("fence rw,rw" ::: "memory");
//   while (rsharc_cartpole_mailbox.state != 0) {}
//   exchange_end(previous);
//   return true;
// }
// cambio para lectura de eventos
static bool send(
    const CartpoleStepOutput& output,
    const cva6_profile_sample& profile)
{
  cva6_region_packet packet{};
  packet.schema = RSHARC_CVA6_REGION_MODE == 2 ? 1 :
      (RSHARC_CVA6_REGION_MODE == 1 ? 2 : 3);
  packet.k = output.k;
  packet.global = profile;
  for (unsigned i = 0; i < 6; ++i)
    packet.event_ids[i] = cva6_profile_event_ids[i];
  for (unsigned r = 0; r < CP_REGION_COUNT; ++r) {
    packet.regions[r] = cp_totals[r];
    packet.calls[r] = cp_calls[r];
  }
  const auto previous = exchange_begin();

  const auto* src = reinterpret_cast<const unsigned char*>(&output);
  auto* dst = reinterpret_cast<volatile unsigned char*>(
      &rsharc_cartpole_mailbox.output);

  for (std::size_t i = 0; i < sizeof(output); ++i) {
    dst[i] = src[i];
  }

  // La RAM del runner es write-through; publicar la copia con bypass activo.
  const auto* packet_src = reinterpret_cast<const unsigned char*>(&packet);
  auto* packet_dst = reinterpret_cast<volatile unsigned char*>(&region_packet);
  for (std::size_t i = 0; i < sizeof(packet); ++i)
    packet_dst[i] = packet_src[i];
  for (unsigned i = 0; i < 7; ++i)
    rsharc_cartpole_mailbox.reserved[i] = 0;
  rsharc_cartpole_mailbox.reserved[1] =
      reinterpret_cast<std::uintptr_t>(&region_packet);
  rsharc_cartpole_mailbox.reserved[2] = sizeof(packet);
  rsharc_cartpole_mailbox.reserved[0] = CVA6_REGION_MAGIC;

  asm volatile("fence rw,rw" ::: "memory");
  rsharc_cartpole_mailbox.state = 3;
  asm volatile("fence rw,rw" ::: "memory");

  while (rsharc_cartpole_mailbox.state != 0) {}

  exchange_end(previous);
  return true;
}
//fin cambio


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
// static bool send(CartpoleStepOutput& output) { return transfer(64, &output, sizeof(output)); }
//cambio para spike
static bool send(
    CartpoleStepOutput& output,
    const cva6_profile_sample&)
{
  return transfer(64, &output, sizeof(output));
}

// fin cambio
#else
static bool receive(CartpoleStepInput& input) {
  return std::fread(&input, sizeof(input), 1, stdin) == 1;
}
// static bool send(const CartpoleStepOutput& output) {
//   return std::fwrite(&output, sizeof(output), 1, stdout) == 1 && std::fflush(stdout) == 0;
// }
//cambio para x86
static bool send(
    const CartpoleStepOutput& output,
    const cva6_profile_sample&)
{
  return std::fwrite(&output, sizeof(output), 1, stdout) == 1
      && std::fflush(stdout) == 0;
}

//fin cambio
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
    //se añade al controlador, cambios para contadores de eventods
#if CVA6_PROFILE_ACTIVE
    if (!cva6_profile_init()) return 91;
#endif
    //fin cambios
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
      // const auto c0 = cycle(), i0 = instret();
      // controller->calculateControl(static_cast<int>(k), input.t, state, disturbance);
      // const auto i1 = instret(), c1 = cycle();
      //Inicio cambio contadores de eventos
#if CVA6_PROFILE_ACTIVE
      for (unsigned r = 0; r < CP_REGION_COUNT; ++r) {
        cp_totals[r] = {};
        cp_totals[r].available = RSHARC_CVA6_REGION_MODE != 0;
        cp_calls[r] = 0;
      }
      const auto profile_begin = cva6_profile_read();
#else
      const auto c0 = cycle(), i0 = instret();
#endif

      controller->calculateControl(
          static_cast<int>(k), input.t, state, disturbance);

#if CVA6_PROFILE_ACTIVE
      const auto profile_end = cva6_profile_read();
      const auto profile = cva6_profile_delta(
          profile_begin, profile_end);
#else
      const auto i1 = instret(), c1 = cycle();
      const cva6_profile_sample profile{};
#endif
//fin cambio contadores de eventos
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
      // output.cycles = c1 - c0;
      // output.instret = i1 - i0;
      //cambios contadores de eventos
#if CVA6_PROFILE_ACTIVE
      output.cycles = profile.cycles;
      output.instret = profile.instret;
#else
      output.cycles = c1 - c0;
      output.instret = i1 - i0;
#endif
      //fin cambios contadores de eventos
#ifdef __riscv
      output.heap_peak = rsharc_heap_high_water();
      output.heap_capacity = rsharc_heap_capacity();
#endif
      // if (!send(output)) return 89;
      if (!send(output, profile)) return 89;
    }
    cva6_profile_stop();
    return 0;
  } catch (const std::exception&) {
    return 90;
  }
}
