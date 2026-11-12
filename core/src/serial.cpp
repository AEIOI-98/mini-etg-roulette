#include "mini_etg/serial.hpp"

#include <algorithm>

namespace mini_etg::serial {
namespace {

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0] | (std::uint16_t{bytes[1]} << 8));
}

void write_u16(std::uint16_t value, std::uint8_t* bytes) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

} // namespace

std::uint16_t crc16(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint16_t crc = 0xffff;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i] << 8);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool high_bit = (crc & 0x8000) != 0;
            crc = static_cast<std::uint16_t>(crc << 1);
            if (high_bit) crc ^= 0x1021;
        }
    }
    return crc;
}

bool encode(std::uint16_t sequence, const std::uint8_t* payload,
            std::size_t size, EncodedFrame& out) noexcept {
    if (size > kMaxPayload || (size != 0 && payload == nullptr)) return false;
    std::array<std::uint8_t, kMaxBody> body{};
    body[0] = static_cast<std::uint8_t>(size);
    write_u16(sequence, body.data() + 1);
    if (size != 0) std::copy_n(payload, size, body.data() + kHeaderBytes);
    const auto content_size = kHeaderBytes + size;
    write_u16(crc16(body.data(), content_size), body.data() + content_size);

    out.bytes[0] = kStart;
    out.size = 1;
    for (std::size_t i = 0; i < content_size + 2; ++i) {
        const auto byte = body[i];
        if (byte == kStart || byte == kEscape) {
            out.bytes[out.size++] = kEscape;
            out.bytes[out.size++] = byte ^ 0x20;
        } else {
            out.bytes[out.size++] = byte;
        }
    }
    return true;
}

DecodeStatus Decoder::feed(std::uint8_t byte, Frame& out) noexcept {
    if (byte == kStart) {
        const auto status = finish();
        receiving_ = true;
        return status;
    }
    if (!receiving_) return DecodeStatus::Pending;
    if (escaped_) {
        if (byte != (kStart ^ 0x20) && byte != (kEscape ^ 0x20)) {
            finish();
            return DecodeStatus::BadEscape;
        }
        byte ^= 0x20;
        escaped_ = false;
    } else if (byte == kEscape) {
        escaped_ = true;
        return DecodeStatus::Pending;
    }

    if (used_ == 0 && byte > kMaxPayload) {
        finish();
        return DecodeStatus::BadLength;
    }
    body_[used_++] = byte;
    const auto content_size = kHeaderBytes + body_[0];
    if (used_ < content_size + 2) return DecodeStatus::Pending;

    finish();
    if (crc16(body_.data(), content_size) != read_u16(body_.data() + content_size)) {
        return DecodeStatus::BadCrc;
    }
    out.sequence = read_u16(body_.data() + 1);
    out.size = body_[0];
    std::copy_n(body_.data() + kHeaderBytes, out.size, out.payload.begin());
    return DecodeStatus::Ready;
}

DecodeStatus Decoder::finish() noexcept {
    const auto status = receiving_ ? DecodeStatus::Truncated : DecodeStatus::Pending;
    receiving_ = false;
    escaped_ = false;
    used_ = 0;
    return status;
}

} // namespace mini_etg::serial
