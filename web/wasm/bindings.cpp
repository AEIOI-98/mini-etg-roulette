#include "browser_storage.hpp"
#include "session.hpp"

#include <emscripten/bind.h>

namespace mini_etg::web {
namespace {
Session* create_session(std::uint32_t seed) {
    return new Session(std::make_shared<BrowserStorage>(), seed);
}
} // namespace

EMSCRIPTEN_BINDINGS(roulette_table) {
    using namespace emscripten;
    class_<Session>("BrowserSession")
        .constructor(&create_session, allow_raw_pointers())
        .function("start", &Session::start)
        .function("tick", &Session::tick)
        .function("place", &Session::place)
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
