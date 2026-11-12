#pragma once

#include "mini_etg/wheel.hpp"

namespace mini_etg::firmware {

// A single sample may contain simultaneous edges. The pocket at an index
// crossing completes the preceding revolution; its new printed position is zero.
struct SensorSample {
    TickMs at{};
    bool pocket{};
    bool index{};
    bool settled{};
    bool lost{};
};

// Demo transducers and interrupt-to-loop FIFO. The physical timer supplies only
// time; the simulated index keeps rotating even when one pocket sensor fails.
class SensorCapture {
public:
    static constexpr std::size_t capacity = 64;
    static constexpr unsigned pockets = kPocketCount;
    static constexpr unsigned last_tick = 3 * pockets;
    static constexpr unsigned period_ms = 30;

    void start(unsigned landing_offset) noexcept {
        stop();
        landing_tick_ = 2 * pockets + landing_offset;
        skip_ = armed_;
        armed_ = false;
        state_ = State::Recording;
    }

    void stop() noexcept {
        state_ = State::Idle;
        skip_ = false;
        tick_ = 0;
        next_index_ = 0;
        head_ = 0;
        count_ = 0;
        overrun_ = false;
    }

    bool arm_fault() noexcept {
        // A complete or overflowed capture can still be waiting in the FIFO
        // while the engine shows Spinning. Never carry that button press over.
        if (state_ == State::AwaitingPoll) return false;
        if (state_ == State::Recording) skip_ = true;
        else armed_ = true;
        return true;
    }

    // Inlined into the IRAM timer handler on ESP32. No allocation, division,
    // engine calls, device I/O, or iteration over the FIFO in the ISR.
    __attribute__((always_inline)) void capture_tick(TickMs at) noexcept {
        if (state_ != State::Recording) return;
        if (count_ == capacity) {
            overrun_ = true;
            lost_at_ = at;
            state_ = State::AwaitingPoll;
            return;
        }
        const bool index = tick_ == next_index_;
        if (index) next_index_ += pockets;
        const bool pocket = tick_ > 0 && !skip_;
        if (tick_ > 0) skip_ = false;
        samples_[(head_ + count_) % capacity] = {at, pocket, index, tick_ == landing_tick_, false};
        ++count_;
        if (tick_ == last_tick) state_ = State::AwaitingPoll;
        ++tick_;
    }

    // The caller protects both read and write with the same ESP32 spinlock.
    // A clock sampled just before an interrupt must leave its newer edge queued.
    bool pop_through(TickMs now, SensorSample& sample) noexcept {
        if (count_ > 0) {
            if (now - samples_[head_].at >= (TickMs{1} << 31)) return false;
            sample = samples_[head_];
            head_ = (head_ + 1) % capacity;
            --count_;
            return true;
        }
        if (overrun_ && now - lost_at_ < (TickMs{1} << 31)) {
            sample = {lost_at_, false, false, false, true};
            overrun_ = false;
            return true;
        }
        return false;
    }

private:
    SensorSample samples_[capacity]{};
    std::size_t head_{}, count_{};
    unsigned tick_{}, next_index_{}, landing_tick_{};
    TickMs lost_at_{};
    enum class State { Idle, Recording, AwaitingPoll };
    State state_{State::Idle};
    bool armed_{}, skip_{}, overrun_{};
};

// This decoder has no access to the demo's random draw, timer tick number or
// fault switch. The only evidence it accepts is timestamped sensor edges.
class PocketDetector {
public:
    void start(RoundId id) noexcept {
        round_ = id;
        pulses_ = 0;
        indexed_ = false;
        landed_ = false;
        done_ = false;
    }

    bool observe(const SensorSample& sample, WheelReport& report) noexcept {
        if (done_) return false;
        if (sample.lost) return invalid(sample.at, report);

        if (!indexed_) {
            if (sample.settled) return invalid(sample.at, report);
            if (!sample.index) return false;
            indexed_ = true;
            report = {round_, WheelSignal::MotionDetected, sample.at, {}, FaultCode::None};
            return true;
        }

        if (sample.pocket && ++pulses_ > kPocketCount) return invalid(sample.at, report);
        if (sample.index) {
            if (pulses_ != kPocketCount) return invalid(sample.at, report);
            pulses_ = 0;
            if (landed_) {
                if (sample.settled) return invalid(sample.at, report);
                // This index is when the result becomes verifiable. A bad last
                // revolution must never pay out a plausible but incorrect pocket.
                done_ = true;
                report = {round_, WheelSignal::BallSettled, sample.at, landing_, FaultCode::None};
                return true;
            }
        }
        if (sample.settled) {
            if (landed_ || pulses_ >= kPocketCount) return invalid(sample.at, report);
            landing_ = PocketIndex{pulses_};
            landed_ = true;
        }
        return false;
    }

private:
    bool invalid(TickMs at, WheelReport& report) noexcept {
        done_ = true;
        report = {round_, WheelSignal::Fault, at, {}, FaultCode::InvalidSensor};
        return true;
    }

    RoundId round_{};
    PocketIndex landing_{};
    std::uint8_t pulses_{};
    bool indexed_{}, landed_{}, done_{true};
};

} // namespace mini_etg::firmware
