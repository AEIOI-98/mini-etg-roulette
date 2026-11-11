// Generate deterministic journal bytes at credit values unavailable on the
// browser's default 1,000-credit seat. The production WASM reads this fixture.
#include "journal_codec.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <string>

using namespace mini_etg;
namespace wire = mini_etg::web::wire;

namespace {
wire::Bytes funded() {
    constexpr Credits stake = (Credits{1} << 53) + 1;
    Snapshot snapshot{};
    snapshot.revision = 1;
    snapshot.last_sequence = 2;
    snapshot.config.limits.max_per_bet = stake;
    snapshot.config.limits.max_total_per_seat = stake;
    snapshot.seats[0].credits = stake + 10;
    snapshot.seats[0].meters.credits_in = stake + 10;

    wire::Bytes bytes;
    wire::Writer writer{bytes};
    std::uint32_t magic = 0x31475445;
    writer.scalar(magic);
    wire::snapshot(writer, snapshot);
    Event event{};
    event.sequence = event.revision = 1;
    wire::event(writer, event);
    event.sequence = 2;
    event.kind = EventKind::CreditLoaded;
    event.amount = stake + 10;
    wire::event(writer, event);
    return bytes;
}

// Change one real event in a WASM-produced journal, keeping every other byte
// including the terminal reserve intact. The checksum is recomputed below.
bool corrupt_event(wire::Bytes& bytes, const std::string& mode) {
    if (bytes.size() < 8) return false;
    bytes.resize(bytes.size() - 4);
    wire::Reader reader{bytes.data(), bytes.size()};
    std::uint32_t tag{};
    Snapshot snapshot{};
    reader.scalar(tag);
    wire::snapshot(reader, snapshot);
    for (Sequence i = 0; reader.ok && i < snapshot.last_sequence; ++i) {
        auto offset = reader.data - bytes.data();
        Event event{};
        wire::event(reader, event);
        if (!reader.ok) return false;
        if (mode == "bad-kind" && event.kind == EventKind::BetAccepted)
            event.selection.kind = static_cast<BetKind>(255);
        else if (mode == "negative-return" && event.kind == EventKind::BetSettled)
            event.amount = -1;
        else continue;
        wire::Bytes replacement;
        wire::Writer writer{replacement};
        wire::event(writer, event);
        std::copy(replacement.begin(), replacement.end(), bytes.begin() + offset);
        return true;
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::string mode = argv[1];
    wire::Bytes bytes;
    if (mode == "funded") bytes = funded();
    else {
        bytes.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        if (!corrupt_event(bytes, mode)) return 1;
    }
    wire::Writer writer{bytes};
    auto hash = wire::checksum(bytes, bytes.size());
    writer.scalar(hash);
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return std::cout ? 0 : 1;
}
