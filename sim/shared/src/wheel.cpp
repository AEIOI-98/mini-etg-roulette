#include "mini_etg/sim/wheel.hpp"

namespace mini_etg::sim {

namespace {
constexpr TickMs kHalfClockRange = TickMs{1} << 31;
}

SimulatedWheel::SimulatedWheel(IRandom& random, WheelTiming timing) noexcept
    : random_(random), timing_(timing) {}

Error SimulatedWheel::arm_fault(SimFault fault) noexcept {
    if (static_cast<unsigned>(fault) > static_cast<unsigned>(SimFault::StartFailure)) {
        return Error::InvalidConfig;
    }
    next_fault_ = fault;
    return Error::Ok;
}

Error SimulatedWheel::start(RoundId round_id, TickMs now_ms) noexcept {
    stop();
    const SimFault fault = next_fault_;
    next_fault_ = SimFault::None;
    if (round_id == 0 || timing_.motion_delay_ms == 0 ||
        timing_.motion_delay_ms >= timing_.result_delay_ms ||
        timing_.result_delay_ms >= kHalfClockRange) {
        return Error::InvalidConfig;
    }

    // Every valid attempt consumes a pocket draw, even a deliberately faulted
    // one. Later rounds therefore retain the same seeded wheel sequence.
    std::uint32_t index = 0;
    const Error error = uniform_below(random_, static_cast<std::uint32_t>(kPocketCount), index);
    if (error != Error::Ok) return error;
    if (fault == SimFault::StartFailure) return Error::WheelError;

    round_id_ = round_id;
    started_at_ms_ = now_ms;
    pocket_ = PocketIndex{static_cast<std::uint8_t>(index)};
    active_fault_ = fault;
    active_ = true;
    return Error::Ok;
}

WheelBatch SimulatedWheel::poll(TickMs now_ms) noexcept {
    WheelBatch batch{};
    if (!active_ || active_fault_ == SimFault::NoSpin) return batch;
    const TickMs elapsed = now_ms - started_at_ms_;
    // The engine rejects ambiguous/backward clock samples. Do not turn such a
    // sample into a future observation when the adapter is exercised directly.
    if (elapsed >= kHalfClockRange) return batch;

    if (!motion_sent_ && elapsed >= timing_.motion_delay_ms) {
        batch.reports[batch.count++] = {
            round_id_, WheelSignal::MotionDetected,
            started_at_ms_ + timing_.motion_delay_ms, {}, FaultCode::None
        };
        motion_sent_ = true;
    }
    if (active_fault_ != SimFault::BallTimeout && elapsed >= timing_.result_delay_ms) {
        // Deliberately malformed sensor evidence is checked by the real engine.
        const PocketIndex reported = active_fault_ == SimFault::InvalidSensor
            ? PocketIndex{static_cast<std::uint8_t>(kPocketCount)} : pocket_;
        batch.reports[batch.count++] = {
            round_id_, WheelSignal::BallSettled,
            started_at_ms_ + timing_.result_delay_ms, reported, FaultCode::None
        };
        active_ = false;
    }
    return batch;
}

void SimulatedWheel::stop() noexcept {
    active_ = false;
    motion_sent_ = false;
    active_fault_ = SimFault::None;
    round_id_ = 0;
    started_at_ms_ = 0;
    pocket_ = {};
}

} // namespace mini_etg::sim
