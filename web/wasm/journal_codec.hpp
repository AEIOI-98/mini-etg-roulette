// Version 1 wire format. Fixed-width little-endian fields avoid compiler padding,
// pointer widths, enum layout and lossy JavaScript number conversions.
#pragma once

#include "mini_etg/journal_wire.hpp"
#include <vector>

namespace mini_etg::web::wire {
using journal_wire::Reader;
using journal_wire::event;
using journal_wire::snapshot;
using Bytes = std::vector<std::uint8_t>;

struct Writer {
    Bytes& bytes;
    template<class T> void scalar(T& value) {
        std::uint64_t bits = static_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes.push_back(static_cast<std::uint8_t>(bits));
            bits >>= 8;
        }
    }
};

inline std::uint32_t checksum(const Bytes& bytes, std::size_t count) {
    return journal_wire::checksum(bytes.data(), count);
}
} // namespace mini_etg::web::wire
