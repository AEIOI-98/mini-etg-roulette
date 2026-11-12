#pragma once
#include "mini_etg/storage.hpp"
#include <array>

namespace mini_etg::firmware {

// Each replace is one durable NVS key replacement. The native fault injector
// implements the same boundary and can lose the acknowledgement after a write.
class BlobStore {
public:
    virtual ~BlobStore() = default;
    virtual Error get(const char* key, std::uint8_t* out, std::size_t capacity,
                      std::size_t& length) noexcept = 0;
    virtual Error put(const char* key, const std::uint8_t* data, std::size_t length) noexcept = 0;
};

class NvsJournal final : public IStorage {
public:
    static constexpr std::size_t max_events = 1024;
    explicit NvsJournal(BlobStore& store, std::size_t capacity = max_events) noexcept;

    NvsJournal(const NvsJournal&) = delete;
    NvsJournal& operator=(const NvsJournal&) = delete;

    Error load_latest(Snapshot& out) noexcept override;
    Error read_event(Sequence sequence, Event& out) noexcept override;
    Error commit(const JournalCommit& transaction) noexcept override;

private:
    // Complete 16-event blocks are archived once. 'head' contains the exact
    // snapshot and its unfinished block, and is always the last key replaced.
    static constexpr std::size_t per_block = 16;
    static constexpr std::size_t event_bytes = 86; // Explicit journal_wire V1 layout.
    static constexpr std::size_t block_bytes = per_block * event_bytes;
    Error read_block(std::size_t index) noexcept;

    BlobStore& store_;
    const std::size_t capacity_;
    Error state_{Error::NotReady};
    Revision revision_{0};
    Sequence last_{0};
    // Dedicated storage keeps these buffers off the ESP32 loop task's stack.
    Snapshot scratch_{};
    std::array<std::uint8_t, 16 * 1024> head_{};
    std::array<std::uint8_t, block_bytes> tail_{};
    std::array<std::uint8_t, block_bytes + 4> block_{};
};

} // namespace mini_etg::firmware
