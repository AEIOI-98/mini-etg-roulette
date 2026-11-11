#pragma once

#include "mini_etg/storage.hpp"
#include "mini_etg/wheel.hpp"

namespace mini_etg::test {

// Tests control capture times independently of poll time. Raw batches also let
// tests deliberately violate the adapter contract and check engine validation.
class FakeWheel final : public IWheel {
public:
    explicit FakeWheel(IStorage* storage = nullptr) noexcept : storage_(storage) {}

    void reset() noexcept {
        next_ = {};
        start_error = Error::Ok;
        started_round = 0;
        started_at = 0;
        start_calls = 0;
        poll_calls = 0;
        stop_calls = 0;
        start_was_journalled = false;
    }

    Error start(RoundId round_id, TickMs now_ms) noexcept override {
        ++start_calls;
        started_round = round_id;
        started_at = now_ms;
        next_ = {};
        if (storage_ != nullptr) {
            Snapshot saved{};
            start_was_journalled = storage_->load_latest(saved) == Error::Ok
                && saved.round.state == RoundState::Spinning
                && saved.round.id == round_id;
        }
        return start_error;
    }

    WheelBatch poll(TickMs) noexcept override {
        ++poll_calls;
        const WheelBatch result = next_;
        next_ = {};
        return result;
    }

    void stop() noexcept override {
        ++stop_calls;
        next_ = {};
    }

    void push(WheelReport report) noexcept {
        if (next_.count < next_.reports.size()) {
            next_.reports[next_.count++] = report;
        }
    }

    void motion(TickMs observed_at_ms) noexcept {
        push({started_round, WheelSignal::MotionDetected, observed_at_ms, {}, FaultCode::None});
    }

    void ball(TickMs observed_at_ms, std::uint8_t index) noexcept {
        push({started_round, WheelSignal::BallSettled, observed_at_ms, {index}, FaultCode::None});
    }

    void fault(TickMs observed_at_ms, FaultCode code) noexcept {
        push({started_round, WheelSignal::Fault, observed_at_ms, {}, code});
    }

    void inject_batch(const WheelBatch& batch) noexcept { next_ = batch; }

    Error start_error{Error::Ok};
    RoundId started_round{0};
    TickMs started_at{0};
    std::size_t start_calls{0};
    std::size_t poll_calls{0};
    std::size_t stop_calls{0};
    bool start_was_journalled{false};

private:
    IStorage* storage_;
    WheelBatch next_{};
};

} // namespace mini_etg::test
