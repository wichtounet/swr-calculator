#pragma once

#include <vector>

#include "portfolio.hpp"
#include "data.hpp"

namespace swr {

enum class Rebalancing : uint64_t {
    NONE,
    MONTHLY,
    YEARLY,
    THRESHOLD
};

Rebalancing parse_rebalance(const std::string& str);
std::ostream & operator<<(std::ostream& out, const Rebalancing & rebalance);

struct scenario {
    std::vector<swr::allocation>        portfolio;
    std::vector<swr::data>              inflation_data;
    std::vector<std::vector<swr::data>> values;
    std::vector<std::vector<swr::data>> exchanges;

    size_t      years;
    float       wr;
    size_t      start_year;
    size_t      end_year;
    bool        monthly_wr = true;
    Rebalancing rebalance = Rebalancing::NONE;
    float       threshold = 0.0f;
    float       fees = 0.0f; // TER 1% = 0.01
};

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

    std::string message;
    bool error = false;

    void compute_terminal_values(std::vector<float> & terminal_values);
};

results simulation(scenario & scenario);

size_t simulations_ran();

} // namespace swr
