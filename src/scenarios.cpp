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
#include "server.hpp"
#include "scenarios.hpp"
#include "graph.hpp"

#include "cpp_utils/parallel.hpp"
#include "cpp_utils/thread_pool.hpp"

namespace {

template <typename T>
void csv_print(const std::string& header, const std::vector<T>& values) {
    std::cout << header;
    for (auto& v : values) {
        std::cout << ";" << v;
    }
    std::cout << "\n";
}

template <typename F>
void multiple_wr_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr, F functor) {
    if (title.empty()) {
        graph.add_legend(portfolio_to_string(scenario, shortForm));
    } else {
        graph.add_legend(title);
    }

    cpp::default_thread_pool pool(2 * std::thread::hardware_concurrency());
    std::map<float, float>   results;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        results[wr] = 0.0f;
    }

    std::atomic<bool> error = false;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        pool.do_task(
                [&results, &scenario, &error, &functor](float wr) {
                    auto my_scenario = scenario;
                    my_scenario.wr   = wr;
                    auto res         = swr::simulation(my_scenario);

                    if (res.error) {
                        error = false;
                        std::cout << "\nERROR: " << res.message << "\n";
                    } else {
                        results[wr] = functor(res, wr);
                    }
                },
                wr);
    }

    pool.wait();

    if (!error) {
        graph.add_data(results);
    }
}

template <typename F>
void multiple_wr_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr, F functor) {
    if (title.empty()) {
        for (const auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                std::cout << position.allocation << "% " << position.asset << " ";
            }
        }
    } else {
        std::cout << title << " ";
    }

    cpp::default_thread_pool pool(2 * std::thread::hardware_concurrency());
    std::vector<float>       results;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        results.push_back(0.0f);
    }

    std::size_t       i     = 0;
    std::atomic<bool> error = false;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        pool.do_task(
                [&results, &scenario, &error, &functor](float wr, size_t i) {
                    auto my_scenario = scenario;
                    my_scenario.wr   = wr;
                    auto res         = swr::simulation(my_scenario);

                    if (res.error) {
                        error = false;
                        std::cout << "\nERROR: " << res.message << "\n";
                    } else {
                        results[i] = functor(res);
                    }
                },
                wr,
                i++);
    }

    pool.wait();

    if (!error) {
        for (auto& res : results) {
            std::cout << ';' << res;
        }
    }

    std::cout << "\n";
}

} // namespace

void swr::multiple_wr(const swr::scenario& scenario) {
    std::cout << "           Portfolio: \n";
    for (const auto& position : scenario.portfolio) {
        std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
    }

    std::cout << "\n";

    cpp::default_thread_pool pool(std::thread::hardware_concurrency());

    std::vector<swr::results> all_yearly_results;
    std::vector<swr::results> all_monthly_results;

    for (float wr = 3.0; wr < 5.1f; wr += 0.25f) {
        all_yearly_results.emplace_back();
        all_monthly_results.emplace_back();
    }

    size_t i = 0;

    for (float wr = 3.0; wr < 5.1f; wr += 0.25f) {
        pool.do_task(
                [&scenario, &all_yearly_results, &all_monthly_results](float wr, size_t i) {
                    auto my_scenario = scenario;

                    my_scenario.wr                 = wr;
                    my_scenario.withdraw_frequency = 12;

                    all_yearly_results[i] = swr::simulation(my_scenario);

                    my_scenario.withdraw_frequency = 1;
                    all_monthly_results[i]         = swr::simulation(my_scenario);
                },
                wr,
                i++);
    }

    pool.wait();

    i = 0;

    for (float wr = 3.0; wr < 5.1f; wr += 0.25f) {
        auto& yearly_results  = all_yearly_results[i];
        auto& monthly_results = all_monthly_results[i];

        std::cout << wr << "% Success Rate (Yearly): (" << yearly_results.successes << "/" << (yearly_results.failures + yearly_results.successes) << ") "
                  << yearly_results.success_rate << "%"
                  << " [" << yearly_results.tv_average << ":" << yearly_results.tv_median << ":" << yearly_results.tv_minimum << ":"
                  << yearly_results.tv_maximum << "]\n";

        if (yearly_results.error) {
            std::cout << "Error in simulation: " << yearly_results.message << "\n";
            return;
        }

        std::cout << wr << "% Success Rate (Monthly): (" << monthly_results.successes << "/" << (monthly_results.failures + monthly_results.successes) << ") "
                  << monthly_results.success_rate << "%"
                  << " [" << monthly_results.tv_average << ":" << monthly_results.tv_median << ":" << monthly_results.tv_minimum << ":"
                  << monthly_results.tv_maximum << "]\n";

        if (monthly_results.error) {
            std::cout << "Error in simulation: " << monthly_results.message << "\n";
            return;
        }

        ++i;
    }
}

std::string swr::asset_to_string(std::string_view asset) {
    if (asset == "ch_stocks") {
        return "CH Stocks";
    } else if (asset == "us_stocks") {
        return "US Stocks";
    } else if (asset == "ex_us_stocks") {
        return "ex-US Stocks";
    } else if (asset == "ch_bonds") {
        return "CH Bonds";
    } else if (asset == "us_bonds") {
        return "US Bonds";
    } else if (asset == "gold") {
        return "Gold";
    } else if (asset == "commodities") {
        return "Commodities";
    } else {
        return std::string(asset);
    }
}

std::string swr::asset_to_string_percent(std::string_view asset) {
    return "% " + asset_to_string(asset);
}

std::string_view swr::asset_to_blog_string(std::string_view asset) {
    if (asset == "ch_stocks") {
        return "Stocks";
    } else if (asset == "us_stocks") {
        return "Stocks";
    } else if (asset == "ex_us_stocks") {
        return "Stocks";
    } else if (asset == "ch_bonds") {
        return "Bonds";
    } else if (asset == "us_bonds") {
        return "Bonds";
    } else if (asset == "gold") {
        return "Gold";
    } else if (asset == "commodities") {
        return "Commodities";
    } else {
        return asset;
    }
}

std::string swr::portfolio_to_blog_string(const swr::scenario& scenario, bool shortForm) {
    std::stringstream ss;
    if (shortForm && scenario.portfolio.size() == 2) {
        const auto& first  = scenario.portfolio.front();
        const auto& second = scenario.portfolio.back();

        if (first.allocation == 0) {
            ss << second.allocation << "% " << asset_to_blog_string(second.asset);
        } else if (second.allocation == 0) {
            ss << first.allocation << "% " << asset_to_blog_string(first.asset);
        } else {
            ss << first.allocation << "% " << asset_to_blog_string(first.asset);
        }
    } else {
        std::string sep;
        for (const auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                ss << sep << position.allocation << "% " << asset_to_blog_string(position.asset);
                sep = " / ";
            }
        }
    }
    return ss.str();
}

std::string swr::portfolio_to_string(const swr::scenario& scenario, bool shortForm) {
    std::stringstream ss;
    if (shortForm && scenario.portfolio.size() == 2) {
        const auto& first  = scenario.portfolio.front();
        const auto& second = scenario.portfolio.back();

        if (first.allocation == 0) {
            ss << second.allocation << asset_to_string_percent(second.asset);
        } else if (second.allocation == 0) {
            ss << first.allocation << asset_to_string_percent(first.asset);
        } else {
            ss << first.allocation << asset_to_string_percent(first.asset);
        }
    } else {
        std::string sep;
        for (const auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                ss << sep << position.allocation << asset_to_string_percent(position.asset);
                sep = " ";
            }
        }
    }
    return ss.str();
}

std::map<float, swr::results> swr::multiple_wr_success_graph_save(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, swr::results> all_results;
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&all_results](const auto& results, float wr) {
        all_results[wr] = results;
        return results.success_rate;
    });
    return all_results;
}

void swr::multiple_wr_success_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [](const auto& results, float) { return results.success_rate; });
}

void swr::multiple_wr_withdrawn_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [](const auto& results, float) { return results.withdrawn_per_year; });
}

void swr::multiple_wr_errors_graph(swr::Graph&                          graph,
                                   std::string_view                     title,
                                   bool                                 shortForm,
                                   const swr::scenario&                 scenario,
                                   float                                start_wr,
                                   float                                end_wr,
                                   float                                add_wr,
                                   const std::map<float, swr::results>& base_results) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&base_results](const auto& results, float wr) {
        const auto& base_result = base_results.at(wr);

        size_t errors = 0;

        for (size_t i = 0; i < results.flexible.size(); ++i) {
            if (results.flexible[i] == 1.0f && base_result.terminal_values[i] > 0.0f) {
                ++errors;
            }
        }

        return static_cast<float>(errors) / float(results.flexible.size());
    });
}

void swr::multiple_wr_duration_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results, float) {
        if (results.failures) {
            return results.worst_duration;
        } else {
            return scenario.years * 12;
        }
    });
}

void swr::multiple_wr_quality_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results, float) {
        if (results.failures) {
            return results.success_rate * (results.worst_duration / (scenario.years * 12.0f));
        } else {
            return 1.0f * results.success_rate;
        }
    });
}

void swr::multiple_wr_success_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [](const auto& results) { return results.success_rate; });
}

void swr::multiple_wr_withdrawn_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [](const auto& results) { return results.withdrawn_per_year; });
}

void swr::multiple_wr_duration_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results) {
        if (results.failures) {
            return results.worst_duration;
        } else {
            return scenario.years * 12;
        }
    });
}

void swr::multiple_wr_tv_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, float> max_tv;
    std::map<float, float> avg_tv;
    std::map<float, float> med_tv;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;

        auto results = swr::simulation(scenario);

        max_tv[wr] = results.tv_maximum;
        avg_tv[wr] = results.tv_average;
        med_tv[wr] = results.tv_median;
    }

    graph.add_legend("MAX");
    graph.add_data(max_tv);

    graph.add_legend("AVG");
    graph.add_data(avg_tv);

    graph.add_legend("MED");
    graph.add_data(med_tv);
}

void swr::multiple_wr_avg_tv_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, swr::results> all_results;
    multiple_wr_graph(graph, "", true, scenario, start_wr, end_wr, add_wr, [&all_results](const auto& results, float wr) {
        all_results[wr] = results;
        return results.tv_average;
    });
}

void swr::multiple_wr_tv_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::vector<float> min_tv;
    std::vector<float> max_tv;
    std::vector<float> avg_tv;
    std::vector<float> med_tv;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);
        min_tv.push_back(monthly_results.tv_minimum);
        max_tv.push_back(monthly_results.tv_maximum);
        avg_tv.push_back(monthly_results.tv_average);
        med_tv.push_back(monthly_results.tv_median);
    }

    csv_print("MIN", min_tv);
    csv_print("AVG", avg_tv);
    csv_print("MED", med_tv);
    csv_print("MAX", max_tv);
}

void swr::multiple_wr_spending_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, float> max_spending;
    std::map<float, float> min_spending;
    std::map<float, float> avg_spending;
    std::map<float, float> med_spending;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;

        auto results = swr::simulation(scenario);

        max_spending[wr] = results.spending_maximum;
        min_spending[wr] = results.spending_minimum;
        avg_spending[wr] = results.spending_average;
        med_spending[wr] = results.spending_median;
    }

    graph.add_legend("MAX");
    graph.add_data(max_spending);

    graph.add_legend("MIN");
    graph.add_data(min_spending);

    graph.add_legend("AVG");
    graph.add_data(avg_spending);

    graph.add_legend("MED");
    graph.add_data(med_spending);
}

void swr::multiple_wr_spending_trend_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, float> small_spending;
    std::map<float, float> large_spending;
    std::map<float, float> volatile_up_spending;
    std::map<float, float> volatile_down_spending;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;

        auto results = swr::simulation(scenario);

        small_spending[wr]         = 100.0f * (results.years_small_spending / static_cast<float>(results.successes * scenario.years));
        large_spending[wr]         = 100.0f * (results.years_large_spending / static_cast<float>(results.successes * scenario.years));
        volatile_up_spending[wr]   = 100.0f * (results.years_volatile_up_spending / static_cast<float>(results.successes * scenario.years));
        volatile_down_spending[wr] = 100.0f * (results.years_volatile_down_spending / static_cast<float>(results.successes * scenario.years));
    }

    graph.add_legend("Small Spending Years");
    graph.add_data(small_spending);

    graph.add_legend("Large Spending Years");
    graph.add_data(large_spending);

    graph.add_legend("Volatile Up Years");
    graph.add_data(volatile_up_spending);

    graph.add_legend("Volatile Down Years");
    graph.add_data(volatile_down_spending);
}

void swr::multiple_wr_spending_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::vector<float> min_spending;
    std::vector<float> max_spending;
    std::vector<float> avg_spending;
    std::vector<float> med_spending;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);
        min_spending.push_back(monthly_results.spending_minimum);
        max_spending.push_back(monthly_results.spending_maximum);
        avg_spending.push_back(monthly_results.spending_average);
        med_spending.push_back(monthly_results.spending_median);
    }

    csv_print("MIN", min_spending);
    csv_print("AVG", avg_spending);
    csv_print("MED", med_spending);
    csv_print("MAX", max_spending);
}

float swr::failsafe_swr_one(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal) {
    for (float wr = start_wr; wr >= end_wr; wr -= step) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);

        if (monthly_results.success_rate >= 100.0f - goal) {
            return wr;
        }
    }

    return 0.0f;
}

void swr::failsafe_swr(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal, std::ostream& out) {
    for (float wr = start_wr; wr >= end_wr; wr -= step) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);

        if (monthly_results.success_rate >= 100.0f - goal) {
            out << std::format(";{:.2f}", wr);
            return;
        }
    }

    out << ";0";
}

void swr::failsafe_swr(std::string_view title, swr::scenario& scenario, float start_wr, float end_wr, float step, std::ostream& out) {
    if (title.empty()) {
        out << portfolio_to_string(scenario, true);
    } else {
        out << title << " ";
    }

    failsafe_swr(scenario, start_wr, end_wr, step, 0.0f, out);
    failsafe_swr(scenario, start_wr, end_wr, step, 1.0f, out);
    failsafe_swr(scenario, start_wr, end_wr, step, 5.0f, out);
    failsafe_swr(scenario, start_wr, end_wr, step, 10.0f, out);
    failsafe_swr(scenario, start_wr, end_wr, step, 25.0f, out);
    out << '\n';
}

void swr::multiple_rebalance_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    if (scenario.rebalance == swr::Rebalancing::THRESHOLD) {
        std::cout << scenario.threshold << " ";
    } else {
        std::cout << scenario.rebalance << " ";
    }

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);
        std::cout << ';' << monthly_results.success_rate;
    }

    std::cout << "\n";
}

void swr::multiple_rebalance_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, float> data;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr  = wr;
        auto results = swr::simulation(scenario);
        data[wr]     = results.success_rate;
    }

    if (scenario.rebalance == swr::Rebalancing::THRESHOLD) {
        graph.add_legend(std::to_string(static_cast<uint32_t>(scenario.threshold * 100)) + "%");
    } else if (scenario.rebalance == swr::Rebalancing::NONE) {
        graph.add_legend("None");
    } else if (scenario.rebalance == swr::Rebalancing::MONTHLY) {
        graph.add_legend("Monthly");
    } else if (scenario.rebalance == swr::Rebalancing::YEARLY) {
        graph.add_legend("Yearly");
    } else {
        graph.add_legend("Error");
    }

    graph.add_data(data);
}

void swr::configure_withdrawal_method(swr::scenario& scenario, std::vector<std::string> args, size_t n) {
    if (args.size() > n) {
        if (args[n] == "fixed") {
            scenario.wmethod = swr::WithdrawalMethod::STANDARD;
        } else if (args[n] == "current") {
            scenario.wmethod = swr::WithdrawalMethod::CURRENT;
            scenario.minimum = 0.04f;
        } else if (args[n] == "vanguard") {
            scenario.wmethod = swr::WithdrawalMethod::VANGUARD;
            scenario.minimum = 0.04f;
        } else if (args[n] == "current3") {
            scenario.wmethod = swr::WithdrawalMethod::CURRENT;
            scenario.minimum = 0.03f;
        } else if (args[n] == "vanguard3") {
            scenario.wmethod = swr::WithdrawalMethod::VANGUARD;
            scenario.minimum = 0.03f;
        } else {
            std::cout << "No support for method: " << args[n] << "\n";
        }
    } else {
        scenario.wmethod = swr::WithdrawalMethod::STANDARD;
    }
}
