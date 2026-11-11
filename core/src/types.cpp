#include "mini_etg/types.hpp"

#include <limits>

namespace mini_etg {

Error checked_add(Credits a, Credits b, Credits& out) noexcept {
    const Credits minimum = std::numeric_limits<Credits>::min();
    const Credits maximum = std::numeric_limits<Credits>::max();
    if ((b > 0 && a > maximum - b) || (b < 0 && a < minimum - b)) {
        return Error::ArithmeticOverflow;
    }
    out = a + b;
    return Error::Ok;
}

Error checked_subtract(Credits a, Credits b, Credits& out) noexcept {
    const Credits minimum = std::numeric_limits<Credits>::min();
    const Credits maximum = std::numeric_limits<Credits>::max();
    // Do not turn subtraction into addition of -b: -INT64_MIN would overflow.
    if ((b > 0 && a < minimum + b) || (b < 0 && a > maximum + b)) {
        return Error::ArithmeticOverflow;
    }
    out = a - b;
    return Error::Ok;
}

Error checked_multiply(Credits a, Credits b, Credits& out) noexcept {
    const Credits minimum = std::numeric_limits<Credits>::min();
    const Credits maximum = std::numeric_limits<Credits>::max();
    if (a == 0 || b == 0) {
        out = 0;
        return Error::Ok;
    }

    // Divisions below are safe: no divisor is zero and none computes MIN / -1.
    if (a > 0) {
        if ((b > 0 && a > maximum / b) || (b < 0 && b < minimum / a)) {
            return Error::ArithmeticOverflow;
        }
    } else {
        if ((b > 0 && a < minimum / b) || (b < 0 && a < maximum / b)) {
            return Error::ArithmeticOverflow;
        }
    }
    out = a * b;
    return Error::Ok;
}

} // namespace mini_etg
