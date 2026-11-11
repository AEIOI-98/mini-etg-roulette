#include "mini_etg/random.hpp"

namespace mini_etg {

Error uniform_below(IRandom& random, std::uint32_t upper_exclusive,
                    std::uint32_t& out) noexcept {
    if (upper_exclusive == 0) {
        return Error::InvalidBound;
    }

    // Unsigned subtraction wraps deliberately. The result is 2^32 % bound.
    // Dropping [0, threshold) leaves a multiple of bound equally likely words.
    const std::uint32_t threshold = (std::uint32_t{0} - upper_exclusive)
                                   % upper_exclusive;
    std::uint32_t word = 0;
    do {
        word = random.next_u32();
    } while (word < threshold);

    out = word % upper_exclusive;
    return Error::Ok;
}

} // namespace mini_etg
