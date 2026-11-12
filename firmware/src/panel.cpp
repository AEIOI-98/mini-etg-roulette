#include "panel.hpp"
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <cmath>
#include <cstdio>

namespace mini_etg::firmware {
namespace {
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
constexpr float tau = 6.283185307f;

void line(int x, int y, const char* value, int size = 1) {
    oled.setCursor(x, y);
    oled.setTextSize(size);
    oled.print(value);
}

void wheel(float turns) {
    constexpr int x = 23, y = 36;
    oled.drawCircle(x, y, 21, SSD1306_WHITE);
    oled.drawCircle(x, y, 15, SSD1306_WHITE);
    for (int spoke = 0; spoke < 8; ++spoke) {
        const float angle = tau * spoke / 8.0f;
        const int dx = std::sin(angle) * 14, dy = -std::cos(angle) * 14;
        oled.drawLine(x + dx / 3, y + dy / 3, x + dx, y + dy, SSD1306_WHITE);
    }
    const float angle = turns * tau;
    oled.fillCircle(x + std::lround(18 * std::sin(angle)),
                    y - std::lround(18 * std::cos(angle)), 2, SSD1306_WHITE);
}
} // namespace

bool begin_panel() {
    Wire.begin(21, 22);
    Wire.setTimeOut(30);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) return false;
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextWrap(false);
    return true;
}

const char* state_name(RoundState state) {
    switch (state) {
    case RoundState::Idle: return "Idle";
    case RoundState::BetsOpen: return "Bets open";
    case RoundState::BetsClosing: return "Closing";
    case RoundState::BetsClosed: return "Bets closed";
    case RoundState::Spinning: return "Spinning";
    case RoundState::Result: return "Result";
    case RoundState::Payout: return "Payout";
    case RoundState::Fault: return "Fault";
    }
    return "Unknown";
}

const char* fault_name(FaultCode fault) {
    switch (fault) {
    case FaultCode::None: return "";
    case FaultCode::InvalidSensor: return "Bad pulse count";
    case FaultCode::NoSpin: return "No motion";
    case FaultCode::BallTimeout: return "Ball timeout";
    case FaultCode::PowerLoss: return "Power loss";
    case FaultCode::WheelStartFailed: return "Wheel start failed";
    case FaultCode::StaleWheelReport: return "Stale sensor data";
    case FaultCode::OperatorInjected: return "Operator fault";
    }
    return "Unknown fault";
}

void draw_panel(const Snapshot* saved, TickMs elapsed, const char* notice) {
    oled.clearDisplay();
    char text[40];
    if (!saved) {
        line(0, 1, "Controller paused");
        line(0, 22, notice);
        line(0, 48, "See serial monitor");
    } else {
        const Snapshot& s = *saved;
        const Round& r = s.round;
        std::snprintf(text, sizeof(text), "%s  #%llu", state_name(r.state),
                      static_cast<unsigned long long>(r.id));
        line(0, 0, text);
        oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
        if (r.state == RoundState::Spinning) {
            wheel(static_cast<float>(elapsed % 800) / 800.0f);
            line(49, 21, "Reading");
            line(49, 33, "wheel pulses");
        } else if ((r.state == RoundState::Result || r.state == RoundState::Payout) && r.has_result) {
            const float target = static_cast<float>(r.result_index.value) / kPocketCount;
            // The engine already saved the result. Ease two turns into its pocket.
            const float left = r.state == RoundState::Result && elapsed < 1100 ?
                               1.0f - elapsed / 1100.0f : 0.0f;
            wheel(target - 2.0f * left * left);
            std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(r.result_number.value));
            line(56, 16, text, 3);
            std::snprintf(text, sizeof(text), "%lld cr", static_cast<long long>(s.seats[0].credits));
            line(52, 43, text);
        } else {
            std::snprintf(text, sizeof(text), "Credit %lld", static_cast<long long>(s.seats[0].credits));
            line(0, 16, text);
            if (r.state == RoundState::Fault) {
                line(0, 30, fault_name(r.fault));
                line(0, 41, "B: acknowledge");
            } else {
                std::snprintf(text, sizeof(text), "Stake  %lld", static_cast<long long>(r.escrow));
                line(0, 29, text);
                const char* action = r.state == RoundState::Idle ? "B: open betting" :
                                     r.state == RoundState::BetsOpen ? "B: close betting" :
                                     r.state == RoundState::BetsClosed ? "L: launch ball" : "Locking bets...";
                line(0, 42, action);
            }
        }
        line(0, 56, notice);
    }
    oled.display();
}
} // namespace mini_etg::firmware
