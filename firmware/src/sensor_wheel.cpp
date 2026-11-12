#include "sensor_wheel.hpp"

namespace mini_etg::firmware {
SensorWheel* SensorWheel::instance_ = nullptr;

bool SensorWheel::begin() noexcept {
    if (timer_) return true;
    if (instance_) return false;
    timer_ = timerBegin(0, 80, true); // 80 MHz APB / 80 = 1 tick per microsecond.
    if (!timer_) return false;
    instance_ = this;
    timerAttachInterrupt(timer_, &SensorWheel::on_timer, false);
    timerAlarmWrite(timer_, SensorCapture::period_ms * 1000, true);
    timerAlarmEnable(timer_);
    return true;
}

void IRAM_ATTR SensorWheel::on_timer() {
    SensorWheel& wheel = *instance_;
    portENTER_CRITICAL_ISR(&wheel.lock_);
    wheel.capture_.capture_tick(millis());
    portEXIT_CRITICAL_ISR(&wheel.lock_);
}

Error SensorWheel::start(RoundId round, TickMs) noexcept {
    stop();
    if (!timer_) return Error::WheelError;
    if (round == 0) return Error::InvalidConfig;

    // Randomize the ball's flight duration in the demo, before enabling
    // capture. The engine can obtain an outcome only by observing the sensors.
    std::uint32_t offset = 0;
    const Error error = uniform_below(random_, kPocketCount, offset);
    if (error != Error::Ok) return error;
    detector_.start(round);
    portENTER_CRITICAL(&lock_);
    capture_.start(offset);
    portEXIT_CRITICAL(&lock_);
    return Error::Ok;
}

void SensorWheel::arm_fault() noexcept {
    portENTER_CRITICAL(&lock_);
    capture_.arm_fault();
    portEXIT_CRITICAL(&lock_);
}

WheelBatch SensorWheel::poll(TickMs now) noexcept {
    WheelBatch batch{};
    // Bounded even if a new interrupt arrives while draining a backlog. The
    // extra slot carries the timestamp of the first sample lost to overflow.
    for (std::size_t i = 0; i <= SensorCapture::capacity; ++i) {
        SensorSample sample{};
        portENTER_CRITICAL(&lock_);
        const bool available = capture_.pop_through(now, sample);
        portEXIT_CRITICAL(&lock_);
        if (!available) break;
        WheelReport report{};
        if (detector_.observe(sample, report)) {
            batch.reports[batch.count++] = report;
            if (report.signal != WheelSignal::MotionDetected) break;
        }
    }
    return batch;
}

void SensorWheel::stop() noexcept {
    portENTER_CRITICAL(&lock_);
    capture_.stop();
    portEXIT_CRITICAL(&lock_);
}
} // namespace mini_etg::firmware
