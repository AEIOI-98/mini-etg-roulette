#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mini_etg::serial {

inline constexpr std::uint8_t kStart = 0x7e;
inline constexpr std::uint8_t kEscape = 0x7d;
inline constexpr std::size_t kMaxPayload = 96;
inline constexpr std::size_t kHeaderBytes = 3; // length, sequence low, sequence high
inline constexpr std::size_t kMaxBody = kHeaderBytes + kMaxPayload + 2;

struct Frame {
    std::uint16_t sequence{0};
    std::array<std::uint8_t, kMaxPayload> payload{};
    std::size_t size{0};
};

struct EncodedFrame {
    std::array<std::uint8_t, 1 + 2 * kMaxBody> bytes{};
    std::size_t size{0};
};

// CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xffff, no reflection/final XOR.
// data must contain size bytes. nullptr is valid when size == 0.
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) noexcept;

// A false return (oversized or missing payload) leaves out untouched.
bool encode(std::uint16_t sequence, const std::uint8_t* payload,
            std::size_t size, EncodedFrame& out) noexcept;

enum class DecodeStatus : std::uint8_t { Pending, Ready, BadLength, BadEscape, BadCrc, Truncated };

class Decoder {
public:
    // Call once per UART byte, regardless of read/chunk boundaries. A new marker
    // both reports an interrupted frame and starts collecting its replacement.
    // Only Ready updates out.
    DecodeStatus feed(std::uint8_t byte, Frame& out) noexcept;

    // Use on EOF, disconnection or application timeout; a short read is not EOF.
    DecodeStatus finish() noexcept;

private:
    std::array<std::uint8_t, kMaxBody> body_{};
    std::size_t used_{0};
    bool receiving_{false};
    bool escaped_{false};
};

} // namespace mini_etg::serial
