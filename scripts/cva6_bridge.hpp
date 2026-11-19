#pragma once
// Simulation adapter only. Runs after both eval() calls of a complete cycle.
#include "acc_channel.hpp"
#include "cartpole_channel.hpp"
#define RSHARC_CVA6_PROFILE_HOST 1
#include "cva6_profile.h"
#include <cstdio>
#include <limits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

class MPCBridge {
  unsigned char* ram_base;
  std::size_t ram_size;
  unsigned char* mailbox;
  int request_fd, response_fd;
  bool cartpole;
  std::size_t input_size, output_size, input_offset, output_offset;
  std::uint64_t requests = 0, responses = 0;
  static void transfer(int fd, void* data, std::size_t size, bool reading) {
    auto* cursor = static_cast<unsigned char*>(data);
    while (size) {
      const auto n = reading ? ::read(fd, cursor, size) : ::write(fd, cursor, size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("MPC channel EOF or I/O error");
      cursor += n; size -= n;
    }
  }
  static unsigned long long env(const char* key) {
    const char* text = std::getenv(key);
    char* end = nullptr;
    if (!text || !*text) throw std::runtime_error("MPC channel environment missing");
    errno = 0;
    const auto value = std::strtoull(text, &end, 0);
    if (errno || *end) throw std::runtime_error("Invalid MPC channel environment");
    return value;
  }
 public:
  MPCBridge(unsigned char* ram, std::size_t size) : ram_base(ram), ram_size(size) {
    const char* controller = std::getenv("MPC_CONTROLLER");
    if (!controller || (std::strcmp(controller, "acc") && std::strcmp(controller, "cartpole")))
      throw std::runtime_error("MPC_CONTROLLER must be acc or cartpole");
    cartpole = std::strcmp(controller, "cartpole") == 0;
    const auto mailbox_size = cartpole ? sizeof(CartpoleMailbox) : sizeof(ACCMailbox);
    input_size = cartpole ? sizeof(CartpoleStepInput) : sizeof(ACCStepInput);
    output_size = cartpole ? sizeof(CartpoleStepOutput) : sizeof(ACCCaptureRecord);
    input_offset = cartpole ? offsetof(CartpoleMailbox, input) : offsetof(ACCMailbox, input);
    output_offset = cartpole ? offsetof(CartpoleMailbox, output) : offsetof(ACCMailbox, output);
    const auto address = env("MPC_MAILBOX_ADDRESS");
    if (size < mailbox_size || address % 64 || address < 0x80000000ULL ||
        address - 0x80000000ULL > size - mailbox_size)
      throw std::runtime_error("MPC mailbox outside RAM or misaligned");
    mailbox = ram + address - 0x80000000ULL;
    const auto request = env("MPC_REQUEST_FD"), response = env("MPC_RESPONSE_FD");
    if (request > std::numeric_limits<int>::max() || response > std::numeric_limits<int>::max())
      throw std::runtime_error("Invalid MPC file descriptor");
    request_fd = static_cast<int>(request);
    response_fd = static_cast<int>(response);
    // Marker also lets the launcher reject a dump-only runner before execution.
    std::fprintf(stderr, "RSHARC_MPC_CHANNEL_V1 controller=%s\n", controller);
  }
  void tick() {
    std::uint64_t state;
    std::memcpy(&state, mailbox, sizeof(state));
    if (state == 1) {
      if (requests != responses) throw std::runtime_error("Overlapping MPC request");
      unsigned char input[sizeof(ACCStepInput)]{};
      static_assert(sizeof(CartpoleStepInput) <= sizeof(input));
      // Block the simulator between cycles while the host integrates the plant.
      transfer(request_fd, input, input_size, true);
      std::uint64_t index = 0;
      std::memcpy(&index, input, cartpole ? sizeof(std::uint64_t) : sizeof(int));
      if (index != requests) throw std::runtime_error("MPC request sequence");
      std::memcpy(mailbox + input_offset, input, input_size);
      state = 2; ++requests;
      std::memcpy(mailbox, &state, sizeof(state));
    } else if (state == 3) {
      if (requests != responses + 1) throw std::runtime_error("Unexpected MPC response");
      unsigned char output[sizeof(ACCCaptureRecord)]{};
      static_assert(sizeof(CartpoleStepOutput) <= sizeof(output));
      std::memcpy(output, mailbox + output_offset, output_size);
      std::uint64_t index;
      std::memcpy(&index, output, sizeof(index));
      if (index != responses) throw std::runtime_error("MPC response sequence");
            // Recoger los contadores publicados por el firmware CartPole.
      if (cartpole) {
        CartpoleStepOutput decoded{};
        std::uint64_t reserved[7]{};

        std::memcpy(&decoded, output, sizeof(decoded));

        std::memcpy(
            reserved,
            mailbox + offsetof(CartpoleMailbox, reserved),
            sizeof(reserved));

        if (reserved[0] == CVA6_REGION_MAGIC) {
          const auto address = reserved[1];
          if (reserved[2] != sizeof(cva6_region_packet) || address % 64 ||
              address < 0x80000000ULL || ram_size < sizeof(cva6_region_packet) ||
              address - 0x80000000ULL > ram_size - sizeof(cva6_region_packet))
            throw std::runtime_error("Invalid MPC region packet address/size");
          cva6_region_packet packet{};
          std::memcpy(&packet, ram_base + address - 0x80000000ULL, sizeof(packet));
          if (packet.schema != 1 || packet.k != decoded.k ||
              packet.global.available != 1 || packet.global.cycles != decoded.cycles ||
              packet.global.instret != decoded.instret)
            throw std::runtime_error("Invalid MPC region packet schema/step/global");
          for (unsigned i = 0; i < 6; ++i) {
            if (packet.event_ids[i] != cva6_profile_event_ids[i])
              throw std::runtime_error("MPC region event selectors mismatch");
            reserved[i + 1] = packet.global.events[i];
          }
          reserved[0] = CVA6_PROFILE_MAGIC;
          cva6_region_log(stderr, &packet);
        }
        cva6_profile_log(
            stderr,
            decoded.k,
            decoded.cycles,
            decoded.instret,
            reserved);
      }
      transfer(response_fd, output, output_size, false);
      state = 0; ++responses;
      std::memcpy(mailbox, &state, sizeof(state));
    } else if (state != 0 && state != 2) {
      throw std::runtime_error("Invalid MPC mailbox state");
    }
  }
};
