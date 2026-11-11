#include "browser_storage.hpp"
#include "session.hpp"

#include <emscripten/bind.h>
#include <limits>

namespace mini_etg::web {
namespace {
Session* create_session(std::uint32_t seed) {
    return new Session(std::make_shared<BrowserStorage>(), seed);
}
// A release WebAssembly call narrows its integer arguments before C++ sees
// them. Validate the original JS values so 2^64 + 1 cannot place a one-credit
// wager and 2^32 cannot silently select position zero.
std::string place_from_js(Session& session, const emscripten::val& position,
                          const emscripten::val& stake, TickMs now) {
    using emscripten::val;
    const auto invalid_id = std::numeric_limits<unsigned>::max();
    unsigned id = invalid_id;
    if (val::global("Number").call<bool>("isSafeInteger", position)) {
        const double index = position.as<double>();
        if (index >= 0 && index < invalid_id) id = static_cast<unsigned>(index);
    }
    Credits amount = 0;
    if (stake.typeOf().strictlyEquals(val("bigint")) &&
        stake.strictlyEquals(val::global("BigInt").call<val>("asIntN", 64, stake)))
        amount = stake.as<Credits>();
    // Invalid inputs still go through the engine and produce a rejection event.
    return session.place(id, amount, now);
}
} // namespace

EMSCRIPTEN_BINDINGS(roulette_table) {
    using namespace emscripten;
    class_<Session>("BrowserSession")
        .constructor(&create_session, allow_raw_pointers())
        .function("start", &Session::start)
        .function("tick", &Session::tick)
        .function("place", &place_from_js)
        .function("undo", &Session::undo)
        .function("clear", &Session::clear)
        .function("repeat", &Session::repeat)
        .function("armFault", &Session::arm_fault)
        .function("remainingMs", &Session::remaining_ms)
        .function("revision", &Session::revision)
        .function("catalog", &Session::catalog)
        .function("view", &Session::view);
}

} // namespace mini_etg::web
