#pragma once

#include "mini_etg/types.hpp"

#include <array>

namespace mini_etg {

enum class Colour : std::uint8_t { Green, Red, Black };

// Clockwise from zero. Definition and reference tests follow after approval.
extern const std::array<Number, kPocketCount> kEuropeanWheelOrder;

Error number_at(PocketIndex index, Number& out) noexcept;
Error colour_of(Number number, Colour& out) noexcept;

enum class WheelSignal : std::uint8_t { NoUpdate, MotionDetected, BallSettled, Fault };

struct WheelReport {
    RoundId round_id{0};
    WheelSignal signal{WheelSignal::NoUpdate};
    PocketIndex pocket{}; // Meaningful only for BallSettled; core validates 0..36.
    FaultCode fault{FaultCode::None}; // Meaningful only for Fault.
};

class IWheel {
public:
    virtual ~IWheel() = default;

    // Non-blocking. Called only after Spinning is durably journalled.
    virtual Error start(RoundId round_id, TickMs now_ms) noexcept = 0;
    virtual WheelReport poll(TickMs now_ms) noexcept = 0;

    // Idempotent: stop the demo wheel and discard pending observations.
    virtual void stop() noexcept = 0;
};

} // namespace mini_etg
