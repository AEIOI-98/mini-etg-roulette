#pragma once
#include <cstdint>

namespace mini_etg::firmware {
enum class Gesture { None, Tap, Hold };

// Releasing a debounced switch generates one tap. A long press generates
// one hold instead. A switch held during boot must be released before use.
class Button {
public:
    void begin(bool pressed, std::uint32_t now) {
        raw_ = stable_ = suppress_ = pressed;
        changed_ = down_ = now;
    }

    Gesture sample(bool pressed, std::uint32_t now, bool detect_hold = false) {
        if (pressed != raw_) {
            raw_ = pressed;
            changed_ = now;
        }
        if (raw_ != stable_ && now - changed_ >= 25) {
            stable_ = raw_;
            if (stable_) {
                down_ = now;
            } else {
                const bool tap = !suppress_;
                suppress_ = false;
                return tap ? Gesture::Tap : Gesture::None;
            }
        }
        if (detect_hold && stable_ && raw_ && !suppress_ && now - down_ >= 1200) {
            suppress_ = true;
            return Gesture::Hold;
        }
        return Gesture::None;
    }

private:
    bool raw_{}, stable_{}, suppress_{};
    std::uint32_t changed_{}, down_{};
};
} // namespace mini_etg::firmware
