#include <unity.h>
#include "../../firmware/src/sensor_capture.hpp"

using namespace mini_etg;
using namespace mini_etg::firmware;

namespace {
constexpr TickMs kStart = 0xfffffff0u; // Cross the millis() rollover during every trace.
constexpr TickMs timestamp(unsigned tick) {
    return kStart + tick * SensorCapture::period_ms;
}

void test_every_pocket_requires_a_full_clean_landing_revolution() {
    for (unsigned offset = 0; offset < kPocketCount; ++offset) {
        SensorCapture capture;
        PocketDetector detector;
        capture.start(offset);
        detector.start(7);
        unsigned reports = 0;
        for (unsigned i = 0; i <= SensorCapture::last_tick + 1; ++i) {
            capture.capture_tick(timestamp(i));
            SensorSample sample{};
            TEST_ASSERT_FALSE(capture.pop_through(timestamp(i) - 1, sample));
            if (!capture.pop_through(timestamp(i), sample)) {
                TEST_ASSERT_EQUAL_UINT(SensorCapture::last_tick + 1, i);
                continue;
            }
            WheelReport report{};
            if (!detector.observe(sample, report)) continue;
            TEST_ASSERT_EQUAL_UINT64(7, report.round_id);
            TEST_ASSERT_EQUAL_UINT32(timestamp(i), report.observed_at_ms);
            if (reports++ == 0) {
                TEST_ASSERT_EQUAL_UINT(0, i);
                TEST_ASSERT_EQUAL_INT(static_cast<int>(WheelSignal::MotionDetected),
                                      static_cast<int>(report.signal));
            } else {
                TEST_ASSERT_EQUAL_UINT(SensorCapture::last_tick, i);
                TEST_ASSERT_EQUAL_INT(static_cast<int>(WheelSignal::BallSettled),
                                      static_cast<int>(report.signal));
                TEST_ASSERT_EQUAL_UINT8(offset, report.pocket.value);
            }
        }
        TEST_ASSERT_EQUAL_UINT(2, reports);
    }
}

void test_missing_pocket_at_any_position_faults_including_after_ball_lands() {
    for (unsigned missing = 1; missing <= SensorCapture::last_tick; ++missing) {
        SensorCapture capture;
        PocketDetector detector;
        capture.start(0);
        detector.start(9);
        unsigned faults = 0;
        for (unsigned i = 0; i <= SensorCapture::last_tick; ++i) {
            if (i == missing) capture.arm_fault();
            capture.capture_tick(timestamp(i));
            SensorSample sample{};
            TEST_ASSERT_TRUE(capture.pop_through(timestamp(i), sample));
            WheelReport report{};
            if (!detector.observe(sample, report) || i == 0) continue;
            TEST_ASSERT_EQUAL_INT(static_cast<int>(WheelSignal::Fault), static_cast<int>(report.signal));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultCode::InvalidSensor), static_cast<int>(report.fault));
            const auto missed_revolution = (missing - 1) / kPocketCount + 1;
            TEST_ASSERT_EQUAL_UINT(missed_revolution * kPocketCount, i);
            ++faults;
        }
        TEST_ASSERT_EQUAL_UINT(1, faults);
    }
}

void test_too_many_pocket_pulses_fault_before_next_index() {
    PocketDetector detector;
    detector.start(1);
    WheelReport report{};
    TEST_ASSERT_TRUE(detector.observe({1, false, true, false, false}, report));
    for (unsigned i = 1; i <= kPocketCount; ++i)
        TEST_ASSERT_FALSE(detector.observe({i + 1, true, false, false, false}, report));
    TEST_ASSERT_TRUE(detector.observe({39, true, false, false, false}, report));
    TEST_ASSERT_EQUAL_UINT32(39, report.observed_at_ms);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultCode::InvalidSensor), static_cast<int>(report.fault));
    TEST_ASSERT_FALSE(detector.observe({40, true, true, true, false}, report));
}

void test_settle_before_sync_or_twice_in_one_revolution_is_invalid() {
    PocketDetector detector;
    WheelReport report{};
    detector.start(1);
    TEST_ASSERT_FALSE(detector.observe({1, true, false, false, false}, report));
    TEST_ASSERT_TRUE(detector.observe({2, true, true, true, false}, report));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultCode::InvalidSensor), static_cast<int>(report.fault));
    detector.start(2);
    TEST_ASSERT_TRUE(detector.observe({1, false, true, false, false}, report));
    TEST_ASSERT_FALSE(detector.observe({2, true, false, true, false}, report));
    TEST_ASSERT_TRUE(detector.observe({3, true, false, true, false}, report));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultCode::InvalidSensor), static_cast<int>(report.fault));
}

void test_full_fifo_preserves_motion_and_timestamps_first_lost_input() {
    SensorCapture capture;
    PocketDetector detector;
    capture.start(5);
    detector.start(3);
    for (unsigned i = 0; i <= SensorCapture::last_tick; ++i)
        capture.capture_tick(timestamp(i));
    const TickMs lost_at = timestamp(SensorCapture::capacity);
    SensorSample sample{};
    WheelReport report{};
    for (unsigned i = 0; i < SensorCapture::capacity; ++i) {
        TEST_ASSERT_TRUE(capture.pop_through(lost_at - 1, sample));
        TEST_ASSERT_EQUAL_UINT32(timestamp(i), sample.at);
        TEST_ASSERT_EQUAL_INT(i == 0, detector.observe(sample, report));
    }
    TEST_ASSERT_FALSE(capture.pop_through(lost_at - 1, sample));
    TEST_ASSERT_TRUE(capture.pop_through(lost_at, sample));
    TEST_ASSERT_TRUE(detector.observe(sample, report));
    TEST_ASSERT_EQUAL_UINT32(lost_at, report.observed_at_ms);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultCode::InvalidSensor), static_cast<int>(report.fault));
    TEST_ASSERT_FALSE(capture.pop_through(lost_at, sample));
}

void test_a_late_fault_press_cannot_affect_the_next_spin() {
    for (bool overflow : {false, true}) {
        SensorCapture capture;
        SensorSample sample{};
        capture.start(10);
        for (unsigned i = 0; i <= SensorCapture::last_tick; ++i) {
            capture.capture_tick(timestamp(i));
            if (!overflow && i < SensorCapture::last_tick)
                TEST_ASSERT_TRUE(capture.pop_through(timestamp(i), sample));
        }
        TEST_ASSERT_FALSE(capture.arm_fault());
        capture.stop();
        capture.start(20);
        capture.capture_tick(220);
        capture.capture_tick(250);
        TEST_ASSERT_TRUE(capture.pop_through(250, sample));
        TEST_ASSERT_TRUE(capture.pop_through(250, sample));
        TEST_ASSERT_TRUE(sample.pocket);
    }
}

void test_stop_clears_pending_samples_but_preserves_explicit_next_fault() {
    SensorCapture capture;
    capture.start(13);
    capture.capture_tick(10);
    capture.stop();
    SensorSample sample{};
    TEST_ASSERT_FALSE(capture.pop_through(100, sample));
    capture.arm_fault();
    capture.stop();
    capture.start(2);
    capture.capture_tick(110);
    capture.capture_tick(140);
    TEST_ASSERT_TRUE(capture.pop_through(110, sample));
    TEST_ASSERT_TRUE(sample.index);
    TEST_ASSERT_FALSE(capture.pop_through(110, sample));
    TEST_ASSERT_TRUE(capture.pop_through(140, sample));
    TEST_ASSERT_FALSE(sample.pocket);
    capture.start(4);
    capture.capture_tick(170);
    capture.capture_tick(200);
    TEST_ASSERT_TRUE(capture.pop_through(200, sample));
    TEST_ASSERT_TRUE(capture.pop_through(200, sample));
    TEST_ASSERT_TRUE(sample.pocket);
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_pocket_requires_a_full_clean_landing_revolution);
    RUN_TEST(test_missing_pocket_at_any_position_faults_including_after_ball_lands);
    RUN_TEST(test_too_many_pocket_pulses_fault_before_next_index);
    RUN_TEST(test_settle_before_sync_or_twice_in_one_revolution_is_invalid);
    RUN_TEST(test_full_fifo_preserves_motion_and_timestamps_first_lost_input);
    RUN_TEST(test_a_late_fault_press_cannot_affect_the_next_spin);
    RUN_TEST(test_stop_clears_pending_samples_but_preserves_explicit_next_fault);
    return UNITY_END();
}
