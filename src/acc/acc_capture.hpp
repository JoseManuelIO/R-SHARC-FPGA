#pragma once

#include <cstdint>

// Fixed-size observation buffer; solver allocation and mathematics are unchanged.
// All fields occupy eight bytes. The host decodes the RV64 little-endian ABI.
struct ACCCaptureRecord {
  std::uint64_t k;
  double t_start, t_end;
  double x_start[3], x_end[3], u[2], u_applied_start[2], w[2];
  std::int64_t controller_status, solver_status;
  std::uint64_t iterations, is_feasible;
  double objective, primal_residual, dual_residual;
  std::uint64_t adaptive_rho_interval, rho_updates;
  double rho_estimate, rho_final;
  // ABI 2 uses reserved slots for frequency, start/delay double bits and flags.
  std::uint64_t cycles, instret, reserved[4];
};

struct ACCCapture {
  std::uint64_t magic, version, record_bytes, count, capacity, complete;
  std::uint64_t heap_peak, heap_capacity;
  ACCCaptureRecord records[64];
};

static_assert(sizeof(ACCCaptureRecord) == 256);
static_assert(sizeof(ACCCapture) == 16448);

extern "C" volatile ACCCapture rsharc_acc_capture;
