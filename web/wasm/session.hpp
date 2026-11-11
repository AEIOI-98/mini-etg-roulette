#pragma once

#include "catalog.hpp"
#include "history.hpp"
#include "mini_etg/engine.hpp"
#include "mini_etg/sim/random.hpp"
#include "mini_etg/sim/wheel.hpp"

#include <memory>

namespace mini_etg::web {

// The clock and input are supplied by the host. All transitions, player
// commands, seven bots and sensor faults go through the C++ engine.
class Session {
public:
    Session(std::shared_ptr<IStorage> storage, std::uint32_t seed);
    std::string start(TickMs now);
    std::string tick(TickMs now);
    std::string place(unsigned selection_id, Credits stake, TickMs now);
    std::string undo(TickMs now);
    std::string clear(TickMs now);
    std::string repeat(TickMs now);
    std::string arm_fault(const std::string& name, TickMs now);

    TickMs remaining_ms(TickMs now) const;
    Revision revision() const { return engine_.snapshot().revision; }
    std::string catalog() const { return catalog_.json(); }
    std::string view() const;

private:
    Error open_with_bots();
    std::string after(Error error);
    std::shared_ptr<IStorage> storage_;
    sim::SeededRandom wheel_random_, lucky_random_, bot_random_;
    sim::SimulatedWheel wheel_;
    Engine engine_;
    Catalog catalog_;
    History history_;
    bool started_{false};
    TickMs entered_at_{0};
    std::string recovery_, armed_fault_;
};

} // namespace mini_etg::web
