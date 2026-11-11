#pragma once

#include "mini_etg/random.hpp"
#include "mini_etg/wheel.hpp"

namespace mini_etg::sim {

enum class SimFault : std::uint8_t {
    None, NoSpin, BallTimeout, InvalidSensor, StartFailure
};

struct WheelTiming {
    TickMs motion_delay_ms{100};
    TickMs result_delay_ms{1000}; // 0 < motion < result < 2^31.
};

// A deterministic test/demo device, driven by the caller's virtual clock.
// It supplies physical pocket indices through IWheel; only Engine settles bets.
class SimulatedWheel final : public IWheel {
public:
    explicit SimulatedWheel(IRandom& random, WheelTiming timing = {}) noexcept;

    // Arm the next attempted start, including a failed start. Replaces any
    // earlier arm; None cancels it. Arming during a spin affects the next spin.
    Error arm_fault(SimFault fault) noexcept;
    Error start(RoundId round_id, TickMs now_ms) noexcept override;
    WheelBatch poll(TickMs now_ms) noexcept override;
    // Discards active observations; an explicitly armed next fault remains.
    void stop() noexcept override;

private:
    IRandom& random_;
    WheelTiming timing_;
    SimFault next_fault_{SimFault::None};
    SimFault active_fault_{SimFault::None};
    RoundId round_id_{0};
    TickMs started_at_ms_{0};
    PocketIndex pocket_{};
    bool active_{false};
    bool motion_sent_{false};
};

} // namespace mini_etg::sim
