#include "catalog.hpp"
#include "mini_etg/wheel.hpp"

#include <algorithm>
#include <initializer_list>
#include <sstream>

namespace mini_etg::web {

const char* kind_name(BetKind kind) {
    constexpr const char* names[] = {"Straight", "Split", "Street", "Corner", "SixLine",
        "Dozen", "Column", "Red", "Black", "Odd", "Even", "Low", "High"};
    return names[static_cast<unsigned>(kind)];
}

std::string selection_label(const BetSelection& selection) {
    const auto kind = selection.kind;
    if (kind == BetKind::Dozen)
        return "Dozen " + std::to_string(1 + (selection.numbers[0].value - 1) / 12);
    if (kind == BetKind::Column)
        return "Column " + std::to_string(selection.numbers[0].value);
    if (kind >= BetKind::Red) return kind_name(kind);
    std::string label = kind == BetKind::SixLine ? "Six line" : kind_name(kind);
    for (unsigned i = 0; i < selection.count; ++i) {
        label += (i == 0 ? " " : "/") + std::to_string(selection.numbers[i].value);
    }
    return label;
}

Catalog::Catalog() {
    // Enumerate candidate touches on the felt. Each selection is still built and
    // validated by the same core functions used by native callers.
    auto inside = [this](BetKind kind, std::initializer_list<int> values) {
        std::array<Number, 6> numbers{};
        std::size_t count = 0;
        for (int n : values) numbers[count++] = Number{static_cast<std::uint8_t>(n)};
        BetSelection selection{};
        if (make_inside_bet(kind, numbers.data(), count, selection) == Error::Ok)
            selections_.push_back(selection);
    };
    for (int n = 0; n <= 36; ++n) inside(BetKind::Straight, {n});
    for (int n = 1; n <= 3; ++n) inside(BetKind::Split, {0, n});
    inside(BetKind::Street, {0, 1, 2});
    inside(BetKind::Street, {0, 2, 3});
    inside(BetKind::Corner, {0, 1, 2, 3});
    for (int n = 1; n <= 36; ++n) {
        if (n % 3 != 0) inside(BetKind::Split, {n, n + 1});
        if (n <= 33) inside(BetKind::Split, {n, n + 3});
        if (n <= 32 && n % 3 != 0) inside(BetKind::Corner, {n, n + 1, n + 3, n + 4});
        if (n % 3 == 1) {
            inside(BetKind::Street, {n, n + 1, n + 2});
            if (n <= 31) inside(BetKind::SixLine, {n, n + 1, n + 2, n + 3, n + 4, n + 5});
        }
    }
    BetSelection selection{};
    for (std::uint8_t group = 1; group <= 3; ++group) {
        if (make_dozen(group, selection) == Error::Ok) selections_.push_back(selection);
        if (make_column(group, selection) == Error::Ok) selections_.push_back(selection);
    }
    for (auto kind : {BetKind::Red, BetKind::Black, BetKind::Odd, BetKind::Even, BetKind::Low, BetKind::High}) {
        if (make_even_money_bet(kind, selection) == Error::Ok) selections_.push_back(selection);
    }
}

const BetSelection* Catalog::at(unsigned id) const {
    return id < selections_.size() ? &selections_[id] : nullptr;
}

int Catalog::id_for(const BetSelection& selection) const {
    for (std::size_t i = 0; i < selections_.size(); ++i) {
        const auto& candidate = selections_[i];
        if (candidate.kind == selection.kind && candidate.count == selection.count &&
            std::equal(candidate.numbers.begin(), candidate.numbers.begin() + candidate.count,
                       selection.numbers.begin(), [](Number a, Number b) { return a.value == b.value; }))
            return static_cast<int>(i);
    }
    return -1;
}

std::string Catalog::json() const {
    std::ostringstream out;
    out << R"({"selections":[)";
    for (std::size_t i = 0; i < selections_.size(); ++i) {
        const auto& s = selections_[i];
        if (i) out << ',';
        out << R"({"id":)" << i << R"(,"kind":")" << kind_name(s.kind)
            << R"(","label":")" << selection_label(s) << R"(","numbers":[)";
        for (unsigned j = 0; j < s.count; ++j) {
            if (j) out << ',';
            out << unsigned(s.numbers[j].value);
        }
        out << "]}";
    }
    out << R"(],"wheel":[)";
    for (std::size_t i = 0; i < kPocketCount; ++i) {
        if (i) out << ',';
        Number n = kEuropeanWheelOrder[i];
        Colour colour{};
        colour_of(n, colour);
        const char* name = colour == Colour::Red ? "red" : colour == Colour::Black ? "black" : "green";
        out << R"({"number":)" << unsigned(n.value) << R"(,"colour":")" << name << R"("})";
    }
    out << "]}";
    return out.str();
}

} // namespace mini_etg::web
