#pragma once
#include "journal_codec.hpp"

namespace mini_etg::web {
class BrowserStorage final : public IStorage {
public:
    BrowserStorage();
    Error load_latest(Snapshot& out) noexcept override;
    Error read_event(Sequence sequence, Event& out) noexcept override;
    Error commit(const JournalCommit& transaction) noexcept override;

private:
    Error restore();
    Error state_{Error::NoJournal};
    Snapshot latest_{};
    wire::Bytes history_;
    std::size_t event_bytes_{};
};
} // namespace mini_etg::web
