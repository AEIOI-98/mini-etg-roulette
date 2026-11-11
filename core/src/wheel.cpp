#include "mini_etg/wheel.hpp"

namespace mini_etg {

const std::array<Number, kPocketCount> kEuropeanWheelOrder = {{
    {0}, {32}, {15}, {19}, {4}, {21}, {2}, {25}, {17}, {34}, {6}, {27}, {13},
    {36}, {11}, {30}, {8}, {23}, {10}, {5}, {24}, {16}, {33}, {1}, {20}, {14},
    {31}, {9}, {22}, {18}, {29}, {7}, {28}, {12}, {35}, {3}, {26}
}};

Error number_at(PocketIndex index, Number& out) noexcept {
    if (index.value >= kPocketCount) {
        return Error::InvalidNumber;
    }
    out = kEuropeanWheelOrder[index.value];
    return Error::Ok;
}

Error colour_of(Number number, Colour& out) noexcept {
    if (number.value >= kPocketCount) {
        return Error::InvalidNumber;
    }
    if (number.value == 0) {
        out = Colour::Green;
        return Error::Ok;
    }

    constexpr std::array<std::uint8_t, 18> red_numbers = {{
        1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36
    }};
    for (const auto red : red_numbers) {
        if (number.value == red) {
            out = Colour::Red;
            return Error::Ok;
        }
    }
    out = Colour::Black;
    return Error::Ok;
}

} // namespace mini_etg
