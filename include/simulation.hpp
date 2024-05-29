#pragma once

#include <vector>
#include <cstdint>

#include "portfolio.hpp"
#include "data.hpp"

namespace swr {

enum class Rebalancing : uint64_t {
    NONE,
    MONTHLY,
    YEARLY,
    THRESHOLD
};

enum class WithdrawalMethod : uint64_t {
    STANDARD, // Withdraw based on the initial portfolio
    CURRENT   // Withdraw based on the current portfolio
};

Rebalancing parse_rebalance(const std::string& str);
std::ostream & operator<<(std::ostream& out, const Rebalancing & rebalance);
std::ostream & operator<<(std::ostream& out, const WithdrawalMethod & wmethod);

struct scenario {
    std::vector<swr::allocation> portfolio;
    data_vector                  inflation_data;
    std::vector<data_vector>     values;
    std::vector<bool>            exchange_set;
    std::vector<data_vector>     exchange_rates;

    size_t           years;
    float            wr;
    size_t           start_year;
    size_t           end_year;
    float            initial_value      = 1000.0f;
    size_t           withdraw_frequency = 1;
    Rebalancing      rebalance          = Rebalancing::NONE;
    float            threshold          = 0.0f;
    float            fees               = 0.001f; // TER 0.1% = 0.001
    WithdrawalMethod wmethod            = WithdrawalMethod::STANDARD;
    float            minimum            = 0.03f; // Minimum of 3% * initial

    // By default, simulations can run for ever but the server will set that lower
    size_t timeout_msecs = 0;

    // Configuration for adding cash to the strategy
    float initial_cash = 0.0f;
    bool  cash_simple  = true;

    // Configuration to sustain the capital
    // By default, we can go down all the way to zero
    // Setting it to 1.0f makes it so we sustain the full capital
    float final_threshold = 0.0f;
    bool  final_inflation = true;

    // Configuration for equity glidepaths
    bool  glidepath = false;
    float gp_pass   = 0.0f; // Monthly increase (or decrease)
    float gp_goal   = 0.0f;

    // Configuration for social security
    bool   social_security = false;
    size_t social_delay    = 0;
    float  social_coverage = 0.0f;

    bool strict_validation = true;

    // Temporary values
    // TODO: Should maybe be moved later into a context class
    float target_value_ = 0.0f;

    bool is_failure(bool end, float current_value) const {
        // If it's not the end, we simply need to not run out of money
        if (!end) {
            return current_value <= 0.0f;
        }

        // If it's the end, we need to respect the threshold
        if (final_inflation) {
            return current_value <= final_threshold * target_value_;
        } else {
            return current_value <= final_threshold * initial_value;
        }
    }
};

std::ostream & operator<<(std::ostream& out, const scenario & scenario);

struct results {
    size_t successes = 0;
    size_t failures  = 0;

    float success_rate = 0.0f;

    float tv_average = 0.0f;
    float tv_minimum = 0.0f;
    float tv_maximum = 0.0f;
    float tv_median  = 0.0f;

    size_t worst_duration       = 0;
    size_t worst_starting_month = 0;
    size_t worst_starting_year  = 0;

    size_t lowest_eff_wr_year        = 0;
    size_t lowest_eff_wr_start_year  = 0;
    size_t lowest_eff_wr_start_month = 0;
    float lowest_eff_wr              = 0.0f;

    size_t highest_eff_wr_year        = 0;
    size_t highest_eff_wr_start_year  = 0;
    size_t highest_eff_wr_start_month = 0;
    float highest_eff_wr              = 0.0f;

    size_t worst_tv       = 0;
    size_t worst_tv_month = 0;
    size_t worst_tv_year  = 0;
    size_t best_tv        = 0;
    size_t best_tv_month  = 0;
    size_t best_tv_year   = 0;

    float withdrawn_per_year = 0;

    std::string message;
    bool error = false;

    void compute_terminal_values(std::vector<float> & terminal_values);

    void record_failure(size_t months, size_t current_month, size_t current_year) {
        if (!worst_duration || months < worst_duration) {
            worst_duration       = months;
            worst_starting_month = current_month;
            worst_starting_year  = current_year;
        }
    }
};

results simulation(scenario & scenario);

size_t simulations_ran();

} // namespace swr
