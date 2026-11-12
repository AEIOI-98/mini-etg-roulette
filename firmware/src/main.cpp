#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstring>
#include "mini_etg/engine.hpp"
#include "mini_etg/serial.hpp"
#include "button.hpp"
#include "nvs_storage.hpp"
#include "panel.hpp"
#include "sensor_wheel.hpp"

// The engine uses fixed ledger copies. Keep them static and allow room for
// checked settlement/initialization temporaries in the Arduino task.
SET_LOOP_TASK_STACK_SIZE(32768);

namespace {
using namespace mini_etg;
using namespace mini_etg::firmware;
constexpr int bet_pin = 25, launch_pin = 26, fault_pin = 27;
constexpr int open_led = 16, spin_led = 17, fault_led = 18;
constexpr Credits demo_stake = 10;

class BoardRandom final : public IRandom {
    std::uint32_t next_u32() noexcept override { return esp_random(); }
};
BoardRandom random_source;
SensorWheel wheel(random_source);
NvsStorage storage;
Engine engine(wheel, storage, random_source);
Button bets, launch, fault;
bool ready = false, display_ready = false, fault_armed = false, dirty = true;
TickMs entered = 0, last_draw = 0;
const char* message = "";
std::uint16_t frame_number = 0;

void send(const char* text) {
    serial::EncodedFrame frame{};
    // Sequence gaps remain visible to the host if the UART is back-pressured.
    if (serial::encode(frame_number++, reinterpret_cast<const std::uint8_t*>(text),
                       std::strlen(text), frame) &&
        Serial.availableForWrite() >= static_cast<int>(frame.size)) {
        Serial.write(frame.bytes.data(), frame.size);
    }
}

void notice(const char* text) {
    message = text;
    dirty = true;
}

void halt(const char* reason) {
    ready = false;
    wheel.stop();
    notice(reason);
    send(reason);
}

void check(Error error) {
    if (error == Error::Ok) return;
    if (error == Error::WheelError && engine.status() == RuntimeStatus::Ready &&
        engine.snapshot().round.state == RoundState::Fault) return;
    if (error == Error::StorageFull) halt("Journal full");
    else if (error == Error::StorageCorrupt) halt("Journal corrupt");
    else if (engine.status() != RuntimeStatus::Ready) halt("Journal unavailable");
    else notice("Command rejected");
    char text[45];
    std::snprintf(text, sizeof(text), "error=%u", static_cast<unsigned>(error));
    send(text);
}

void publish_status() {
    const Snapshot& s = engine.snapshot();
    char text[serial::kMaxPayload + 1];
    std::snprintf(text, sizeof(text), "state=%s round=%llu credit=%lld escrow=%lld",
                  state_name(s.round.state), static_cast<unsigned long long>(s.round.id),
                  static_cast<long long>(s.seats[0].credits), static_cast<long long>(s.round.escrow));
    send(text);
    if (s.round.state == RoundState::Result || s.round.state == RoundState::Payout) {
        std::snprintf(text, sizeof(text), "pocket=%u number=%u", s.round.result_index.value,
                      s.round.result_number.value);
        send(text);
    }
    if (s.round.state == RoundState::Fault) send(fault_name(s.round.fault));
    dirty = true;
}

void open_table() {
    Error error = engine.open_bets();
    if (error == Error::Ok && engine.snapshot().seats[0].credits >= demo_stake) {
        BetRequest request{};
        request.stake = demo_stake;
        error = make_even_money_bet(BetKind::Red, request.selection);
        BetId id{};
        if (error == Error::Ok) error = engine.place_bet(request, id);
        if (error == Error::Ok) notice("10 credits on red");
    } else if (error == Error::Ok) {
        notice("Seat skips this round");
    }
    check(error);
}

void press_bets(RoundState state) {
    switch (state) {
    case RoundState::Idle: open_table(); break;
    case RoundState::BetsOpen: check(engine.begin_closing()); break;
    case RoundState::Payout: check(engine.finish_round()); break;
    case RoundState::Fault: check(engine.acknowledge_fault()); break;
    default: break;
    }
}

void transition_notice(RoundState state) {
    switch (state) {
    case RoundState::Idle: notice("Hold L: reset"); break;
    case RoundState::BetsClosing: notice("No more bets"); break;
    case RoundState::BetsClosed: notice(fault_armed ? "Bad pulse armed" : "F: arm bad pulse"); break;
    case RoundState::Spinning: notice("F: drop one pulse"); break;
    case RoundState::Result: notice("Recorded result"); break;
    case RoundState::Payout: notice("Paid / B: finish"); break;
    case RoundState::Fault: notice("Stakes refunded"); break;
    case RoundState::BetsOpen: break;
    }
}

void boot() {
    if (esp_task_wdt_init(8, true) != ESP_OK) { halt("Watchdog init failed"); return; }
    enableLoopWDT();
    if (esp_task_wdt_status(nullptr) != ESP_OK) { halt("Watchdog unavailable"); return; }
    if (!display_ready) { halt("OLED init failed"); return; }
    if (!wheel.begin()) { halt("Timer init failed"); return; }
    Error error = storage.begin();
    if (error != Error::Ok) { check(error); return; }
    RecoveryReport recovered{};
    error = engine.recover(recovered);
    if (error == Error::NoJournal) {
        std::array<Credits, kSeatCount> funding{};
        funding[0] = 1000;
        error = engine.initialize(EngineConfig{}, funding);
        notice("Hold L: reset");
    } else if (error == Error::Ok) {
        switch (recovered.action) {
        case RecoveryAction::Refunded: notice("Recovered: refunded"); break;
        case RecoveryAction::PayoutCompleted: notice("Recovered: paid"); break;
        case RecoveryAction::AlreadyPaid: notice("Already paid"); break;
        case RecoveryAction::None: notice("Journal restored"); break;
        }
        send(message);
    }
    if (error != Error::Ok) { check(error); return; }
    ready = true;
    entered = millis();
    publish_status();
}
} // namespace

void setup() {
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    send("boot=esp32 protocol=1");
    for (int pin : {bet_pin, launch_pin, fault_pin}) pinMode(pin, INPUT_PULLUP);
    for (int pin : {open_led, spin_led, fault_led}) pinMode(pin, OUTPUT);
    const auto now = millis();
    bets.begin(digitalRead(bet_pin) == LOW, now);
    launch.begin(digitalRead(launch_pin) == LOW, now);
    fault.begin(digitalRead(fault_pin) == LOW, now);
    display_ready = begin_panel();
    boot();
}

void loop() {
    const TickMs now = millis();
    const Gesture b = bets.sample(digitalRead(bet_pin) == LOW, now);
    const Gesture l = launch.sample(digitalRead(launch_pin) == LOW, now, true);
    const Gesture f = fault.sample(digitalRead(fault_pin) == LOW, now);
    if (l == Gesture::Hold) ESP.restart(); // Simulate pressing EN without reloading flash.
    if (!ready) {
        digitalWrite(open_led, LOW);
        digitalWrite(spin_led, LOW);
        digitalWrite(fault_led, HIGH);
        if (dirty && display_ready) draw_panel(nullptr, 0, message);
        dirty = false;
        return;
    }
    const RoundState before = engine.snapshot().round.state;
    if (f == Gesture::Tap && (before == RoundState::BetsOpen || before == RoundState::BetsClosing ||
                              before == RoundState::BetsClosed || before == RoundState::Spinning) && wheel.arm_fault()) {
        fault_armed = true;
        notice("Bad pulse armed");
        send("inject=missing_pocket");
    }
    if (b == Gesture::Tap) press_bets(before);
    if (l == Gesture::Tap && before == RoundState::BetsClosed) check(engine.start_spin(now));
    if (before == RoundState::BetsClosing && now - entered >= 700) check(engine.close_bets());
    if (before == RoundState::Spinning) check(engine.tick(now));
    if (before == RoundState::Result && now - entered >= 3200) check(engine.settle());

    if (!ready) return; // A failed commit pauses the panel on the next loop.
    const auto& s = engine.snapshot();
    const RoundState state = s.round.state;
    if (state != before) {
        entered = millis();
        if (state == RoundState::Fault || state == RoundState::Result) fault_armed = false;
        transition_notice(state);
        publish_status();
    }
    digitalWrite(open_led, state == RoundState::BetsOpen);
    digitalWrite(spin_led, state == RoundState::Spinning);
    digitalWrite(fault_led, state == RoundState::Fault);
    const TickMs paint = millis();
    const bool moving = state == RoundState::Spinning || state == RoundState::Result;
    if (display_ready && (dirty || (moving && paint - last_draw >= 120))) {
        draw_panel(&s, paint - entered, message);
        dirty = false;
        last_draw = paint;
    }
}
