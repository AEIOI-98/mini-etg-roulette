#pragma once

#include "mini_etg/random.hpp"
#include "sensor_capture.hpp"
#include <Arduino.h>

namespace mini_etg::firmware {

// One wheel owns ESP32 hardware timer 0. begin() runs after Arduino startup.
class SensorWheel final : public IWheel {
public:
    explicit SensorWheel(IRandom& random) noexcept : random_(random) {}
    bool begin() noexcept;
    void arm_fault() noexcept;
    Error start(RoundId round, TickMs now) noexcept override;
    WheelBatch poll(TickMs now) noexcept override;
    void stop() noexcept override;

private:
    static void IRAM_ATTR on_timer();
    static SensorWheel* instance_;
    IRandom& random_;
    hw_timer_t* timer_{};
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    SensorCapture capture_;
    PocketDetector detector_;
};

} // namespace mini_etg::firmware
