#pragma once

#include "mini_etg/random.hpp"

#include <random>

namespace mini_etg::test {

// The standardized generator's state is stored inline; bounded mapping is ours.
class SeededRandom final : public IRandom {
public:
    explicit SeededRandom(std::uint32_t seed) noexcept : generator_(seed) {}

    std::uint32_t next_u32() noexcept override {
        return static_cast<std::uint32_t>(generator_());
    }

private:
    std::mt19937 generator_;
};

} // namespace mini_etg::test
