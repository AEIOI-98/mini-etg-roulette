#include "mini_etg/lucky.hpp"
#include "mini_etg/wheel.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <system_error>

using namespace mini_etg;

namespace {

constexpr std::uint64_t kMaxSpins = 1000000000;
constexpr std::uint64_t kSampleInterval = 100000;
constexpr double kChiSquareCritical = 58.619; // df=36, upper-tail alpha=0.01, NIST.
constexpr std::uint32_t kLuckySeedMask = 0x9e3779b9u;

class SeededRandom final : public IRandom {
public:
    explicit SeededRandom(std::uint32_t seed) noexcept : generator_(seed) {}
    std::uint32_t next_u32() noexcept override {
        return static_cast<std::uint32_t>(generator_());
    }
private:
    std::mt19937 generator_;
};

// This math-only wheel emits motion and a uniformly selected physical pocket.
// It follows the same IWheel boundary; it does not model hardware or animation.
class AnalysisWheel final : public IWheel {
public:
    explicit AnalysisWheel(IRandom& random) noexcept : random_(random) {}
    Error start(RoundId round_id, TickMs now_ms) noexcept override {
        stop();
        std::uint32_t index = 0;
        const Error error = uniform_below(random_, kPocketCount, index);
        if (error != Error::Ok) return error;
        round_id_ = round_id;
        started_ = now_ms;
        pocket_ = PocketIndex{static_cast<std::uint8_t>(index)};
        active_ = true;
        return Error::Ok;
    }
    WheelBatch poll(TickMs now_ms) noexcept override {
        WheelBatch batch{};
        if (!active_) return batch;
        const TickMs elapsed = now_ms - started_;
        if (!motion_sent_ && elapsed >= 1) {
            batch.reports[batch.count++] = {
                round_id_, WheelSignal::MotionDetected, started_ + 1, {}, FaultCode::None
            };
            motion_sent_ = true;
        }
        if (elapsed >= 2) {
            batch.reports[batch.count++] = {
                round_id_, WheelSignal::BallSettled, started_ + 2, pocket_, FaultCode::None
            };
            active_ = false;
        }
        return batch;
    }
    void stop() noexcept override { active_ = false; motion_sent_ = false; }
private:
    IRandom& random_;
    RoundId round_id_{0};
    TickMs started_{0};
    PocketIndex pocket_{};
    bool active_{false};
    bool motion_sent_{false};
};

struct Options {
    std::uint64_t spins{10000000};
    std::uint32_t seed{20261111};
    std::filesystem::path output{"tools/results"};
};

bool parse_unsigned(const std::string& text, std::uint64_t maximum, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > maximum) {
        return false;
    }
    out = value;
    return true;
}

bool parse_options(int argc, char** argv, Options& out) {
    bool have_spins = false, have_seed = false, have_output = false;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        if (i + 1 == argc) return false;
        const std::string value = argv[++i];
        std::uint64_t number = 0;
        if (name == "--spins" && !have_spins) {
            if (!parse_unsigned(value, kMaxSpins, number) || number < 2) return false;
            out.spins = number;
            have_spins = true;
        } else if (name == "--seed" && !have_seed) {
            if (!parse_unsigned(value, std::numeric_limits<std::uint32_t>::max(), number)) return false;
            out.seed = static_cast<std::uint32_t>(number);
            have_seed = true;
        } else if (name == "--output" && !have_output) {
            if (value.empty()) return false;
            out.output = value;
            have_output = true;
        } else {
            return false;
        }
    }
    return true;
}

bool require_ok(Error error, const char* operation) {
    if (error == Error::Ok) return true;
    std::cerr << operation << " failed with engine error " << static_cast<unsigned>(error) << '\n';
    return false;
}

struct Totals {
    Credits returned{0}; // Exact cumulative credits, never a floating-point ledger.
    long double squares{0}; // Analysis of the return distribution, not money storage.

    bool add(Credits amount) {
        if (amount < 0 || checked_add(returned, amount, returned) != Error::Ok) return false;
        const long double sample = static_cast<long double>(amount);
        squares += sample * sample;
        return true;
    }
};

struct Estimate {
    double rtp{0};
    double half_width{0};
    double theoretical_se{0};
    double z{0};
};

Estimate estimate(const Totals& totals, std::uint64_t spins, Credits stake_per_spin,
                  double theoretical_rtp, double normalized_variance) {
    const long double n = static_cast<long double>(spins);
    const long double total = static_cast<long double>(totals.returned);
    const long double variance = (totals.squares - total * total / n) / (n - 1);
    const long double sample_variance = variance > 0 ? variance : 0;
    Estimate result{};
    result.rtp = static_cast<double>(total / (n * stake_per_spin));
    result.half_width = static_cast<double>(1.96L * std::sqrt(sample_variance / n) / stake_per_spin);
    result.theoretical_se = std::sqrt(normalized_variance / static_cast<double>(spins));
    result.z = result.theoretical_se > 0
        ? (result.rtp - theoretical_rtp) / result.theoretical_se : 0;
    return result;
}

double portfolio_variance(const LuckyConfig& config, const LuckyMath& math) {
    // The portfolio has one winning straight per spin. Its return is B or M;
    // dividing by the 37-credit wager gives the RTP sample for that whole spin.
    double weighted_square = 0;
    std::uint64_t weights = 0;
    for (std::size_t i = 0; i < config.table_count; ++i) {
        const double multiplier = config.table[i].gross_multiplier;
        weighted_square += multiplier * multiplier * config.table[i].weight;
        weights += config.table[i].weight;
    }
    const double base = static_cast<double>(config.base_straight_profit_odds) + 1;
    const double selected = math.expected_count / kPocketCount;
    const double mean_square = (1 - selected) * base * base + selected * weighted_square / weights;
    return mean_square / (kPocketCount * kPocketCount) - math.straight_rtp * math.straight_rtp;
}

void report_row(std::ostream& out, const char* label, Credits wagered, const Totals& totals,
                double theory, const Estimate& measured) {
    out << "| " << label << " | " << wagered << " | " << totals.returned
        << " | " << theory * 100 << "% | " << measured.rtp * 100 << "% | ["
        << (measured.rtp - measured.half_width) * 100 << "%, "
        << (measured.rtp + measured.half_width) * 100 << "%] | " << measured.z << " |\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Usage: roulette_math [--spins 2..1000000000] [--seed 0..4294967295] [--output directory]\n";
        return 2;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(options.output, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Cannot create output directory: " << filesystem_error.message() << '\n';
        return 1;
    }
    std::ofstream convergence(options.output / "convergence.csv");
    std::ofstream histogram_file(options.output / "histogram.csv");
    std::ofstream report(options.output / "RESULTS.md");
    if (!convergence || !histogram_file || !report) {
        std::cerr << "Cannot open output files\n";
        return 1;
    }
    convergence << std::setprecision(12);
    histogram_file << std::setprecision(12);
    report << std::fixed << std::setprecision(6);

    LuckyConfig lucky_config{};
    lucky_config.enabled = true;
    LuckyConfig standard_config{};
    LuckyMath lucky_math{}, standard_math{};
    if (!require_ok(analyze_lucky(lucky_config, lucky_math), "Lucky analysis") ||
        !require_ok(analyze_lucky(standard_config, standard_math), "Standard analysis")) return 1;
    const double portfolio_return_variance = portfolio_variance(lucky_config, lucky_math);

    const std::uint32_t lucky_seed = options.seed ^ kLuckySeedMask;
    SeededRandom wheel_random(options.seed);
    SeededRandom lucky_random(lucky_seed);
    AnalysisWheel wheel(wheel_random);
    AcceptedBet standard_bet{};
    standard_bet.id = 1;
    standard_bet.request.stake = 1;
    standard_bet.request.selection.kind = BetKind::Straight;
    standard_bet.request.selection.count = 1;
    standard_bet.request.selection.numbers[0] = Number{0};
    standard_bet.profit_odds = 35;
    AcceptedBet lucky_bet = standard_bet;
    lucky_bet.profit_odds = lucky_config.base_straight_profit_odds;
    AcceptedBet winning_bet = lucky_bet;
    Totals standard_totals{}, lucky_totals{}, portfolio_totals{};
    std::array<std::uint64_t, kPocketCount> histogram{};
    std::array<std::uint64_t, kMaxLuckyNumbers> count_histogram{};
    std::array<std::uint64_t, kMaxMultiplierEntries> multiplier_histogram{};

    convergence << "spins,standard_returned,lucky_returned,portfolio_returned,standard_rtp,lucky_rtp,portfolio_rtp,theoretical_rtp\n";
    for (std::uint64_t spin = 1; spin <= options.spins; ++spin) {
        LuckyDraw draw{};
        // Draw before obtaining the wheel result, from a separate seeded source.
        if (!require_ok(draw_lucky_numbers(lucky_random, lucky_config, draw), "Lucky draw") ||
            !require_ok(wheel.start(spin, 0), "Wheel start")) return 1;
        const WheelBatch observations = wheel.poll(2);
        if (observations.count != 2 || observations.reports[0].signal != WheelSignal::MotionDetected ||
            observations.reports[1].signal != WheelSignal::BallSettled) {
            std::cerr << "Invalid analysis wheel batch\n";
            return 1;
        }
        Number result{};
        if (!require_ok(number_at(observations.reports[1].pocket, result), "Wheel mapping")) return 1;
        ++histogram[result.value];
        ++count_histogram[draw.count - 1];
        for (std::size_t i = 0; i < draw.count; ++i) {
            for (std::size_t entry = 0; entry < lucky_config.table_count; ++entry) {
                if (draw.entries[i].gross_multiplier == lucky_config.table[entry].gross_multiplier) {
                    ++multiplier_histogram[entry];
                }
            }
        }
        Credits standard_return = 0, lucky_return = 0, portfolio_return = 0;
        winning_bet.request.selection.numbers[0] = result;
        if (!require_ok(gross_return(standard_bet, result, standard_return), "Standard payout") ||
            !require_ok(lucky_gross_return(lucky_bet, result, lucky_config, draw, lucky_return), "Lucky payout") ||
            !require_ok(lucky_gross_return(winning_bet, result, lucky_config, draw, portfolio_return), "Portfolio payout")) return 1;
        if (!standard_totals.add(standard_return) || !lucky_totals.add(lucky_return) ||
            !portfolio_totals.add(portfolio_return)) {
            std::cerr << "Return total overflow\n";
            return 1;
        }
        if (spin % kSampleInterval == 0 || spin == options.spins) {
            const double n = static_cast<double>(spin);
            convergence << spin << ',' << standard_totals.returned << ',' << lucky_totals.returned
                << ',' << portfolio_totals.returned << ',' << standard_totals.returned / n
                << ',' << lucky_totals.returned / n << ',' << portfolio_totals.returned / (37 * n)
                << ',' << lucky_math.straight_rtp << '\n';
        }
    }

    const double expected_frequency = static_cast<double>(options.spins) / kPocketCount;
    double chi_square = 0;
    histogram_file << "number,observed,expected\n";
    for (std::size_t number = 0; number < kPocketCount; ++number) {
        const double difference = static_cast<double>(histogram[number]) - expected_frequency;
        chi_square += difference * difference / expected_frequency;
        histogram_file << number << ',' << histogram[number] << ',' << expected_frequency << '\n';
    }
    const Estimate standard = estimate(standard_totals, options.spins, 1,
        standard_math.straight_rtp, standard_math.straight_return_variance);
    const Estimate lucky = estimate(lucky_totals, options.spins, 1,
        lucky_math.straight_rtp, lucky_math.straight_return_variance);
    const Estimate portfolio = estimate(portfolio_totals, options.spins, 37,
        lucky_math.straight_rtp, portfolio_return_variance);
    const Credits spins = static_cast<Credits>(options.spins);

    report << "# Seeded Phase 2 math results\n\n"
        << "Generated by `tools/math.cpp` using shared C++ core selection and payout functions.\n\n"
        << "- Spins: " << options.spins << ".\n"
        << "- Fixed straight: one credit on printed number 0 per spin.\n"
        << "- Wheel MT19937 seed: " << options.seed << ".\n"
        << "- Lucky MT19937 seed: " << lucky_seed << " (`wheel_seed XOR 0x9e3779b9`).\n"
        << "- Lucky count: uniform " << static_cast<unsigned>(lucky_config.min_count) << "–"
        << static_cast<unsigned>(lucky_config.max_count) << "; numbers selected without replacement.\n"
        << "- Base straight payout: " << static_cast<unsigned>(lucky_config.base_straight_profit_odds)
        << ":1 profit (" << static_cast<unsigned>(lucky_config.base_straight_profit_odds) + 1
        << "× total return).\n- Gross multiplier weights:";
    for (std::size_t i = 0; i < lucky_config.table_count; ++i) {
        report << ' ' << lucky_config.table[i].gross_multiplier << "×:" << lucky_config.table[i].weight;
    }
    report << ".\n\n";
    if (options.spins < 10000000) {
        report << "**Smoke run:** fewer than the required 10 million spins. Do not use this run "
            << "as the Phase 2 Monte Carlo acceptance result. Small samples can give unreliable intervals.\n\n";
    }
    report << "| Experiment | Credits wagered | Credits returned | Analytical RTP | Observed RTP | Approx. 95% CI | z vs theory |\n"
        << "| --- | ---: | ---: | ---: | ---: | --- | ---: |\n";
    report_row(report, "Standard, fixed number", spins, standard_totals, standard_math.straight_rtp, standard);
    report_row(report, "Lucky, fixed number", spins, lucky_totals, lucky_math.straight_rtp, lucky);
    report_row(report, "Lucky, all 37 straights", 37 * spins, portfolio_totals, lucky_math.straight_rtp, portfolio);
    report << "\nThe all-37 portfolio stakes one credit on each number, or 37 credits per spin. "
        << "Its only return comes from the winning straight. Its interval uses " << options.spins
        << " independent spin samples, **not** " << 37 * options.spins << " independent bets. "
        << "It is a lower-variance estimate of the same per-credit RTP; it does not change the expected loss.\n\n"
        << "Analytical Lucky RTP differs from standard by "
        << (lucky_math.straight_rtp - standard_math.straight_rtp) * 100
        << " percentage points (target: within ±0.1 percentage point). "
        << "Theoretical fixed-number standard error at this sample size: " << lucky.theoretical_se * 100
        << " percentage points. The corresponding portfolio error is " << portfolio.theoretical_se * 100
        << " percentage points. Observed intervals use sample variance and a 1.96 normal approximation.\n\n"
        << "## Wheel frequency check\n\n"
        << "Pearson chi-square = **" << chi_square << "**, 36 degrees of freedom; expected "
        << expected_frequency << " observations per pocket. "
        << "The upper-tail 1% critical value is " << kChiSquareCritical << ". ";
    if (expected_frequency < 5) {
        report << "This smoke run is too small for the stated chi-square approximation; no test conclusion is reported.\n\n";
    } else {
        report << (chi_square < kChiSquareCritical
            ? "Uniformity is not rejected at the 1% level.\n\n"
            : "Uniformity is rejected at the 1% level for this sample.\n\n");
    }
    report << "A non-rejection is not proof of randomness, independence or hardware fairness. "
        << "The test does not measure serial correlation. A genuine uniform source can exceed "
        << "the threshold in roughly 1% of repeated experiments.\n\n"
        << "## Draw diagnostics\n\n| Lucky count | Observed rounds | Expected rounds |\n| --- | ---: | ---: |\n";
    for (std::size_t i = 0; i < count_histogram.size(); ++i) {
        const double expected_count = i + 1 >= lucky_config.min_count && i + 1 <= lucky_config.max_count
            ? static_cast<double>(options.spins) / (lucky_config.max_count - lucky_config.min_count + 1) : 0;
        report << "| " << i + 1 << " | " << count_histogram[i] << " | " << expected_count << " |\n";
    }
    report << "\n| Gross multiplier | Observed selections | Weight |\n| --- | ---: | ---: |\n";
    for (std::size_t i = 0; i < lucky_config.table_count; ++i) {
        report << "| " << lucky_config.table[i].gross_multiplier << "× | "
            << multiplier_histogram[i] << " | " << lucky_config.table[i].weight << " |\n";
    }
    report << "\nConvergence samples are in [convergence.csv](convergence.csv); all 37 number counts "
        << "are in [histogram.csv](histogram.csv). The full-round engine/recovery tests are separate "
        << "from this fast probability harness. See the repository's `docs/MATH.md` for derivation and limitations.\n\n"
        << "Statistical references: [NIST chi-square test](https://www.itl.nist.gov/div898/handbook/eda/section3/eda35f.htm), "
        << "[critical values](https://www.itl.nist.gov/div898/handbook/eda/section3/eda3674.htm), "
        << "[confidence limits](https://www.itl.nist.gov/div898/handbook/eda/section3/eda352.htm).\n";
    convergence.close();
    histogram_file.close();
    report.close();
    if (!convergence || !histogram_file || !report) {
        std::cerr << "Output write failed\n";
        return 1;
    }
    std::cout << "Completed " << options.spins << " spins. Results: "
        << (options.output / "RESULTS.md").string() << '\n';
    return 0;
}
