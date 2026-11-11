#pragma once

#include "mini_etg/types.hpp"

#include <array>

namespace mini_etg {

enum class Colour : std::uint8_t { Green, Red, Black };

// Clockwise from zero. Definition and reference tests follow after approval.
extern const std::array<Number, kPocketCount> kEuropeanWheelOrder;

Error number_at(PocketIndex index, Number& out) noexcept;
Error colour_of(Number number, Colour& out) noexcept;

enum class WheelSignal : std::uint8_t { MotionDetected, BallSettled, Fault };

struct WheelReport {
    RoundId round_id{0};
    WheelSignal signal{WheelSignal::MotionDetected};
    TickMs observed_at_ms{0}; // Captured at observation, in start()/poll()'s clock.
    PocketIndex pocket{}; // Meaningful only for BallSettled; core validates 0..36.
    FaultCode fault{FaultCode::None}; // Meaningful only for Fault.
};

// One first-motion report and one terminal result/fault at most per start().
// Raw pocket pulses remain inside the adapter. A zero count means no update.
inline constexpr std::size_t kMaxWheelReportsPerPoll = 2;

struct WheelBatch {
    std::array<WheelReport, kMaxWheelReportsPerPoll> reports{};
    std::size_t count{0};
};

class IWheel {
public:
    virtual ~IWheel() = default;

    // Non-blocking. Called only after Spinning is durably journalled.
    virtual Error start(RoundId round_id, TickMs now_ms) noexcept = 0;
    // Non-blocking: return all queued reports through now_ms, in observation
    // order, once. Keep later observations for the next call. Timestamps must
    // not be replaced with poll time. A terminal report ends this start().
    // start()/stop() clear pending reports; lost raw sensor input reports Fault
    // with InvalidSensor instead of silently dropping evidence.
    virtual WheelBatch poll(TickMs now_ms) noexcept = 0;

    // Idempotent: stop the demo wheel and discard pending observations.
    virtual void stop() noexcept = 0;
};

} // namespace mini_etg
