#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "ACC_Step_Types.h"
#include "acc_capture.hpp"

// Local binary channel: x86-64 and RV64 little-endian, IEEE binary64.
// Requests use the existing controller input. Replies use the capture record;
// x_end is filled by the host plant. No serialization changes floating bits.
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(double) == 8 && sizeof(int) == 4);
static_assert(std::is_trivially_copyable_v<ACCStepInput>);
static_assert(sizeof(ACCStepInput) == 72 && offsetof(ACCStepInput, t) == 8);
static_assert(offsetof(ACCStepInput, x) == 16 && offsetof(ACCStepInput, w) == 40);
static_assert(offsetof(ACCStepInput, u_previous) == 56);
static_assert(std::is_trivially_copyable_v<ACCCaptureRecord>);

// One request in flight. Mailbox is used only by the CVA6 runner, never by MPC.
// 0=acknowledged, 1=ready, 2=request published, 3=response published.
struct alignas(64) ACCMailbox {
  std::uint64_t state;
  std::uint64_t reserved[7];
  alignas(64) ACCStepInput input;
  alignas(64) ACCCaptureRecord output;
};
static_assert(offsetof(ACCMailbox, input) == 64);
static_assert(offsetof(ACCMailbox, output) == 192);
static_assert(sizeof(ACCMailbox) == 448);
