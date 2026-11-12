#include "button.hpp"
#include <unity.h>
#include <limits>

using mini_etg::firmware::Button;
using mini_etg::firmware::Gesture;
namespace {
void tap_requires_stable_press_and_release() {
    Button b;
    b.begin(false, 0);
    TEST_ASSERT_TRUE(b.sample(true, 1) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(false, 5) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(true, 7) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(true, 32) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(false, 90) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(true, 94) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(false, 99) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(false, 124) == Gesture::Tap);
    TEST_ASSERT_TRUE(b.sample(false, 170) == Gesture::None);
}
void hold_does_not_launch_after_reset() {
    Button b;
    b.begin(false, 0);
    b.sample(true, 10, true);
    b.sample(true, 35, true);
    TEST_ASSERT_TRUE(b.sample(true, 1234, true) == Gesture::None);
    TEST_ASSERT_TRUE(b.sample(true, 1235, true) == Gesture::Hold);
    TEST_ASSERT_TRUE(b.sample(true, 1300, true) == Gesture::None);
    b.begin(true, 0); // restart while Launch is still held
    TEST_ASSERT_TRUE(b.sample(true, 2000, true) == Gesture::None);
    b.sample(false, 2010, true);
    TEST_ASSERT_TRUE(b.sample(false, 2040, true) == Gesture::None);
    b.sample(true, 2100, true);
    b.sample(true, 2130, true);
    b.sample(false, 2180, true);
    TEST_ASSERT_TRUE(b.sample(false, 2210, true) == Gesture::Tap);
}
void elapsed_time_crosses_millis_rollover() {
    Button b;
    constexpr auto near_wrap = std::numeric_limits<std::uint32_t>::max() - 10;
    b.begin(false, near_wrap);
    b.sample(true, near_wrap + 1);
    b.sample(true, 25);
    b.sample(false, 50);
    TEST_ASSERT_TRUE(b.sample(false, 75) == Gesture::Tap);
}
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(tap_requires_stable_press_and_release);
    RUN_TEST(hold_does_not_launch_after_reset);
    RUN_TEST(elapsed_time_crosses_millis_rollover);
    return UNITY_END();
}
