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
bool glidepath(bool end, swr::scenario & scenario, std::array<float, N> & current_values) {
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
            if (scenario.is_failure(end, total_value)) {
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
bool monthly_rebalance(bool end, swr::scenario & scenario, std::array<float, N> & current_values) {
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
        if (scenario.is_failure(end, total_value)) {
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
            if (scenario.is_failure(end, total_value)) {
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
bool yearly_rebalance(bool end, swr::scenario & scenario, std::array<float, N> & current_values) {
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
        if (scenario.is_failure(end, total_value)) {
            return false;
        }

        for (size_t i = 0; i < N; ++i) {
            current_values[i] = total_value * (scenario.portfolio[i].allocation_ / 100.0f);
        }
    }

    return true;
}

template <size_t N>
bool pay_fees(bool end, swr::scenario & scenario, std::array<float, N> & current_values) {
    // Simulate TER
    if (scenario.fees > 0.0f) {
        for (size_t i = 0; i < N; ++i) {
            current_values[i] *= 1.0f - (scenario.fees / 12.0f);
        }

        // TER can cause failure
        if (scenario.is_failure(end, current_value(current_values))) {
            return false;
        }
    }

    return true;
}

template <size_t N>
bool withdraw(size_t months, size_t total_months, swr::scenario & scenario, std::array<float, N> & current_values, float & cash, float & withdrawn, float minimum, float withdrawal, float starting_value) {
    const bool end = months == total_months;

    if ((months - 1) % scenario.withdraw_frequency == 0) {
        const auto total_value = current_value(current_values);

        auto periods = scenario.withdraw_frequency;
        if ((months - 1) + scenario.withdraw_frequency > total_months) {
            periods = total_months - (months - 1);
        }

        float withdrawal_amount = 0;

        // Compute the withdrawal amount based on the withdrawal strategy
        if (scenario.method == swr::Method::STANDARD) {
            withdrawal_amount = withdrawal / (12.0f / periods);
        } else if (scenario.method == swr::Method::CURRENT) {
            float minimum_withdrawal = minimum / (12.0f / periods);

            withdrawal_amount = (total_value * (scenario.wr / 100.0f)) / (12.0f / periods);
            if (withdrawal_amount < minimum_withdrawal) {
                withdrawal_amount = minimum_withdrawal;
            }
        }

        if (scenario.social_security) {
            if ((months / 12.0f) >= scenario.social_delay) {
                withdrawal_amount -= (scenario.social_coverage * withdrawal_amount);
            }
        }

        if (withdrawal_amount <= 0.0f) {
            return true;
        }

        auto eff_wr = withdrawal_amount / starting_value;

        // Strategies with cash
        if (scenario.cash_simple || ((eff_wr * 100.0f) >= (scenario.wr / 12.0f))) {
            // First, withdraw from cash if possible
            if (cash > 0.0f) {
                if (withdrawal_amount <= cash) {
                    withdrawn += withdrawal_amount;
                    cash -= withdrawal_amount;
                    withdrawal_amount = 0;
                } else {
                    withdrawn += cash;
                    withdrawal_amount -= cash;
                    cash = 0.0f;
                }
            }
        }

        for (auto& value : current_values) {
            value = std::max(0.0f, value - (value / total_value) * withdrawal_amount);
        }

        // Check for failure after the withdrawal
        if (scenario.is_failure(end, current_value(current_values))) {
            withdrawn += total_value;
            return false;
        }

        withdrawn += withdrawal_amount;
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

        if (scenario.method != swr::Method::STANDARD) {
            res.message = "Social security is only implemented for standard withdrawal method";
            res.error = true;
            return res;
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

    const size_t total_months           = scenario.years * 12;

    // Prepare the starting points (for efficiency)
    std::array<swr::data_vector::const_iterator, N> start_returns;
    std::array<swr::data_vector::const_iterator, N> start_exchanges;

    for (size_t i = 0; i < N; ++i) {
        start_returns[i] = swr::get_start(values[i], scenario.start_year, 1);
        start_exchanges[i] = swr::get_start(exchange_rates[i], scenario.start_year, 1);
    }

    auto start_inflation = swr::get_start(inflation_data, scenario.start_year, 1);

    // 3. Do the actual simulation

    std::vector<float> terminal_values;
    std::array<swr::data_vector::const_iterator, N> returns;
    std::array<swr::data_vector::const_iterator, N> exchanges;

    for (size_t current_year = scenario.start_year; current_year <= scenario.end_year - scenario.years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            const size_t end_year  = current_year + (current_month - 1 + total_months - 1) / 12;
            const size_t end_month = 1 + ((current_month - 1) + (total_months - 1) % 12) % 12;

            // The amount of money withdrawn per year (STANDARD method)
            float withdrawal = scenario.initial_value * (scenario.wr / 100.0f);

            // The minimum amount of money withdraw (CURRENT method)
            float minimum = scenario.initial_value * scenario.minimum;

            // The amount of cash available
            float cash = scenario.initial_cash;

            // Used for the target threshold
            scenario.target_value_ = scenario.initial_value;

            // Reset the allocation for the context
            for (auto & asset : scenario.portfolio) {
                asset.allocation_ = asset.allocation;
            }

            std::array<float, N> current_values;

            // Compute the initial values of the assets
            for (size_t i = 0; i < N; ++i) {
                current_values[i] = scenario.initial_value * (scenario.portfolio[i].allocation_ / 100.0f);
                returns[i]        = start_returns[i]++;
                exchanges[i]      = start_exchanges[i]++;
            }

            auto inflation = start_inflation++;

            size_t months = 1;
            float total_withdrawn = 0.0f;
            bool failure = false;

            auto step = [&](auto result) {
                if (!failure && !result()) {
                    failure = true;
                    res.record_failure(months, current_month, current_year);
                }
            };

            for (size_t y = current_year; y <= end_year; ++y) {
                const auto starting_value = current_value(current_values);

                float withdrawn = 0.0f;

                for (size_t m = (y == current_year ? current_month : 1); !failure && m <= (y == end_year ? end_month : 12); ++m, ++months) {
                    const bool end = months == total_months;

                    // Adjust the portfolio with the returns and exchanges
                    for (size_t i = 0; i < N; ++i) {
                        current_values[i] *= returns[i]->value;
                        current_values[i] *= exchanges[i]->value;

                        ++returns[i];
                        ++exchanges[i];
                    }

                    // Stock market losses can cause failure
                    step([&]() { return !scenario.is_failure(end, current_value(current_values)); });

                    // Glidepath
                    step([&]() { return glidepath(end, scenario, current_values); });

                    // Monthly Rebalance
                    step([&]() { return monthly_rebalance(end, scenario, current_values); });

                    // Simulate TER
                    step([&]() { return pay_fees(end, scenario, current_values); });

                    // Adjust the withdrawals for inflation
                    withdrawal *= inflation->value;
                    minimum *= inflation->value;
                    scenario.target_value_ *= inflation->value;
                    ++inflation;

                    // Monthly withdrawal
                    step([&]() { return withdraw(months, total_months, scenario, current_values, cash, withdrawn, minimum, withdrawal, starting_value); });
                }

                total_withdrawn += withdrawn;

                // Yearly Rebalance and check for failure
                step([&]() { return yearly_rebalance(months == total_months, scenario, current_values); });

                // Record effective withdrawal rates

                if (failure) {
                    auto eff_wr = withdrawn / starting_value;

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

                // Total amount of money withdrawn
                res.withdrawn_per_year += total_withdrawn;
            } else {
                ++res.failures;
            }

            terminal_values.push_back(final_value);

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

    res.withdrawn_per_year = (res.withdrawn_per_year / scenario.years) / float(res.successes);

    res.highest_eff_wr *= 100.0f;
    res.lowest_eff_wr *= 100.0f;

    res.success_rate = 100 * (res.successes / float(res.successes + res.failures));
    res.compute_terminal_values(terminal_values);

    simulations += terminal_values.size();

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

std::ostream & swr::operator<<(std::ostream& out, const Method & method){
    switch (method) {
        case Method::STANDARD:
            return out << "standard";
        case Method::CURRENT:
            return out << "current";
    }

    return out << "Unknown method";
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
        << " withdraw={" << scenario.withdraw_frequency << "," << scenario.method << "," << scenario.minimum << "}"
        << " fees=" << scenario.fees
        << " soc_sec={" << scenario.social_security << "," << scenario.social_delay << "," << scenario.social_coverage << "}"
        << " gp={" << scenario.glidepath << "," << scenario.gp_pass << " " << scenario.gp_goal << "}"
        << " fin={" << scenario.final_inflation << "," << scenario.final_threshold << "}"
        << " cash={" << scenario.cash_simple << "," << scenario.initial_cash << "}"
        << "}";
    return out;
}
