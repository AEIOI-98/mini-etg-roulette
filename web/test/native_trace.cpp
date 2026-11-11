// Feed a timestamped command trace to the browser session running as native C++.
// A session recreation shares only the journal, exactly like a browser reload.
#include "session.hpp"
#include "mini_etg/sim/journal.hpp"

#include <iostream>
#include <sstream>

using namespace mini_etg;

int main() {
    std::vector<Event> events(8192);
    auto storage = std::make_shared<sim::MemoryJournal>(events.data(), events.size());
    std::unique_ptr<web::Session> session;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command, answer;
        TickMs now{};
        if (!(input >> command >> now)) return 1;
        if (command == "start") {
            std::uint32_t seed{};
            if (!(input >> seed)) return 1;
            session = std::make_unique<web::Session>(storage, seed);
            answer = session->start(now);
        } else if (!session) return 1;
        else if (command == "tick") answer = session->tick(now);
        else if (command == "place") {
            unsigned id{};
            Credits stake{};
            if (!(input >> id >> stake)) return 1;
            answer = session->place(id, stake, now);
        } else if (command == "undo") answer = session->undo(now);
        else if (command == "clear") answer = session->clear(now);
        else if (command == "repeat") answer = session->repeat(now);
        else if (command == "fault") {
            std::string name;
            if (!(input >> name)) return 1;
            answer = session->arm_fault(name, now);
        } else return 1;
        std::cout << answer << '\t' << session->view() << '\n';
    }
}
