#pragma once

#include "mini_etg/bet.hpp"

#include <string>
#include <vector>

namespace mini_etg::web {

const char* kind_name(BetKind kind);
std::string selection_label(const BetSelection& selection);

class Catalog {
public:
    Catalog();
    const BetSelection* at(unsigned id) const;
    int id_for(const BetSelection& selection) const;
    std::string json() const;

private:
    std::vector<BetSelection> selections_;
};

} // namespace mini_etg::web
