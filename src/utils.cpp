//=======================================================================G
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <format>
#include <string>
#include <iostream>
#include <string_view>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "data.hpp"
#include "portfolio.hpp"
#include "simulation.hpp"
#include "utils.hpp"

#include "cpp_utils/parallel.hpp"
#include "cpp_utils/thread_pool.hpp"

#include <httplib.h>

std::vector<std::string> swr::parse_args(int argc, const char* argv[]) {
    std::vector<std::string> args;

    for (int i = 0; i < argc - 1; ++i) {
        args.emplace_back(argv[i + 1]);
    }

    return args;
}

bool swr::prepare_exchange_rates(swr::scenario& scenario, const std::string& currency) {
    auto exchange_data     = swr::load_exchange("usd_chf");
    auto inv_exchange_data = swr::load_exchange_inv("usd_chf");

    if (exchange_data.empty() || inv_exchange_data.empty()) {
        return false;
    }

    const size_t N = scenario.portfolio.size();

    scenario.exchange_rates.resize(N);
    scenario.exchange_set.resize(N);

    for (size_t i = 0; i < N; ++i) {
        auto& asset = scenario.portfolio[i].asset;

        if (currency == "usd") {
            if (asset == "ch_stocks" || asset == "ch_bonds") {
                scenario.exchange_set[i]   = true;
                scenario.exchange_rates[i] = inv_exchange_data;
            } else {
                scenario.exchange_set[i]   = false;
                scenario.exchange_rates[i] = scenario.values[i]; // Must copy from values to keep full range
                // We set everything to one
                for (auto& v : scenario.exchange_rates[i]) {
                    v.value = 1.0f;
                }
            }
        } else if (currency == "chf") {
            if (asset == "ch_stocks" || asset == "ch_bonds") {
                scenario.exchange_set[i]   = false;
                scenario.exchange_rates[i] = scenario.values[i]; // Must copy from values to keep full range
                // We set everything to one
                for (auto& v : scenario.exchange_rates[i]) {
                    v.value = 1.0f;
                }
            } else {
                scenario.exchange_set[i]   = true;
                scenario.exchange_rates[i] = exchange_data;
            }
        }
    }

    return true;
}

float swr::percentile(const std::vector<float>& v, size_t p) {
    auto point = v.size() * (p / 100.0f);
    return v[point];
}

std::vector<float> swr::to_yearly_returns(const swr::data_vector& v) {
    std::vector<float> yearly_returns;

    auto year_it  = v.begin();
    auto year_end = v.end();

    while (year_it != year_end) {
        float returns = 1.0f;

        bool skip = false;

        for (size_t month = 0; month < 12; ++month) {
            auto ret = year_it->value;

            returns *= ret;

            ++year_it;

            if (year_it == year_end) {
                skip = true;
                break;
            }
        }

        if (!skip) {
            yearly_returns.push_back(returns);
        }
    }

    std::ranges::sort(yearly_returns);

    return yearly_returns;
}

std::vector<float> swr::to_cagr_returns(const std::vector<swr::allocation>& portfolio, size_t rolling) {
    const size_t N = portfolio.size(); // Number of assets

    auto values = swr::load_values(portfolio);

    const size_t M = values[0].size(); // Number of months

    std::vector<float> current_values(N);

    for (size_t i = 0; i < N; ++i) {
        current_values[i] = 1000.0f * (portfolio[i].allocation / 100.0f);
    }

    std::vector<float> acc_values(M);

    for (size_t m = 0; m < M; ++m) {
        for (size_t i = 0; i < N; ++i) {
            current_values[i] *= values[i][m].value;
        }

        acc_values[m] = std::accumulate(current_values.begin(), current_values.end(), 0.0f);
    }

    std::vector<float> cagr_returns;
    cagr_returns.reserve(M);

    for (size_t m = 0; m + rolling * 12 < M; ++m) {
        auto start_value = acc_values[m];
        auto end_value   = acc_values[m + rolling * 12];
        cagr_returns.push_back(std::powf((end_value / start_value), 1.0f / static_cast<float>(rolling)) - 1.0f);
    }

    std::ranges::sort(cagr_returns);

    return cagr_returns;
}
