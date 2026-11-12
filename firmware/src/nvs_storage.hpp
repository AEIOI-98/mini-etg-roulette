#pragma once
#include "nvs_journal.hpp"
#include <nvs.h>

namespace mini_etg::firmware {
class NvsBlobs final : public BlobStore {
public:
    NvsBlobs() = default;
    ~NvsBlobs() override { if (handle_) nvs_close(handle_); }
    NvsBlobs(const NvsBlobs&) = delete;
    NvsBlobs& operator=(const NvsBlobs&) = delete;
    Error begin() noexcept;
    Error get(const char* key, std::uint8_t* out, std::size_t capacity,
              std::size_t& length) noexcept override;
    Error put(const char* key, const std::uint8_t* data, std::size_t length) noexcept override;
private:
    nvs_handle_t handle_{};
};

class NvsStorage final : public IStorage {
public:
    Error begin() noexcept { return blobs_.begin(); }
    Error load_latest(Snapshot& out) noexcept override { return journal_.load_latest(out); }
    Error read_event(Sequence seq, Event& out) noexcept override { return journal_.read_event(seq, out); }
    Error commit(const JournalCommit& tx) noexcept override { return journal_.commit(tx); }
private:
    NvsBlobs blobs_{};
    NvsJournal journal_{blobs_};
};
} // namespace mini_etg::firmware
