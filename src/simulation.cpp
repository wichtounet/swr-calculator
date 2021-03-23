#include "simulation.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <sstream>

namespace {

size_t simulations = 0;

// In percent
constexpr const float monthly_rebalancing_cost   = 0.005;
constexpr const float yearly_rebalancing_cost    = 0.01;
constexpr const float threshold_rebalancing_cost = 0.01;

bool valid_year(const std::vector<swr::data> & data, size_t year) {
    return year >= data.front().year && year <= data.back().year;
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

swr::results swr::simulation(scenario & scenario) {
    auto & inflation_data = scenario.inflation_data;
    auto & values = scenario.values;

    // The final results
    swr::results res;

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

    if (!valid_year(inflation_data, scenario.start_year) && !valid_year(inflation_data, scenario.end_year)) {
        res.message = "The given period is out of the historical data, it's either too far in the future or too far in the past";
        res.error = true;
        return res;
    }

    for (auto& v : values) {
        if (!valid_year(v, scenario.start_year) && !valid_year(v, scenario.end_year)) {
            res.message = "The given period is out of the historical data, it's either too far in the future or too far in the past";
            res.error = true;
            return res;
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

    if (scenario.end_year - scenario.start_year < scenario.years) {
        std::stringstream ss;
        ss << "The period is too short for a " << scenario.years << " years simulation. "
           << "The number of years has been reduced to "
           << (scenario.end_year - scenario.start_year);
        res.message += ss.str();

        scenario.years = scenario.end_year - scenario.start_year;
    }

    const size_t months           = scenario.years * 12;
    const size_t number_of_assets = scenario.portfolio.size();
    const float start_value       = 1000.0f;

    // 3. Do the actual simulation

    std::vector<float> terminal_values;
    std::vector<std::vector<swr::data>::const_iterator> returns(number_of_assets);

    for (size_t current_year = scenario.start_year; current_year <= scenario.end_year - scenario.years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            size_t end_year  = current_year + (current_month - 1 + months - 1) / 12;
            size_t end_month = 1 + ((current_month - 1) + (months - 1) % 12) % 12;

            // The amount of money withdrawn per year
            float withdrawal = start_value * scenario.wr / 100.0f;

            std::vector<float> current_values(number_of_assets);

            for (size_t i = 0; i < number_of_assets; ++i) {
                current_values[i] = start_value * (scenario.portfolio[i].allocation / 100.0f);
                returns[i]        = swr::get_start(values[i], current_year, (current_month % 12) + 1);
            }

            auto inflation = swr::get_start(inflation_data, current_year, (current_month % 12) + 1);

            size_t months = 1;

            for (size_t y = current_year; y <= end_year; ++y) {
                auto starting_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);
                float withdrawed = 0.0f;

                for (size_t m = (y == current_year ? current_month : 1); m <= (y == end_year ? end_month : 12); ++m, ++months) {
                    // Adjust the portfolio with the returns
                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] *= returns[i]->value;
                        ++returns[i];
                    }

                    // Monthly Rebalance if necessary
                    if (scenario.rebalance == Rebalancing::MONTHLY) {
                        // Pay the fees
                        for (size_t i = 0; i < number_of_assets; ++i) {
                            current_values[i] *= 1.0f - monthly_rebalancing_cost / 100.0f;
                        }

                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        for (size_t i = 0; i < number_of_assets; ++i) {
                            current_values[i] = total_value * (scenario.portfolio[i].allocation / 100.0f);
                        }
                    }

                    // Threshold Rebalance if necessary
                    if (scenario.rebalance == Rebalancing::THRESHOLD) {
                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        bool rebalance = false;
                        for (size_t i = 0; i < number_of_assets; ++i) {
                            if (std::abs((scenario.portfolio[i].allocation / 100.0f) - current_values[i] / total_value) >= scenario.threshold) {
                                rebalance = true;
                                break;
                            }
                        }

                        if (rebalance) {
                            // Pay the fees
                            for (size_t i = 0; i < number_of_assets; ++i) {
                                current_values[i] *= 1.0f - threshold_rebalancing_cost / 100.0f;
                            }

                            for (size_t i = 0; i < number_of_assets; ++i) {
                                current_values[i] = total_value * (scenario.portfolio[i].allocation / 100.0f);
                            }
                        }
                    }

                    // Simulate TER
                    if (scenario.fees > 0.0f) {
                        for (size_t i = 0; i < number_of_assets; ++i) {
                            current_values[i] *= 1.0f - (scenario.fees / 12.0f);
                        }
                    }

                    // Adjust the withdrawal for inflation
                    withdrawal *= inflation->value;
                    ++inflation;

                    // Withdraw money from the portfolio
                    if ((months - 1) % scenario.withdraw_frequency == 0) {
                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        float withdrawal_amount = withdrawal / (12.0f / scenario.withdraw_frequency);
                        withdrawed += withdrawal_amount;

                        if (total_value > 0.0f) {
                            for (auto& value : current_values) {
                                value = std::max(0.0f, value - (value / total_value) * withdrawal_amount);
                            }

                            if (total_value - withdrawal_amount <= 0.0f) {
                                // Record the worst duration
                                if (!res.worst_duration || months < res.worst_duration) {
                                    res.worst_duration       = months;
                                    res.worst_starting_month = current_month;
                                    res.worst_starting_year  = current_year;
                                }
                            }
                        }
                    }
                }

                // Yearly Rebalance if necessary
                if (scenario.rebalance == Rebalancing::YEARLY) {
                    // Pay the fees
                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] *= 1.0f - yearly_rebalancing_cost / 100.0f;
                    }

                    auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] = total_value * (scenario.portfolio[i].allocation / 100.0f);
                    }
                }

                // Record effective withdrawal rates

                auto final_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                if (final_value > 0.0f) {
                    auto eff_wr = withdrawed / starting_value;

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
                }
            }

            auto final_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

            if (final_value > 0.0f) {
                ++res.successes;
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
        }
    }

    res.highest_eff_wr *= 100.0f;
    res.lowest_eff_wr *= 100.0f;

    res.success_rate = 100 * (res.successes / float(res.successes + res.failures));
    res.compute_terminal_values(terminal_values);

    simulations += terminal_values.size();

    return res;
}

void swr::results::compute_terminal_values(std::vector<float> & terminal_values) {
    std::sort(terminal_values.begin(), terminal_values.end());

    tv_median  = terminal_values[terminal_values.size() / 2 + 1];
    tv_minimum = terminal_values.front();
    tv_maximum = terminal_values.back();
    tv_average = std::accumulate(terminal_values.begin(), terminal_values.end(), 0.0f) / terminal_values.size();
}

size_t swr::simulations_ran() {
    return simulations;
}
