#pragma once

#include "mini_etg/types.hpp"

namespace mini_etg {

class IRandom {
public:
    virtual ~IRandom() = default;

    // Adapter contract: uniformly distributed words over the full uint32 range.
    virtual std::uint32_t next_u32() noexcept = 0;
};

// Rejection sampling; upper_exclusive must be nonzero. No distribution object.
// On error, out is unchanged. A broken RNG that never accepts is out of contract.
Error uniform_below(IRandom& random,
                    std::uint32_t upper_exclusive,
                    std::uint32_t& out) noexcept;

} // namespace mini_etg
