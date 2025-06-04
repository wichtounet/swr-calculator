//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include "simulation.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <sstream>
#include <cassert>
#include <cmath>
#include <array>
#include <chrono>
#include "data.hpp"

namespace chr = std::chrono;

namespace {

size_t simulations = 0;

// In percent
constexpr const float monthly_rebalancing_cost   = 0.005;
constexpr const float yearly_rebalancing_cost    = 0.01;
constexpr const float threshold_rebalancing_cost = 0.01;

bool valid_year(const swr::data_vector & data, size_t year) {
    return year >= data.front().year && year <= data.back().year;
}

template <size_t N>
auto current_value(const std::array<float, N>& current_values) {
    float value = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        value += current_values[i];
    }
    return value;
}

template <size_t N>
bool glidepath(swr::scenario & scenario, swr::context & context, std::array<float, N> & current_values) {
    if (scenario.glidepath) {
        // Check if we have already reached the target
        if (scenario.portfolio[0].allocation_ == scenario.gp_goal) {
            return true;
        }

        scenario.portfolio[0].allocation_ += scenario.gp_pass;
        scenario.portfolio[1].allocation_ -= scenario.gp_pass;

        // Acount for float inaccuracies
        if (scenario.gp_pass > 0.0f && scenario.portfolio[0].allocation_ > scenario.gp_goal) {
            scenario.portfolio[0].allocation_ = scenario.gp_goal;
            scenario.portfolio[1].allocation_ = 100.0f - scenario.gp_goal;
        } else if (scenario.gp_pass < 0.0f && scenario.portfolio[0].allocation_ < scenario.gp_goal) {
            scenario.portfolio[0].allocation_ = scenario.gp_goal;
            scenario.portfolio[1].allocation_ = 100.0f - scenario.gp_goal;
        }

        // If rebalancing is not monthly, we do a rebalancing ourselves
        // Otherwise, it will be done as the next step
        if (scenario.rebalance == swr::Rebalancing::NONE) {
            // Pay the fees
            for (size_t i = 0; i < N; ++i) {
                current_values[i] *= 1.0f - monthly_rebalancing_cost / 100.0f;
            }

            const auto total_value = current_value(current_values);

            // Fees can cause failure
            if (scenario.is_failure(context, total_value)) {
                return false;
            }

            for (size_t i = 0; i < N; ++i) {
                current_values[i] = total_value * (scenario.portfolio[i].allocation_ / 100.0f);
            }
        }
    }

    return true;
}

template <size_t N>
bool monthly_rebalance(const swr::scenario & scenario, swr::context & context, std::array<float, N> & current_values) {
    // Nothing to rebalance if we have a single asset
    if constexpr (N == 1) {
        return true;
    }

    // Monthly Rebalance if necessary
    if (scenario.rebalance == swr::Rebalancing::MONTHLY) {
        // Pay the fees
        for (size_t i = 0; i < N; ++i) {
            current_values[i] *= 1.0f - monthly_rebalancing_cost / 100.0f;
        }

        const auto total_value = current_value(current_values);

        // Fees can cause failure
        if (scenario.is_failure(context, total_value)) {
            return false;
        }

        for (size_t i = 0; i < N; ++i) {
            current_values[i] = total_value * (scenario.portfolio[i].allocation_ / 100.0f);
        }
    }

    // Threshold Rebalance if necessary
    if (scenario.rebalance == swr::Rebalancing::THRESHOLD) {
        bool rebalance = false;

        {
            const auto total_value = current_value(current_values);
            for (size_t i = 0; i < N; ++i) {
                if (std::abs((scenario.portfolio[i].allocation_ / 100.0f) - current_values[i] / total_value) >= scenario.threshold) {
                    rebalance = true;
                    break;
                }
            }
        }

        if (rebalance) {
            // Pay the fees
            for (size_t i = 0; i < N; ++i) {
                current_values[i] *= 1.0f - threshold_rebalancing_cost / 100.0f;
            }

            // We need to recompute the total value after the fees
            const auto total_value = current_value(current_values);

            // Fees can cause failure
            if (scenario.is_failure(context, total_value)) {
                return false;
            }

            for (size_t i = 0; i < N; ++i) {
                current_values[i] = total_value * (scenario.portfolio[i].allocation_ / 100.0f);
            }
        }
    }

    return true;
}

template <size_t N>
bool yearly_rebalance(const swr::scenario & scenario, swr::context & context, std::array<float, N> & current_values) {
    // Nothing to rebalance if we have a single asset
    if constexpr (N == 1) {
        return true;
    }

    // Yearly Rebalance if necessary
    if (scenario.rebalance == swr::Rebalancing::YEARLY) {
        // Pay the fees
        for (size_t i = 0; i < N; ++i) {
            current_values[i] *= 1.0f - yearly_rebalancing_cost / 100.0f;
        }

        const auto total_value = current_value(current_values);

        // Fees can cause failure
        if (scenario.is_failure(context, total_value)) {
            return false;
        }

        for (size_t i = 0; i < N; ++i) {
            current_values[i] = total_value * (scenario.portfolio[i].allocation_ / 100.0f);
        }
    }

    return true;
}

template <size_t N>
bool pay_fees(const swr::scenario & scenario, swr::context & context, std::array<float, N> & current_values) {
    // Simulate TER
    if (scenario.fees > 0.0f) {
        for (size_t i = 0; i < N; ++i) {
            current_values[i] *= 1.0f - (scenario.fees / 12.0f);
        }

        // TER can cause failure
        if (scenario.is_failure(context, current_value(current_values))) {
            return false;
        }
    }

    return true;
}

template <size_t N>
bool withdraw(const swr::scenario & scenario, swr::context & context, std::array<float, N> & current_values, const std::array<float, N> & market_values) {
    if ((context.months - 1) % scenario.withdraw_frequency == 0) {
        const auto total_value = current_value(current_values);

        auto periods = scenario.withdraw_frequency;
        if ((context.months - 1) + scenario.withdraw_frequency > context.total_months) {
            periods = context.total_months - (context.months - 1);
        }

        float withdrawal_amount = 0;

        // Compute the withdrawal amount based on the withdrawal strategy
        if (scenario.wmethod == swr::WithdrawalMethod::STANDARD) {
            withdrawal_amount = context.withdrawal / (12.0f / periods);

            if (scenario.flexibility == swr::Flexibility::PORTFOLIO) {
                if (total_value < scenario.flexibility_threshold_2 * scenario.initial_value) {
                    withdrawal_amount *= scenario.flexibility_change_2;
                    context.flexible = true;
                } else if (total_value < scenario.flexibility_threshold_1 * scenario.initial_value) {
                    withdrawal_amount *= scenario.flexibility_change_1;
                    context.flexible = true;
                }
            } else if (scenario.flexibility == swr::Flexibility::MARKET) {
                const auto market_value = current_value(market_values);

                if (market_value > context.hist_high) {
                    context.hist_high = market_value;
                }

                if (market_value < scenario.flexibility_threshold_2 * context.hist_high) {
                    withdrawal_amount *= scenario.flexibility_change_2;
                    context.flexible = true;
                } else if (market_value < scenario.flexibility_threshold_1 * context.hist_high) {
                    withdrawal_amount *= scenario.flexibility_change_1;
                    context.flexible = true;
                }
            }
        } else if (scenario.wmethod == swr::WithdrawalMethod::CURRENT) {
            withdrawal_amount = (total_value * (scenario.wr / 100.0f)) / (12.0f / periods);

            // Make sure, we don't go over the minimum
            const float minimum_withdrawal = context.minimum / (12.0f / periods);
            if (withdrawal_amount < minimum_withdrawal) {
                withdrawal_amount = minimum_withdrawal;
            }
        } else if (scenario.wmethod == swr::WithdrawalMethod::VANGUARD) {
            // Compute the withdrawal for the year

            if (context.months == 1) {
                context.vanguard_withdrawal = total_value * (scenario.wr / 100.0f);
                context.last_year_withdrawal = context.vanguard_withdrawal;
            } else if ((context.months - 1) % 12 == 0) {
                context.last_year_withdrawal = context.vanguard_withdrawal;
                context.vanguard_withdrawal  = total_value * (scenario.wr / 100.0f);

                // Don't go over a given maximum decrease or increase
                if (context.vanguard_withdrawal > (1.0f + scenario.vanguard_max_increase) * context.last_year_withdrawal) {
                    context.vanguard_withdrawal = (1.0f + scenario.vanguard_max_increase) * context.last_year_withdrawal;
                } else if (context.vanguard_withdrawal < (1.0f - scenario.vanguard_max_decrease) * context.last_year_withdrawal) {
                    context.vanguard_withdrawal = (1.0f - scenario.vanguard_max_decrease) * context.last_year_withdrawal;
                }
            }

            // The base amount to withdraw
            withdrawal_amount = context.vanguard_withdrawal / (12.0f / periods);

            // Make sure, we don't go over the minimum
            const float minimum_withdrawal = context.minimum / (12.0f / periods);
            if (withdrawal_amount < minimum_withdrawal) {
                withdrawal_amount = minimum_withdrawal;
            }
        }

        if (scenario.social_security) {
            if ((context.months / 12.0f) >= scenario.social_delay) {
                withdrawal_amount -= (scenario.social_coverage * withdrawal_amount);
            }
        }

        context.last_withdrawal_amount = withdrawal_amount;

        if (withdrawal_amount <= 0.0f) {
            return true;
        }

        auto eff_wr = withdrawal_amount / context.year_start_value;

        // Strategies with cash
        if (scenario.cash_simple || ((eff_wr * 100.0f) >= (scenario.wr / 12.0f))) {
            // First, withdraw from cash if possible
            if (context.cash > 0.0f) {
                if (withdrawal_amount <= context.cash) {
                    context.year_withdrawn += withdrawal_amount;
                    context.cash -= withdrawal_amount;
                    withdrawal_amount = 0;
                } else {
                    context.year_withdrawn += context.cash;
                    withdrawal_amount -= context.cash;
                    context.cash = 0.0f;
                }
            }
        }

        switch (scenario.wselection) {
            case swr::WithdrawalSelection::ALLOCATION:
            {
                for (auto& value : current_values) {
                    value = std::max(0.0f, value - (value / total_value) * withdrawal_amount);
                }
                break;
            }

            case swr::WithdrawalSelection::STOCKS:
                [[fallthrough]];
            case swr::WithdrawalSelection::BONDS:
            {
                if (current_values[context.withdraw_index] > withdrawal_amount) {
                    current_values[context.withdraw_index] -= withdrawal_amount;
                } else {
                    auto leftover = withdrawal_amount - current_values[context.withdraw_index];
                    current_values[context.withdraw_index] = 0;
                    current_values[context.withdraw_index == 1 ? 0 : 1] = std::max(0.0f, current_values[context.withdraw_index == 1 ? 0 : 1] - leftover);
                }
            }
        }


        // Check for failure after the withdrawal
        if (scenario.is_failure(context, current_value(current_values))) {
            context.year_withdrawn += total_value;
            return false;
        }

        context.year_withdrawn += withdrawal_amount;
    }

    return true;
}

template <size_t N>
swr::results swr_simulation(swr::scenario & scenario) {
    auto & inflation_data = scenario.inflation_data;
    auto & values = scenario.values;
    auto & exchange_rates = scenario.exchange_rates;

    // The final results
    swr::results res;

    auto start_tp = chr::high_resolution_clock::now();

    // For compatibility, we set up exchange_rates and exchange_sets

    if (scenario.exchange_set.empty() || exchange_rates.empty()) {
        res.message = "Invalid scenario (no exchange rates)";
        res.error = true;
        return res;
    }

    // 0. Make sure the years make some sense

    if (scenario.start_year >= scenario.end_year) {
        res.message = "The end year must be higher than the start year";
        res.error = true;
        return res;
    }

    if (!scenario.years) {
        res.message = "The number of years must be at least 1";
        res.error = true;
        return res;
    }

    // 1. Adapt the start and end year with inflation and stocks
    // Note: the data is already normalized so we do not have to check for stat
    // and end months

    bool changed = false;

    // A. If the interval is totally out, there is nothing we can do

    if (scenario.strict_validation) {
        if (!valid_year(inflation_data, scenario.start_year) && !valid_year(inflation_data, scenario.end_year)) {
            res.message = "The given period is out of the historical data, it's either too far in the future or too far in the past";
            res.error   = true;
            return res;
        }

        for (auto& v : values) {
            if (!valid_year(v, scenario.start_year) && !valid_year(v, scenario.end_year)) {
                res.message = "The given period is out of the historical data, it's either too far in the future or too far in the past";
                res.error   = true;
                return res;
            }
        }
    }

    // B. Try to adapt the years

    if (inflation_data.front().year > scenario.start_year) {
        scenario.start_year = inflation_data.front().year;
        changed    = true;
    }

    if (inflation_data.back().year < scenario.end_year) {
        scenario.end_year = inflation_data.back().year;
        changed  = true;
    }

    for (auto& v : values) {
        if (v.front().year > scenario.start_year) {
            scenario.start_year = v.front().year;
            changed    = true;
        }

        if (v.back().year < scenario.end_year) {
            scenario.end_year = v.back().year;
            changed  = true;
        }
    }

    for (size_t i = 0; i < N; ++i) {
        if (scenario.exchange_set[i]) {
            auto& v = exchange_rates[i];

            if (v.front().year > scenario.start_year) {
                scenario.start_year = v.front().year;
                changed             = true;
            }

            if (v.back().year < scenario.end_year) {
                scenario.end_year = v.back().year;
                changed           = true;
            }
        }
    }

    if (changed) {
        // It's possible that the change is invalid
        if (scenario.end_year == scenario.start_year) {
            res.message = "The period is invalid with this duration. Try to use a longer period (1871-2018 works well) or a shorter duration.";
            res.error = true;
            return res;
        } else {
            std::stringstream ss;
            ss << "The period has been changed to "
               << scenario.start_year << ":" << scenario.end_year
               << " based on the available data. ";
            res.message = ss.str();
        }
    }

    // 2. Make sure the simulation makes sense

    if (scenario.portfolio.empty()) {
        res.message = "Cannot work with an empty portfolio";
        res.error = true;
        return res;
    }

    if (scenario.social_security) {
        if (scenario.initial_cash > 0.0f) {
            res.message = "Social security and cash is not implemented";
            res.error = true;
            return res;
        }

        if (scenario.wmethod != swr::WithdrawalMethod::STANDARD) {
            res.message = "Social security is only implemented for standard withdrawal method";
            res.error = true;
            return res;
        }
    }

    if (scenario.wmethod == swr::WithdrawalMethod::VANGUARD && scenario.withdraw_frequency != 1) {
        res.message = "Vanguard dynamic spending is only implemented with monthly withdrawals";
        res.error = true;
        return res;
    }

    size_t withdraw_index = 0;
    if (scenario.wselection != swr::WithdrawalSelection::ALLOCATION) {
        auto & portfolio = scenario.portfolio;

        if (portfolio.size() != 2) {
            res.message = "This withdrawal selection method only works with bonds and stocks";
            res.error = true;
            return res;
        }

        if (portfolio[0].asset != "us_stocks" && portfolio[0].asset != "us_bonds") {
            res.message = "This withdrawal selection method only works with bonds and stocks";
            res.error = true;
            return res;
        }

        if (portfolio[1].asset != "us_stocks" && portfolio[2].asset != "us_bonds") {
            res.message = "This withdrawal selection method only works with bonds and stocks";
            res.error = true;
            return res;
        }

        if (scenario.wselection == swr::WithdrawalSelection::BONDS) {
            withdraw_index = portfolio[0].asset == "us_bonds" ? 0 : 1;
        } else if (scenario.wselection == swr::WithdrawalSelection::STOCKS) {
            withdraw_index = portfolio[0].asset == "us_stocks" ? 0 : 1;
        } 
    }

    if (scenario.end_year - scenario.start_year < scenario.years) {
        std::stringstream ss;
        ss << "The period is too short for a " << scenario.years << " years simulation. "
           << "The number of years has been reduced to "
           << (scenario.end_year - scenario.start_year);
        res.message += ss.str();

        scenario.years = scenario.end_year - scenario.start_year;
    }

    if (scenario.glidepath){
        auto & portfolio = scenario.portfolio;
        if (portfolio[0].asset != "us_stocks") {
            res.message = "The first assert must be us_stocks for glidepath";
            res.error = true;
            return res;
        }

        if (scenario.rebalance != swr::Rebalancing::NONE && scenario.rebalance != swr::Rebalancing::MONTHLY) {
            res.message = "Invalid rebalancing method for glidepath";
            res.error = true;
            return res;
        }

        if (scenario.gp_pass == 0.0f) {
            res.message = std::format("Invalid pass ({}) for glidepath", scenario.gp_pass);
            res.error = true;
            return res;
        }

        if (scenario.gp_pass > 0.0f && scenario.gp_goal <= portfolio[0].allocation) {
            std::cout << scenario.gp_pass << std::endl;
            std::cout << scenario.gp_goal << std::endl;
            std::cout << portfolio[0].allocation << std::endl;
            res.message = "Invalid goal/pass (1) for glidepath";
            res.error = true;
            return res;
        }

        if (scenario.gp_pass < 0.0f && scenario.gp_goal >= portfolio[0].allocation) {
            std::cout << scenario.gp_pass << std::endl;
            std::cout << scenario.gp_goal << std::endl;
            std::cout << portfolio[0].allocation << std::endl;
            res.message = "Invalid goal/pass (2) for glidepath";
            res.error = true;
            return res;
        }
    }

    if (scenario.flexibility != swr::Flexibility::NONE) {
        if (scenario.wmethod != swr::WithdrawalMethod::STANDARD) {
            res.message = "Invalid withdrawal method for flexibility";
            res.error = true;
            return res;
        }

        if (scenario.initial_cash > 0.0f) {
            res.message = "Cannot use cash with flexibility";
            res.error = true;
            return res;
        }

        if (scenario.flexibility_threshold_1 <= scenario.flexibility_threshold_2) {
            res.message = "The first threshold must be higher than the second";
            res.error = true;
            return res;
        }
    }

    // More validation of data (should not happen but would fail silently otherwise)
    
    bool valid = true;
    for (size_t i = 0; i < N; ++i) {
        valid &= swr::is_start_valid(values[i], scenario.start_year, 1);
        valid &= swr::is_start_valid(exchange_rates[i], scenario.start_year, 1);
    }

    valid &= swr::is_start_valid(inflation_data, scenario.start_year, 1);

    if (!valid) {
        res.message = "Invalid data points (internal bug, contact the developer)";
        res.error   = true;
        return res;
    }

    // Prepare the starting points (for efficiency)
    std::array<swr::data_vector::const_iterator, N> start_returns;
    std::array<swr::data_vector::const_iterator, N> start_exchanges;

    for (size_t i = 0; i < N; ++i) {
        start_returns[i] = swr::get_start(values[i], scenario.start_year, 1);
        start_exchanges[i] = swr::get_start(exchange_rates[i], scenario.start_year, 1);
    }

    auto start_inflation = swr::get_start(inflation_data, scenario.start_year, 1);

    // 3. Do the actual simulation

    std::vector<std::vector<float>> spending;
    std::array<swr::data_vector::const_iterator, N> returns;
    std::array<swr::data_vector::const_iterator, N> exchanges;

    res.terminal_values.reserve(((scenario.end_year - scenario.start_year) - scenario.years) * 12);

    for (size_t current_year = scenario.start_year; current_year <= scenario.end_year - scenario.years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            spending.emplace_back();

            swr::context context;
            context.months = 1;
            context.total_months = scenario.years * 12;
            context.withdraw_index = withdraw_index;

            // The amount of money withdrawn per year (STANDARD method)
            context.withdrawal = scenario.initial_value * (scenario.wr / 100.0f);

            // The minimum amount of money withdraw (CURRENT method)
            context.minimum = scenario.initial_value * scenario.minimum;

            // The amount of cash available
            context.cash = scenario.initial_cash;

            // Used for the target threshold
            context.target_value_ = scenario.initial_value;

            const size_t end_year  = current_year + (current_month - 1 + context.total_months - 1) / 12;
            const size_t end_month = 1 + ((current_month - 1) + (context.total_months - 1) % 12) % 12;

            // Reset the allocation for the context
            for (auto & asset : scenario.portfolio) {
                asset.allocation_ = asset.allocation;
            }

            std::array<float, N> current_values;
            std::array<float, N> market_values;

            // Compute the initial values of the assets
            for (size_t i = 0; i < N; ++i) {
                current_values[i] = scenario.initial_value * (scenario.portfolio[i].allocation_ / 100.0f);
                market_values[i]  = scenario.initial_value * (scenario.portfolio[i].allocation_ / 100.0f);
                returns[i]        = start_returns[i]++;
                exchanges[i]      = start_exchanges[i]++;
            }

            auto inflation = start_inflation++;

            float total_withdrawn = 0.0f;
            bool failure = false;

            auto step = [&](auto result) {
                if (!failure && !result()) {
                    failure = true;
                    res.record_failure(context.months, current_month, current_year);
                }
            };

            for (size_t y = current_year; y <= end_year; ++y) {
                context.year_start_value = current_value(current_values);
                context.year_withdrawn = 0.0f;

                size_t m = 0;
                for (m = (y == current_year ? current_month : 1); !failure && m <= (y == end_year ? end_month : 12); ++m, ++context.months) {
                    // Adjust the portfolio with the returns and exchanges
                    for (size_t i = 0; i < N; ++i) {
                        current_values[i] *= returns[i]->value;
                        current_values[i] *= exchanges[i]->value;

                        market_values[i] *= returns[i]->value;
                        market_values[i] *= exchanges[i]->value;

                        ++returns[i];
                        ++exchanges[i];
                    }

                    // Stock market losses can cause failure
                    step([&]() { return !scenario.is_failure(context, current_value(current_values)); });

                    // Glidepath
                    step([&]() { return glidepath(scenario, context, current_values); });

                    // Monthly Rebalance
                    step([&]() { return monthly_rebalance(scenario, context, current_values); });

                    // Simulate TER
                    step([&]() { return pay_fees(scenario, context, current_values); });

                    // Adjust the withdrawals for inflation
                    context.withdrawal *= inflation->value;
                    context.minimum *= inflation->value;
                    context.target_value_ *= inflation->value;
                    ++inflation;

                    // Monthly withdrawal
                    step([&]() { return withdraw(scenario, context, current_values, market_values); });

                    // Record spending
                    if ((context.months - 1) % 12 == 0) {
                        spending.back().push_back(context.last_withdrawal_amount);
                    } else {
                        spending.back().back() += context.last_withdrawal_amount;
                    }
                }

                total_withdrawn += context.year_withdrawn;

                // Yearly Rebalance and check for failure
                step([&]() { return yearly_rebalance(scenario, context, current_values); });

                // Record effective withdrawal rates

                if (failure) {
                    auto eff_wr = context.year_withdrawn / context.year_start_value;

                    if (!res.lowest_eff_wr_year || eff_wr < res.lowest_eff_wr) {
                        res.lowest_eff_wr_start_year  = current_year;
                        res.lowest_eff_wr_start_month = current_month;
                        res.lowest_eff_wr_year        = y;
                        res.lowest_eff_wr             = eff_wr;
                    }

                    if (!res.highest_eff_wr_year || eff_wr > res.highest_eff_wr) {
                        res.highest_eff_wr_start_year = current_year;
                        res.highest_eff_wr_start_month = current_month;
                        res.highest_eff_wr_year       = y;
                        res.highest_eff_wr            = eff_wr;
                    }

                    break;
                }
            }

            const auto final_value = failure ? 0.0f : current_value(current_values);

            if (!failure) {
                ++res.successes;

                if (context.flexible) {
                    ++res.flexible_successes;
                }

                // Total amount of money withdrawn
                res.total_withdrawn += total_withdrawn;
            } else {
                ++res.failures;

                if (context.flexible) {
                    ++res.flexible_failures;
                }
            }

            res.terminal_values.push_back(final_value);
            res.flexible.push_back(context.flexible ? 1.0f : 0.0f);

            if (failure) {
                spending.pop_back();
            }

            // Record periods

            if (!res.best_tv_year) {
                res.best_tv_year  = current_year;
                res.best_tv_month = current_month;
                res.best_tv       = final_value;
            }

            if (!res.worst_tv_year) {
                res.worst_tv_year  = current_year;
                res.worst_tv_month = current_month;
                res.worst_tv       = final_value;
            }

            if (final_value < res.worst_tv) {
                res.worst_tv_year  = current_year;
                res.worst_tv_month = current_month;
                res.worst_tv       = final_value;
            }

            if (final_value > res.best_tv) {
                res.best_tv_year  = current_year;
                res.best_tv_month = current_month;
                res.best_tv       = final_value;
            }

            // After each starting point, we check if we should timeout

            if (scenario.timeout_msecs) {
                auto stop_tp = chr::high_resolution_clock::now();
                auto duration = chr::duration_cast<chr::milliseconds>(stop_tp - start_tp).count();

                if (size_t(duration) > scenario.timeout_msecs) {
                    res.message = "The computation took too long";
                    res.error = true;
                    std::cout << "ERROR: Timeout after " << duration << "ms" << std::endl;
                    return res;
                }
            }
        }
    }

    res.withdrawn_per_year = (res.total_withdrawn / scenario.years) / float(res.successes);

    res.highest_eff_wr *= 100.0f;
    res.lowest_eff_wr *= 100.0f;

    res.success_rate = 100 * (res.successes / float(res.successes + res.failures));
    res.compute_terminal_values(res.terminal_values);
    res.compute_spending(spending, scenario.years);

    simulations += res.terminal_values.size();

    return res;
}

} // end of anonymous namespace

swr::Rebalancing swr::parse_rebalance(const std::string& str) {
    if (str == "none") {
        return Rebalancing::NONE;
    } else if (str == "monthly") {
        return Rebalancing::MONTHLY;
    } else if (str == "yearly") {
        return Rebalancing::YEARLY;
    } else {
        return Rebalancing::THRESHOLD;
    }
}

std::ostream & swr::operator<<(std::ostream& out, const Rebalancing & rebalance){
    switch (rebalance) {
        case Rebalancing::NONE:
            return out << "none";
        case Rebalancing::MONTHLY:
            return out << "monthly";
        case Rebalancing::YEARLY:
            return out << "yearly";
        case Rebalancing::THRESHOLD:
            return out << "threshold";
    }

    return out << "Unknown rebalancing";
}

std::ostream & swr::operator<<(std::ostream& out, const WithdrawalMethod & wmethod){
    switch (wmethod) {
        case WithdrawalMethod::STANDARD:
            return out << "standard";
        case WithdrawalMethod::CURRENT:
            return out << "current";
        case WithdrawalMethod::VANGUARD:
            return out << "vanguard";
    }

    return out << "Unknown withdrawal method";
}

std::ostream & swr::operator<<(std::ostream& out, const WithdrawalSelection & wselection){
    switch (wselection) {
        case swr::WithdrawalSelection::ALLOCATION:
            return out << "allocatio";
        case swr::WithdrawalSelection::STOCKS:
            return out << "stocks";
        case swr::WithdrawalSelection::BONDS:
            return out << "bonds";
    }

    return out << "Unknown withdrawal method";
}

swr::results swr::simulation(scenario & scenario) {
    const size_t number_of_assets = scenario.portfolio.size();

    switch (number_of_assets) {
    case 1:
            return swr_simulation<1>(scenario);
    case 2:
            return swr_simulation<2>(scenario);
    case 3:
            return swr_simulation<3>(scenario);
    case 4:
            return swr_simulation<4>(scenario);
    case 5:
            return swr_simulation<5>(scenario);
    default:
            swr::results res;
            res.message = "The number of assets is too high";
            res.error   = true;
            return res;
    }
}

void swr::results::compute_terminal_values(std::vector<float> & terminal_values) {
    std::ranges::sort(terminal_values);

    tv_median  = terminal_values[terminal_values.size() / 2 + 1];
    tv_minimum = terminal_values.front();
    tv_maximum = terminal_values.back();
    tv_average = std::accumulate(terminal_values.begin(), terminal_values.end(), 0.0f) / terminal_values.size();
}

void swr::results::compute_spending(std::vector<std::vector<float>> & yearly_spending, size_t years) {
    if (yearly_spending.empty()) {
        spending_median  = 0;
        spending_minimum = 0;
        spending_maximum = 0;
        spending_average = 0;
        return;
    }

    std::vector<float> spending;

    for (auto & yearly : yearly_spending) {
        spending.push_back(std::accumulate(yearly.begin(), yearly.end(), 0.0f));

        for (size_t y = 1; y < yearly.size(); ++y) {
            if (yearly[y] >= 1.5f * yearly[0]){
                ++years_large_spending;
            }

            if (yearly[y] <= 0.5f * yearly[0]){
                ++years_small_spending;
            }

            if (yearly[y] >= 1.1f * yearly[y - 1]) {
                ++years_volatile_up_spending;
            }

            if (yearly[y] <= 0.9f * yearly[y - 1]) {
                ++years_volatile_down_spending;
            }
        }
    }

    std::ranges::sort(spending);

    spending_median  = spending[spending.size() / 2 + 1] / years;
    spending_minimum = spending.front() / years;
    spending_maximum = spending.back() / years;
    spending_average = (std::accumulate(spending.begin(), spending.end(), 0.0f) / spending.size()) / years;
}

size_t swr::simulations_ran() {
    return simulations;
}

std::ostream & swr::operator<<(std::ostream& out, const scenario & scenario) {
    out << "{"
        << "portfolio=" << scenario.portfolio
        << " inflation=" << scenario.inflation_data.name
        << " exchange_set=" << std::ranges::count(scenario.exchange_set, true)
        << " wr=" << scenario.wr << " rebalance={" << scenario.rebalance << "," << scenario.threshold << "}"
        << " init=" << scenario.initial_value
        << " years={" << scenario.years << "," << scenario.start_year << "," << scenario.end_year << "}"
        << " withdraw={" << scenario.withdraw_frequency << "," << scenario.wmethod << "," << scenario.wselection << "," << scenario.minimum << "}"
        << " fees=" << scenario.fees
        << " soc_sec={" << scenario.social_security << "," << scenario.social_delay << "," << scenario.social_coverage << "}"
        << " gp={" << scenario.glidepath << "," << scenario.gp_pass << " " << scenario.gp_goal << "}"
        << " fin={" << scenario.final_inflation << "," << scenario.final_threshold << "}"
        << " cash={" << scenario.cash_simple << "," << scenario.initial_cash << "}"
        << "}";
    return out;
}
