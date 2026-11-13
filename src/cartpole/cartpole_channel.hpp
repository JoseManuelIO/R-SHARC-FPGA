#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// Local binary protocol for little-endian x86-64 and RV64.
struct CartpoleStepInput {
  std::uint64_t k;
  double t;
  double x[4];
  double w[1];
  double u_previous[1];
};
struct CartpoleStepOutput {
  std::uint64_t k;
  std::int64_t solver_status;
  std::uint64_t is_feasible;
  std::uint64_t cost_available;
  double u;
  double cost;
  std::uint64_t cycles;
  std::uint64_t instret;
  std::uint64_t heap_peak;
  std::uint64_t heap_capacity;
};

// 0=acknowledged, 1=ready, 2=request published, 3=response published.
struct alignas(64) CartpoleMailbox {
  std::uint64_t state;
  std::uint64_t reserved[7];
  alignas(64) CartpoleStepInput input;
  alignas(64) CartpoleStepOutput output;
};
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(std::is_standard_layout_v<CartpoleStepInput>);
static_assert(std::is_trivially_copyable_v<CartpoleStepInput>);
static_assert(std::is_standard_layout_v<CartpoleStepOutput>);
static_assert(std::is_trivially_copyable_v<CartpoleStepOutput>);
static_assert(sizeof(CartpoleStepInput) == 64);
static_assert(offsetof(CartpoleStepInput, t) == 8);
static_assert(offsetof(CartpoleStepInput, x) == 16);
static_assert(offsetof(CartpoleStepInput, w) == 48);
static_assert(offsetof(CartpoleStepInput, u_previous) == 56);
static_assert(sizeof(CartpoleStepOutput) == 80);
static_assert(offsetof(CartpoleStepOutput, u) == 32);
static_assert(offsetof(CartpoleStepOutput, cost) == 40);
static_assert(offsetof(CartpoleStepOutput, cycles) == 48);
static_assert(offsetof(CartpoleStepOutput, heap_peak) == 64);
static_assert(offsetof(CartpoleMailbox, input) == 64);
static_assert(offsetof(CartpoleMailbox, output) == 128);
static_assert(sizeof(CartpoleMailbox) == 256);
