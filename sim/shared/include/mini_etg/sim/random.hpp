#pragma once

#include "mini_etg/random.hpp"

#include <random>

namespace mini_etg::sim {

// The generator's fixed state lives inside the adapter. Bounded choices still
// go through the core's uniform_below(), rather than a library distribution.
class SeededRandom final : public IRandom {
public:
    explicit SeededRandom(std::uint32_t seed) noexcept : generator_(seed) {}

    std::uint32_t next_u32() noexcept override {
        return static_cast<std::uint32_t>(generator_());
    }

private:
    std::mt19937 generator_;
};

} // namespace mini_etg::sim
