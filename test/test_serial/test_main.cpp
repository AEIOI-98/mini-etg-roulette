#include <unity.h>
#include "mini_etg/serial.hpp"

#include <algorithm>
#include <array>
#include <cstring>

using namespace mini_etg::serial;

void setUp() {}
void tearDown() {}

namespace {

void expect(DecodeStatus expected, DecodeStatus actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

DecodeStatus consume(Decoder& decoder, const std::uint8_t* bytes, std::size_t size, Frame& out) {
    DecodeStatus status = DecodeStatus::Pending;
    for (std::size_t i = 0; i < size; ++i) {
        expect(DecodeStatus::Pending, status);
        status = decoder.feed(bytes[i], out);
    }
    return status;
}

constexpr std::array<std::uint8_t, 11> kHello{{
    0x7e, 0x05, 0x34, 0x12, 'H', 'e', 'l', 'l', 'o', 0xd9, 0xbb
}};

void assert_hello(const Frame& out) {
    TEST_ASSERT_EQUAL_UINT16(0x1234, out.sequence);
    TEST_ASSERT_EQUAL_UINT(5, out.size);
    TEST_ASSERT_EQUAL_MEMORY("Hello", out.payload.data(), out.size);
}

void test_crc_reference_and_wire_byte_order() {
    const auto* digits = reinterpret_cast<const std::uint8_t*>("123456789");
    TEST_ASSERT_EQUAL_HEX16(0x29b1, crc16(digits, 9));
    TEST_ASSERT_EQUAL_HEX16(0xffff, crc16(nullptr, 0));
    EncodedFrame wire{};
    TEST_ASSERT_TRUE(encode(0x1234, kHello.data() + 4, 5, wire));
    TEST_ASSERT_EQUAL_UINT(kHello.size(), wire.size);
    TEST_ASSERT_EQUAL_MEMORY(kHello.data(), wire.bytes.data(), wire.size);
    Decoder decoder;
    Frame out{};
    expect(DecodeStatus::Ready, consume(decoder, kHello.data(), kHello.size(), out));
    assert_hello(out);
    expect(DecodeStatus::Pending, decoder.finish());
}

void test_empty_frame_and_invalid_encode_preserves_output() {
    const std::uint8_t empty[]{kStart, 0, 0, 0, 0x9c, 0xcc};
    EncodedFrame wire{};
    TEST_ASSERT_TRUE(encode(0, nullptr, 0, wire));
    TEST_ASSERT_EQUAL_UINT(sizeof(empty), wire.size);
    TEST_ASSERT_EQUAL_MEMORY(empty, wire.bytes.data(), wire.size);
    const EncodedFrame before = wire;
    TEST_ASSERT_FALSE(encode(0, nullptr, 1, wire));
    TEST_ASSERT_FALSE(encode(0, empty, kMaxPayload + 1, wire));
    TEST_ASSERT_EQUAL_UINT(before.size, wire.size);
    TEST_ASSERT_EQUAL_MEMORY(before.bytes.data(), wire.bytes.data(), before.bytes.size());
    Frame out{};
    Decoder decoder;
    expect(DecodeStatus::Ready, consume(decoder, empty, sizeof(empty), out));
    TEST_ASSERT_EQUAL_UINT(0, out.size);
    TEST_ASSERT_EQUAL_UINT16(0, out.sequence);
}

void test_escapes_sequence_payload_and_crc_and_accepts_max_length() {
    const std::uint8_t special[]{kStart, kEscape};
    const std::uint8_t golden[]{kStart, 2, kEscape, 0x5e, kEscape, 0x5d,
                                kEscape, 0x5e, kEscape, 0x5d, 0x95, 0xab};
    EncodedFrame wire{};
    TEST_ASSERT_TRUE(encode(0x7d7e, special, sizeof(special), wire));
    TEST_ASSERT_EQUAL_UINT(sizeof(golden), wire.size);
    TEST_ASSERT_EQUAL_MEMORY(golden, wire.bytes.data(), wire.size);
    Decoder decoder;
    Frame out{};
    expect(DecodeStatus::Ready, consume(decoder, golden, sizeof(golden), out));
    TEST_ASSERT_EQUAL_UINT16(0x7d7e, out.sequence);
    TEST_ASSERT_EQUAL_MEMORY(special, out.payload.data(), sizeof(special));

    std::array<std::uint8_t, kMaxPayload> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = special[i % 2];
    for (std::uint16_t sequence = 0; sequence < 256; ++sequence) {
        TEST_ASSERT_TRUE(encode(sequence, payload.data(), payload.size(), wire));
        TEST_ASSERT_TRUE(wire.size <= wire.bytes.size());
        expect(DecodeStatus::Ready, consume(decoder, wire.bytes.data(), wire.size, out));
        TEST_ASSERT_EQUAL_UINT16(sequence, out.sequence);
        TEST_ASSERT_EQUAL_UINT(kMaxPayload, out.size);
        TEST_ASSERT_EQUAL_MEMORY(payload.data(), out.payload.data(), payload.size());
    }
}

void test_every_split_and_consecutive_frames() {
    for (std::size_t split = 1; split < kHello.size(); ++split) {
        Decoder decoder;
        Frame out{};
        expect(DecodeStatus::Pending, consume(decoder, kHello.data(), split, out));
        expect(DecodeStatus::Ready, consume(decoder, kHello.data() + split, kHello.size() - split, out));
        assert_hello(out);
        expect(DecodeStatus::Ready, consume(decoder, kHello.data(), kHello.size(), out));
        assert_hello(out);
    }
}

void test_corruption_rejects_unchanged_output_then_resynchronizes() {
    for (std::size_t i = 2; i < kHello.size(); ++i) {
        auto corrupt = kHello;
        corrupt[i] ^= 1; // One bit in sequence, payload or CRC.
        Decoder decoder;
        Frame out{};
        out.sequence = 778;
        out.size = 1;
        out.payload[0] = 0xa1;
        expect(DecodeStatus::BadCrc, consume(decoder, corrupt.data(), corrupt.size(), out));
        TEST_ASSERT_EQUAL_UINT16(778, out.sequence);
        TEST_ASSERT_EQUAL_UINT(1, out.size);
        TEST_ASSERT_EQUAL_HEX8(0xa1, out.payload[0]);
        expect(DecodeStatus::Ready, consume(decoder, kHello.data(), kHello.size(), out));
        assert_hello(out);
    }
}

void test_all_truncated_prefixes_at_eof_or_new_marker() {
    for (std::size_t cut = 1; cut < kHello.size(); ++cut) {
        Decoder decoder;
        Frame out{};
        expect(DecodeStatus::Pending, consume(decoder, kHello.data(), cut, out));
        expect(DecodeStatus::Truncated, decoder.finish());
        expect(DecodeStatus::Pending, decoder.finish());
        expect(DecodeStatus::Pending, consume(decoder, kHello.data(), cut, out));
        expect(DecodeStatus::Truncated, decoder.feed(kStart, out));
        expect(DecodeStatus::Ready, consume(decoder, kHello.data() + 1, kHello.size() - 1, out));
        assert_hello(out);
    }
}

void test_boot_noise_invalid_lengths_and_broken_escapes() {
    Decoder decoder;
    Frame out{};
    const std::uint8_t noise[]{'E','S','P','3','2',0,'\n',kEscape,0xff};
    expect(DecodeStatus::Pending, consume(decoder, noise, sizeof(noise), out));
    const std::uint8_t oversize[]{kStart, kMaxPayload + 1};
    expect(DecodeStatus::BadLength, consume(decoder, oversize, sizeof(oversize), out));
    const std::uint8_t invalid[]{kStart, kEscape, 'A'};
    expect(DecodeStatus::BadEscape, consume(decoder, invalid, sizeof(invalid), out));
    const std::uint8_t interrupted[]{kStart, 5, kEscape};
    expect(DecodeStatus::Pending, consume(decoder, interrupted, sizeof(interrupted), out));
    expect(DecodeStatus::Truncated, decoder.feed(kStart, out));
    expect(DecodeStatus::Ready, consume(decoder, kHello.data() + 1, kHello.size() - 1, out));
    assert_hello(out);
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc_reference_and_wire_byte_order);
    RUN_TEST(test_empty_frame_and_invalid_encode_preserves_output);
    RUN_TEST(test_escapes_sequence_payload_and_crc_and_accepts_max_length);
    RUN_TEST(test_every_split_and_consecutive_frames);
    RUN_TEST(test_corruption_rejects_unchanged_output_then_resynchronizes);
    RUN_TEST(test_all_truncated_prefixes_at_eof_or_new_marker);
    RUN_TEST(test_boot_noise_invalid_lengths_and_broken_escapes);
    return UNITY_END();
}
