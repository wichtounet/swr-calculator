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

#include "cpp_utils/parallel.hpp"
#include "cpp_utils/thread_pool.hpp"

#include <httplib.h>

namespace {

std::vector<std::string> parse_args(int argc, const char* argv[]) {
    std::vector<std::string> args;

    for (int i = 0; i < argc - 1; ++i) {
        args.emplace_back(argv[i + 1]);
    }

    return args;
}

void multiple_wr(const swr::scenario& scenario) {
    std::cout << "           Portfolio: \n";
    for (auto& position : scenario.portfolio) {
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
                  << yearly_results.tv_maximum << "]" << std::endl;

        if (yearly_results.error) {
            std::cout << "Error in simulation: " << yearly_results.message << std::endl;
            return;
        }

        std::cout << wr << "% Success Rate (Monthly): (" << monthly_results.successes << "/" << (monthly_results.failures + monthly_results.successes) << ") "
                  << monthly_results.success_rate << "%"
                  << " [" << monthly_results.tv_average << ":" << monthly_results.tv_median << ":" << monthly_results.tv_minimum << ":"
                  << monthly_results.tv_maximum << "]" << std::endl;

        if (monthly_results.error) {
            std::cout << "Error in simulation: " << monthly_results.message << std::endl;
            return;
        }

        ++i;
    }
}

struct GraphBase {
    explicit GraphBase(bool enabled, std::string_view ytitle, std::string_view graph) : enabled_(enabled), graph_(graph), yitle_(ytitle) {}

    template <typename T1, typename T2>
    void dump_labels(const std::vector<std::map<T1, T2>>& data) {
        cpp_assert(data.size(), "data cannot be empty");

        std::string sep = "";
        for (auto& [key, value] : data.front()) {
            std::cout << sep << key;
            sep = ",";
        }
        std::cout << "|,\"series\":|";

        std::string serie_sep;

        for (auto& serie : data) {
            std::cout << serie_sep << "|";
            sep = "";
            for (auto& [key, value] : serie) {
                std::cout << sep << value;
                sep = ",";
            }
            std::cout << "|";
            serie_sep = ",";
        }
    }

    template <typename T1, typename T2>
    void dump_graph(const std::vector<std::map<T1, T2>>& data) {
        if (enabled_ && !flushed_) {
            std::cout << "[" << graph_ << " title=\"" << title_ << "\" ytitle=\"" << yitle_ << "\" xtitle=\"" << xtitle_ << "\"";

            if (legends_.empty()) {
                std::cout << "]";

                extra_ += "\"legend_position\":\"none\", ";
            } else {
                std::stringstream legends;
                std::string       sep;
                for (auto& legend : legends_) {
                    legends << sep << legend;
                    sep = ",";
                }
                std::cout << " legends=\"" << legends.str() << "\"]";
            }
            std::cout << "{" << extra_ << "\"labels\":|";
            dump_labels(data);
            std::cout << "|}[/" << graph_ << "]";
            std::cout << '\n';

            flushed_ = true;
        }
    }

    void add_legend(std::string_view title) {
        legends_.emplace_back(title);
    }

    void set_extra(std::string_view extra) {
        extra_ = extra;
    }

    const bool               enabled_;
    const std::string        graph_;
    const std::string        yitle_;
    std::string              xtitle_ = "Withdrawal Rate (%)";
    std::string              title_  = "TODO";
    std::string              extra_;
    std::vector<std::string> legends_;
    bool                     flushed_ = false;
};

struct Graph : GraphBase {
    explicit Graph(bool enabled, std::string_view ytitle = "Success Rate (%)", std::string_view graph = "line-graph") : GraphBase(enabled, ytitle, graph) {}

    ~Graph() {
        flush();
    }

    // We don't use JSON, but a form of retarded JSON for WPML to handle
    void flush() {
        dump_graph(data_);
    }

    void add_data(const std::map<float, float>& data) {
        data_.emplace_back(data);
    }

    std::vector<std::map<float, float>> data_;
};

struct TimeGraph : GraphBase {
    explicit TimeGraph(bool enabled, std::string_view ytitle = "Success Rate (%)", std::string_view graph = "line-graph") : GraphBase(enabled, ytitle, graph) {}

    ~TimeGraph() {
        flush();
    }

    // We don't use JSON, but a form of retarded JSON for WPML to handle
    void flush() {
        extra_ += "\"x_data_type\":\"time\", ";
        dump_graph(data_);
    }

    void add_data(const std::map<int64_t, float>& data) {
        data_.emplace_back(data);
    }

    std::vector<std::map<int64_t, float>> data_;
};

std::string asset_to_string(std::string_view asset) {
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

std::string asset_to_string_percent(std::string_view asset) {
    return "% " + asset_to_string(asset);
}

std::string_view asset_to_blog_string(std::string_view asset) {
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

std::string portfolio_to_blog_string(const swr::scenario& scenario, bool shortForm) {
    std::stringstream ss;
    if (shortForm && scenario.portfolio.size() == 2) {
        auto& first  = scenario.portfolio.front();
        auto& second = scenario.portfolio.back();

        if (first.allocation == 0) {
            ss << second.allocation << "% " << asset_to_blog_string(second.asset);
        } else if (second.allocation == 0) {
            ss << first.allocation << "% " << asset_to_blog_string(first.asset);
        } else {
            ss << first.allocation << "% " << asset_to_blog_string(first.asset);
        }
    } else {
        std::string sep;
        for (auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                ss << sep << position.allocation << "% " << asset_to_blog_string(position.asset);
                sep = " / ";
            }
        }
    }
    return ss.str();
}

std::string portfolio_to_string(const swr::scenario& scenario, bool shortForm) {
    std::stringstream ss;
    if (shortForm && scenario.portfolio.size() == 2) {
        auto& first  = scenario.portfolio.front();
        auto& second = scenario.portfolio.back();

        if (first.allocation == 0) {
            ss << second.allocation << asset_to_string_percent(second.asset);
        } else if (second.allocation == 0) {
            ss << first.allocation << asset_to_string_percent(first.asset);
        } else {
            ss << first.allocation << asset_to_string_percent(first.asset);
        }
    } else {
        std::string sep;
        for (auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                ss << sep << position.allocation << asset_to_string_percent(position.asset);
                sep = " ";
            }
        }
    }
    return ss.str();
}

template <typename F>
void multiple_wr_graph(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr, F functor) {
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
                        std::cout << std::endl << "ERROR: " << res.message << std::endl;
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
        for (auto& position : scenario.portfolio) {
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
                        std::cout << std::endl << "ERROR: " << res.message << std::endl;
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

std::map<float, swr::results> multiple_wr_success_graph_save(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, swr::results> all_results;
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&all_results](const auto& results, float wr) {
        all_results[wr] = results;
        return results.success_rate;
    });
    return all_results;
}

void multiple_wr_success_graph(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [](const auto& results, float) { return results.success_rate; });
}

void multiple_wr_withdrawn_graph(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [](const auto& results, float) { return results.withdrawn_per_year; });
}

void multiple_wr_errors_graph(Graph&                               graph,
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

        return float(errors) / float(results.flexible.size());
    });
}

void multiple_wr_duration_graph(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results, float) {
        if (results.failures) {
            return results.worst_duration;
        } else {
            return scenario.years * 12;
        }
    });
}

void multiple_wr_quality_graph(
        Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_graph(graph, title, shortForm, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results, float) {
        if (results.failures) {
            return results.success_rate * (results.worst_duration / (scenario.years * 12.0f));
        } else {
            return 1.0f * results.success_rate;
        }
    });
}

void multiple_wr_success_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [](const auto& results) { return results.success_rate; });
}

void multiple_wr_withdrawn_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [](const auto& results) { return results.withdrawn_per_year; });
}

void multiple_wr_duration_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr) {
    multiple_wr_sheets(title, scenario, start_wr, end_wr, add_wr, [&scenario](const auto& results) {
        if (results.failures) {
            return results.worst_duration;
        } else {
            return scenario.years * 12;
        }
    });
}

template <typename T>
void csv_print(const std::string& header, const std::vector<T>& values) {
    std::cout << header;
    for (auto& v : values) {
        std::cout << ";" << v;
    }
    std::cout << "\n";
}

void multiple_wr_tv_graph(Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

void multiple_wr_avg_tv_graph(Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, swr::results> all_results;
    multiple_wr_graph(graph, "", true, scenario, start_wr, end_wr, add_wr, [&all_results](const auto& results, float wr) {
        all_results[wr] = results;
        return results.tv_average;
    });
}

void multiple_wr_tv_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

void multiple_wr_spending_graph(Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

void multiple_wr_spending_trend_graph(Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
    std::map<float, float> small_spending;
    std::map<float, float> large_spending;
    std::map<float, float> volatile_up_spending;
    std::map<float, float> volatile_down_spending;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;

        auto results = swr::simulation(scenario);

        small_spending[wr]         = 100.0f * (results.years_small_spending / float(results.successes * scenario.years));
        large_spending[wr]         = 100.0f * (results.years_large_spending / float(results.successes * scenario.years));
        volatile_up_spending[wr]   = 100.0f * (results.years_volatile_up_spending / float(results.successes * scenario.years));
        volatile_down_spending[wr] = 100.0f * (results.years_volatile_down_spending / float(results.successes * scenario.years));
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

void multiple_wr_spending_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

float failsafe_swr_one(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal) {
    for (float wr = start_wr; wr >= end_wr; wr -= step) {
        scenario.wr          = wr;
        auto monthly_results = swr::simulation(scenario);

        if (monthly_results.success_rate >= 100.0f - goal) {
            return wr;
        }
    }

    return 0.0f;
}

void failsafe_swr(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal, std::ostream& out) {
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

void failsafe_swr(std::string_view title, swr::scenario& scenario, float start_wr, float end_wr, float step, std::ostream& out) {
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

void multiple_rebalance_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

void multiple_rebalance_graph(Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr) {
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

httplib::Server* server_ptr = nullptr;

void server_signal_handler(int signum) {
    std::cout << "Received signal (" << signum << ")" << std::endl;

    if (server_ptr) {
        server_ptr->stop();
    }
}

void install_signal_handler() {
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags   = 0;
    action.sa_handler = server_signal_handler;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    std::cout << "Installed the signal handler" << std::endl;
}

bool check_parameters(const httplib::Request& req, httplib::Response& res, std::vector<const char*> parameters) {
    using namespace std::string_literals;
    for (auto& param : parameters) {
        if (!req.has_param(param)) {
            res.set_content("{\"results\":{\"message\": \"Missing parameter "s + param + "\",\"error\": true,}}", "text/json");
            return false;
        }
    }

    return true;
}

bool prepare_exchange_rates(swr::scenario& scenario, const std::string& currency) {
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

void configure_withdrawal_method(swr::scenario& scenario, std::vector<std::string> args, size_t n) {
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
            std::cout << "No support for method: " << args[n] << std::endl;
        }
    } else {
        scenario.wmethod = swr::WithdrawalMethod::STANDARD;
    }
}

float percentile(const std::vector<float>& v, size_t p) {
    auto point = v.size() * (p / 100.0f);
    return v[point];
}

std::vector<float> to_yearly_returns(const swr::data_vector& v) {
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

std::vector<float> to_cagr_returns(const std::vector<swr::allocation>& portfolio, size_t rolling) {
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
        cagr_returns.push_back(std::powf((end_value / start_value), 1.0f / float(rolling)) - 1.0f);
    }

    std::ranges::sort(cagr_returns);

    return cagr_returns;
}

void server_simple_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(req, res, {"inflation", "years", "wr", "start", "end"})) {
        return;
    }

    if (!req.has_param("portfolio")) {
        if (!check_parameters(req, res, {"p_us_stocks", "p_us_bonds", "p_commodities", "p_gold", "p_cash", "p_ex_us_stocks"})) {
            return;
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    swr::scenario scenario;

    // Let the simulation find the period if necessary
    scenario.strict_validation = false;

    // Don't run for too long
    scenario.timeout_msecs = 200;

    auto inflation = req.get_param_value("inflation");
    if (req.has_param("inflation2")) {
        inflation = req.get_param_value("inflation2");
    }

    // Parse the parameters
    scenario.wr         = atof(req.get_param_value("wr").c_str());
    scenario.years      = atoi(req.get_param_value("years").c_str());
    scenario.start_year = atoi(req.get_param_value("start").c_str());
    scenario.end_year   = atoi(req.get_param_value("end").c_str());

    // Parse the portfolio
    std::string portfolio_base;
    if (req.has_param("portfolio")) {
        portfolio_base     = req.get_param_value("portfolio");
        scenario.portfolio = swr::parse_portfolio(portfolio_base, false);
    } else {
        portfolio_base     = std::format("us_stocks:{};us_bonds:{};commodities:{};gold:{};cash:{};ex_us_stocks:{};ch_stocks:{};ch_bonds:{};",
                                     req.get_param_value("p_us_stocks"),
                                     req.get_param_value("p_us_bonds"),
                                     req.get_param_value("p_commodities"),
                                     req.get_param_value("p_gold"),
                                     req.get_param_value("p_cash"),
                                     req.get_param_value("p_ex_us_stocks"),
                                     req.get_param_value("p_ch_stocks"),
                                     req.get_param_value("p_ch_bonds"));
        scenario.portfolio = swr::parse_portfolio(portfolio_base, false);
    }

    // Parse the optional parameters

    if (req.has_param("rebalance")) {
        auto param = req.get_param_value("rebalance");

        if (param == "none") {
            scenario.rebalance = swr::Rebalancing::NONE;
        } else if (param == "monthly") {
            scenario.rebalance = swr::Rebalancing::MONTHLY;
        } else if (param == "yearly") {
            scenario.rebalance = swr::Rebalancing::YEARLY;
        } else if (param == "threshold") {
            scenario.rebalance = swr::Rebalancing::THRESHOLD;
        } else {
            scenario.rebalance = swr::Rebalancing::NONE;
        }
    } else {
        scenario.rebalance = swr::Rebalancing::NONE;
    }

    if (req.has_param("rebalance_threshold")) {
        scenario.threshold = atof(req.get_param_value("rebalance_threshold").c_str()) / 100.0f;
    } else {
        scenario.threshold = 0.01;
    }

    if (req.has_param("initial")) {
        scenario.initial_value = atof(req.get_param_value("initial").c_str());
    } else {
        scenario.initial_value = 1000.0f;
    }

    if (req.has_param("fees")) {
        scenario.fees = atof(req.get_param_value("fees").c_str()) / 100.0f;
    } else {
        scenario.fees = 0.001; // 0.1% fees
    }

    if (req.has_param("final_threshold")) {
        scenario.final_threshold = atof(req.get_param_value("final_threshold").c_str()) / 100.0f;
    } else {
        scenario.final_threshold = 0.0f; // 0.1% fees
    }

    if (req.has_param("final_inflation")) {
        scenario.final_inflation = req.get_param_value("final_inflation") == "true";
    } else {
        scenario.final_inflation = true;
    }

    if (req.has_param("social_security")) {
        scenario.social_security = req.get_param_value("social_security") == "true";
    } else {
        scenario.social_security = false;
    }

    if (req.has_param("social_delay")) {
        scenario.social_delay = atof(req.get_param_value("social_delay").c_str());
    } else {
        scenario.social_delay = 0;
    }

    if (req.has_param("social_coverage")) {
        scenario.social_coverage = atof(req.get_param_value("social_coverage").c_str()) / 100.0f;
    } else {
        scenario.social_coverage = 0;
    }

    if (req.has_param("social_amount")) {
        scenario.social_amount = atof(req.get_param_value("social_amount").c_str());
    } else {
        scenario.social_amount = 0;
    }

    if (req.has_param("withdraw_frequency")) {
        scenario.withdraw_frequency = atoi(req.get_param_value("withdraw_frequency").c_str());
    } else {
        scenario.withdraw_frequency = 12;
    }

    if (req.has_param("withdraw_minimum")) {
        scenario.minimum = atof(req.get_param_value("withdraw_minimum").c_str()) / 100.0f;
    } else {
        scenario.minimum = 0.03;
    }

    if (req.has_param("withdraw_method")) {
        scenario.wmethod = req.get_param_value("withdraw_method") == "current" ? swr::WithdrawalMethod::CURRENT : swr::WithdrawalMethod::STANDARD;
    } else {
        scenario.wmethod = swr::WithdrawalMethod::STANDARD;
    }

    if (req.has_param("initial_cash")) {
        scenario.initial_cash = atof(req.get_param_value("initial_cash").c_str());
    } else {
        scenario.initial_cash = 0.0f;
    }

    if (req.has_param("cash_method")) {
        scenario.cash_simple = req.get_param_value("cash_method") == "smart" ? false : true;
    } else {
        scenario.cash_simple = true;
    }

    if (req.has_param("gp")) {
        scenario.glidepath = req.get_param_value("gp") == "true";
    } else {
        scenario.glidepath = false;
    }

    if (req.has_param("gp_pass")) {
        scenario.gp_pass = atof(req.get_param_value("gp_pass").c_str());
    } else {
        scenario.gp_pass = 0.0f;
    }

    if (req.has_param("gp_goal")) {
        scenario.gp_goal = atof(req.get_param_value("gp_goal").c_str());
    } else {
        scenario.gp_goal = 0.0f;
    }

    if (req.has_param("extra_income_amount")) {
        scenario.extra_income_amount = atof(req.get_param_value("extra_income_amount").c_str());
        scenario.extra_income        = scenario.extra_income_amount > 0.0f;
    } else {
        scenario.extra_income = false;
    }

    std::string currency = "usd";
    if (req.has_param("currency")) {
        currency = req.get_param_value("currency") == "chf" ? "chf" : "usd";
    }

    std::cout << "DEBUG: Request " << scenario << std::endl;

    swr::normalize_portfolio(scenario.portfolio);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    if (scenario.values.empty()) {
        res.set_content("{\"results\": {\"message\":\"Error: Invalid portfolio\", \"error\": true}}", "text/json");
        return;
    }

    if (scenario.inflation_data.empty()) {
        res.set_content("{\"results\": {\"message\":\"Error: Invalid inflation\", \"error\": true}}", "text/json");
        return;
    }

    if (!prepare_exchange_rates(scenario, currency)) {
        res.set_content("{\"results\": {\"message\":\"Error: Invalid exchange data\", \"error\": true}}", "text/json");
        return;
    }

    auto results = simulation(scenario);

    std::cout << "DEBUG: Response"
              << " error=" << results.error << " message=" << results.message << " success_rate=" << results.success_rate << std::endl;

    std::stringstream ss;

    ss << "{ \"results\": {\n";
    ss << "  \"successes\": " << results.successes << ",\n";
    ss << "  \"failures\": " << results.failures << ",\n";
    ss << "  \"success_rate\": " << results.success_rate << ",\n";
    ss << "  \"tv_average\": " << results.tv_average << ",\n";
    ss << "  \"tv_minimum\": " << results.tv_minimum << ",\n";
    ss << "  \"tv_maximum\": " << results.tv_maximum << ",\n";
    ss << "  \"tv_median\": " << results.tv_median << ",\n";
    ss << "  \"worst_duration\": " << results.worst_duration << ",\n";
    ss << "  \"worst_starting_month\": " << results.worst_starting_month << ",\n";
    ss << "  \"worst_starting_year\": " << results.worst_starting_year << ",\n";
    ss << "  \"worst_tv\": " << results.worst_tv << ",\n";
    ss << "  \"worst_tv_month\": " << results.worst_tv_month << ",\n";
    ss << "  \"worst_tv_year\": " << results.worst_tv_year << ",\n";
    ss << "  \"best_tv\": " << results.best_tv << ",\n";
    ss << "  \"best_tv_month\": " << results.best_tv_month << ",\n";
    ss << "  \"best_tv_year\": " << results.best_tv_year << ",\n";
    ss << "  \"withdrawn_per_year\": " << results.withdrawn_per_year << ",\n";
    ss << "  \"spending_average\": " << results.spending_average << ",\n";
    ss << "  \"spending_minimum\": " << results.spending_minimum << ",\n";
    ss << "  \"spending_maximum\": " << results.spending_maximum << ",\n";
    ss << "  \"spending_median\": " << results.spending_median << ",\n";
    ss << "  \"years_large_spending\": " << results.years_large_spending << ",\n";
    ss << "  \"years_small_spending\": " << results.years_small_spending << ",\n";
    ss << "  \"years_volatile_up_spending\": " << results.years_volatile_up_spending << ",\n";
    ss << "  \"years_volatile_down_spending\": " << results.years_volatile_down_spending << ",\n";
    ss << "  \"message\": \"" << results.message << "\",\n";
    ss << "  \"error\": " << (results.error ? "true" : "false") << "\n";
    ss << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop     = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms" << std::endl;
}

void server_retirement_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(req, res, {"expenses", "income", "wr", "sr", "nw"})) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    swr::scenario scenario;

    // Don't run for too long
    scenario.timeout_msecs = 200;

    // Parse the parameters
    scenario.wr    = atof(req.get_param_value("wr").c_str());
    float sr       = atof(req.get_param_value("sr").c_str());
    float income   = atoi(req.get_param_value("income").c_str());
    float expenses = atoi(req.get_param_value("expenses").c_str());
    float nw       = atoi(req.get_param_value("nw").c_str());

    // Parse the optional parameters

    if (req.has_param("rebalance")) {
        auto param = req.get_param_value("rebalance");

        if (param == "none") {
            scenario.rebalance = swr::Rebalancing::NONE;
        } else if (param == "monthly") {
            scenario.rebalance = swr::Rebalancing::MONTHLY;
        } else if (param == "yearly") {
            scenario.rebalance = swr::Rebalancing::YEARLY;
        } else {
            scenario.rebalance = swr::Rebalancing::NONE;
        }
    } else {
        scenario.rebalance = swr::Rebalancing::NONE;
    }

    float returns = 7.0f;

    std::cout << "DEBUG: Retirement Request wr=" << scenario.wr << " sr=" << sr << " nw=" << nw << " income=" << income << " expenses=" << expenses
              << " rebalance=" << scenario.rebalance << std::endl;

    float fi_number = expenses * (100.0f / scenario.wr);

    size_t months = 0;
    if (nw < fi_number && !income) {
        months = 12 * 1000;
    } else {
        while (nw < fi_number && months < 1200) {
            nw *= 1.0f + (returns / 100.0f) / 12.0f;
            nw += (income * sr / 100.0f) / 12.0f;
            ++months;
        }
    }

    // For now cannot be configured
    scenario.withdraw_frequency = 12;
    scenario.threshold          = 0.0f;
    scenario.start_year         = 1871;
    scenario.end_year           = 2022;

    auto portfolio_100 = swr::parse_portfolio("us_stocks:100;", false);
    auto values_100    = swr::load_values(portfolio_100);

    auto portfolio_60 = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    auto values_60    = swr::load_values(portfolio_60);

    auto portfolio_40 = swr::parse_portfolio("us_stocks:40;us_bonds:60;", false);
    auto values40     = swr::load_values(portfolio_40);

    scenario.inflation_data = swr::load_inflation(values_100, "us_inflation");

    scenario.portfolio = portfolio_100;
    scenario.values    = values_100;
    prepare_exchange_rates(scenario, "usd");

    scenario.years      = 30;
    auto results_30_100 = simulation(scenario);
    scenario.years      = 40;
    auto results_40_100 = simulation(scenario);
    scenario.years      = 50;
    auto results_50_100 = simulation(scenario);

    scenario.portfolio = portfolio_60;
    scenario.values    = values_60;
    prepare_exchange_rates(scenario, "usd");

    scenario.years     = 30;
    auto results_30_60 = simulation(scenario);
    scenario.years     = 40;
    auto results_40_60 = simulation(scenario);
    scenario.years     = 50;
    auto results_50_60 = simulation(scenario);

    scenario.portfolio = portfolio_40;
    scenario.values    = values40;
    prepare_exchange_rates(scenario, "usd");

    scenario.years     = 30;
    auto results_30_40 = simulation(scenario);
    scenario.years     = 40;
    auto results_40_40 = simulation(scenario);
    scenario.years     = 50;
    auto results_50_40 = simulation(scenario);

    bool        error = false;
    std::string message;
    if (results_50_40.error) {
        error   = true;
        message = results_50_40.message;
    } else if (results_40_40.error) {
        error   = true;
        message = results_40_40.message;
    } else if (results_30_40.error) {
        error   = true;
        message = results_30_40.message;
    } else if (results_50_60.error) {
        error   = true;
        message = results_50_60.message;
    } else if (results_40_60.error) {
        error   = true;
        message = results_40_60.message;
    } else if (results_30_60.error) {
        error   = true;
        message = results_30_60.message;
    } else if (results_50_100.error) {
        error   = true;
        message = results_50_100.message;
    } else if (results_40_100.error) {
        error   = true;
        message = results_40_100.message;
    } else if (results_30_100.error) {
        error   = true;
        message = results_30_100.message;
    }

    if (error) {
        std::cout << "ERROR: Simulation error: " << message << std::endl;
    }

    std::stringstream ss;

    ss << "{ \"results\": {\n"
       << "  \"message\": \"" << message << "\",\n"
       << "  \"error\": " << (error ? "true" : "false") << ",\n"
       << "  \"fi_number\": " << std::setprecision(2) << std::fixed << fi_number << ",\n"
       << "  \"years\": " << months / 12 << ",\n"
       << "  \"months\": " << months % 12 << ",\n"
       << "  \"success_rate_100\": " << results_30_100.success_rate << ",\n"
       << "  \"success_rate_60\": " << results_30_60.success_rate << ",\n"
       << "  \"success_rate_40\": " << results_30_40.success_rate << ",\n"
       << "  \"success_rate40_100\": " << results_40_100.success_rate << ",\n"
       << "  \"success_rate40_60\": " << results_40_60.success_rate << ",\n"
       << "  \"success_rate40_40\": " << results_40_40.success_rate << ",\n"
       << "  \"success_rate50_100\": " << results_50_100.success_rate << ",\n"
       << "  \"success_rate50_60\": " << results_50_60.success_rate << ",\n"
       << "  \"success_rate50_40\": " << results_50_40.success_rate << "\n"
       << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop     = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms" << std::endl;
}

std::string params_to_string(const httplib::Request& req) {
    std::stringstream debug;
    debug << "[";
    std::string separator;
    for (auto& [key, value] : req.params) {
        debug << separator << key << "=" << value;
        separator = ",";
    }
    debug << "]";
    return debug.str();
}

void server_fi_planner_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(
                req,
                res,
                {"birth_year", "life_expectancy", "expenses", "income", "wr", "sr", "nw", "portfolio", "social_age", "social_amount", "extra_amount"})) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    const std::chrono::time_point     now{std::chrono::system_clock::now()};
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(now)};

    const unsigned current_year = static_cast<unsigned>(static_cast<int>(ymd.year()));

    swr::scenario scenario;

    // Don't run for too long
    scenario.timeout_msecs = 200;

    // Parse the parameters
    scenario.wr                    = atof(req.get_param_value("wr").c_str());
    const unsigned birth_year      = atoi(req.get_param_value("birth_year").c_str());
    const unsigned life_expectancy = atoi(req.get_param_value("life_expectancy").c_str());
    const float    sr              = atof(req.get_param_value("sr").c_str());
    const float    income          = atof(req.get_param_value("income").c_str());
    const float    expenses        = atof(req.get_param_value("expenses").c_str());
    const float    fi_net_worth    = atof(req.get_param_value("nw").c_str());
    const auto     portfolio_str   = req.get_param_value("portfolio");
    const auto     portfolio       = swr::parse_portfolio(portfolio_str, false);

    if (birth_year >= current_year) {
        res.set_content("{\"results\":{\"message\": \"There is something wrong with the birth year\",\"error\": true,}}", "text/json");
        return;
    }

    const unsigned age           = current_year - birth_year;
    const unsigned social_age    = atoi(req.get_param_value("social_age").c_str());
    const unsigned social_year   = social_age > age ? current_year - (social_age - age) : current_year;
    const float    social_amount = atof(req.get_param_value("social_amount").c_str());

    // TODO Validate life life_expectancy and age

    const float extra_amount = atof(req.get_param_value("extra_amount").c_str());

    std::cout << "DEBUG: FI Planner Request " << params_to_string(req) << std::endl;

    const float fi_number = expenses * (100.0f / scenario.wr);
    const bool  fi        = fi_number < fi_net_worth;

    // Estimate the number of months until retirement
    size_t months = 0;
    if (fi_net_worth < fi_number) {
        if (!income) {
            months = 12 * 1000;
        } else {
            const float returns = 5.0f;

            auto acc = fi_net_worth;
            while (acc < fi_number && months < 1200) {
                acc *= 1.0f + (returns / 100.0f) / 12.0f;
                acc += (income * sr / 100.0f) / 12.0f;
                ++months;
            }
        }
    }

    const unsigned retirement_year  = current_year + months / 12;
    const unsigned retirement_age   = retirement_year - birth_year;
    const unsigned retirement_years = life_expectancy - retirement_age;

    // Important to configure the initial value for social security and extra income to make sense
    scenario.initial_value = std::max(fi_net_worth, fi_number);

    // Enable social security if configured (simulation expects yearly, API expects monthly)
    if (social_amount > 0.0f) {
        scenario.social_security = true;
        scenario.social_delay    = retirement_year < social_year ? social_year - retirement_year : 0;
        scenario.social_amount   = 12.0f * social_amount;
    }

    // Enable income if configured (simulation expects yearly, API expects monthly)
    if (extra_amount > 0.0f) {
        scenario.extra_income        = true;
        scenario.extra_income_amount = 12.0f * extra_amount;
    }

    // For now cannot be configured
    scenario.rebalance          = swr::Rebalancing::YEARLY;
    scenario.withdraw_frequency = 12;
    scenario.threshold          = 0.0f;
    scenario.start_year         = 1871;
    scenario.end_year           = 2025;

    auto values             = swr::load_values(portfolio);
    scenario.inflation_data = swr::load_inflation(values, "us_inflation");

    scenario.portfolio = portfolio;
    scenario.values    = values;
    prepare_exchange_rates(scenario, "usd");

    scenario.years = retirement_years;
    auto results   = simulation(scenario);

    bool        error = false;
    std::string message;
    if (results.error) {
        error   = true;
        message = results.message;
    }

    if (error) {
        std::cout << "ERROR: Simulation error: " << message << std::endl;
    }

    swr::data_vector merged = values[0];

    for (size_t n = 0; n < merged.size(); ++n) {
        merged[n].value *= portfolio[0].allocation / 100.0f;

        for (size_t i = 1; i < values.size(); ++i) {
            merged[n].value += (portfolio[i].allocation / 100.0f) * values[i][n].value;
        }
    }

    auto yearly_returns = to_yearly_returns(merged);

    const auto low  = 100.0f * (percentile(yearly_returns, 40) - 1.0f);
    const auto med  = 100.0f * (percentile(yearly_returns, 50) - 1.0f);
    const auto high = 100.0f * (percentile(yearly_returns, 60) - 1.0f);

    std::stringstream ss;

    auto calculator = [&](float returns) {
        float current_value             = fi_net_worth;
        float current_withdrawal_amount = expenses;

        std::string separator;

        bool below_fi = current_value < fi_number;

        for (size_t year = current_year; year < current_year + (life_expectancy - age); ++year) {
            ss << separator << current_value;
            separator = ",";

            if (below_fi && current_value < fi_number) {
                current_value += income * (sr / 100.0f);
                current_value *= returns;
            } else {
                below_fi = false;

                // There are two cases based on social security

                auto withdrawal = current_withdrawal_amount;
                if (current_year >= social_year) {
                    withdrawal -= social_amount * 12.0f;
                }

                withdrawal -= extra_amount * 12.0f;

                current_value -= withdrawal;

                current_value *= returns;
                current_withdrawal_amount *= 1.01;
            }
        }
    };

    ss << "{ \"results\": {\n"
       << "  \"message\": \"" << message << "\",\n"
       << "  \"error\": " << (error ? "true" : "false") << ",\n"
       << "  \"fi\": " << (fi ? "true" : "false") << ",\n"
       << "  \"fi_number\": " << std::setprecision(2) << std::fixed << fi_number << ",\n"
       << "  \"years\": " << months / 12 << ",\n"
       << "  \"months\": " << months % 12 << ",\n"
       << "  \"retirement_year\": " << retirement_year << ",\n"
       << "  \"retirement_age\": " << retirement_age << ",\n"
       << "  \"retirement_years\": " << retirement_years << ",\n"
       << "  \"success_rate\": " << results.success_rate << ",\n"
       << "  \"low\": " << low << ",\n"
       << "  \"med\": " << med << ",\n"
       << "  \"high\": " << high << ",\n"
       << "  \"results_low\": [";

    calculator(1.0f + low * 0.8);
    ss << "  ],\n\"results_med\": [";
    calculator(1.0f + med * 0.8);
    ss << "  ],\n\"results_high\": [";
    calculator(1.0f + high * 0.8);
    ss << "  ]\n";
    ss << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop     = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms" << std::endl;
}

} // namespace

void print_general_help() {
    std::cout
            << "\nSafe Withdrawal Rate (SWR) Calculator - Command Line Tool\n"
            << "-------------------------------------------------------\n\n"
            << "Usage:\n"
            << "  swr_calculator <command> [arguments]\n\n"
            << "Available Commands:\n\n"

            << "1. fixed\n"
            << "   Analyze a fixed withdrawal rate over a historical period.\n"
            << "   Usage:\n"
            << "     swr_calculator fixed <withdrawal_rate> <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [final_threshold]\n"
            << "   Example:\n"
            << "     swr_calculator fixed 4 30 1871 2024 \"us_stocks:100;\" us_inflation 0.1 5\n\n"

            << "2. swr\n"
            << "   Find the safe withdrawal rate that meets a success rate limit.\n"
            << "   Usage:\n"
            << "     swr_calculator swr <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [success_rate_limit]\n"
            << "   Example:\n"
            << "     swr_calculator swr 30 1871 2024 \"us_stocks:100;\" us_inflation 0.1 95\n\n"

            << "3. multiple_wr\n"
            << "   Analyze multiple withdrawal rates with rebalancing strategies.\n"
            << "   Usage:\n"
            << "     swr_calculator multiple_wr <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> <rebalance_strategy>\n"
            << "   Example:\n"
            << "     swr_calculator multiple_wr 30 1871 2024 \"us_stocks:70;us_bonds:30;\" us_inflation annual\n\n"

            << "4. withdraw_frequency\n"
            << "   Analyze different withdrawal frequencies.\n"
            << "   Usage:\n"
            << "     swr_calculator withdraw_frequency <withdrawal_rate> <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [portfolio_adjustment]\n"
            << "   Example:\n"
            << "     swr_calculator withdraw_frequency 4 30 1871 2024 \"us_stocks:70;us_bonds:30;\" us_inflation 0.1 25\n\n"

            << "5. frequency\n"
            << "   Analyze portfolio performance with different withdrawal frequencies and contributions.\n"
            << "   Usage:\n"
            << "     swr_calculator frequency <start_year> <end_year> <years> <withdraw_frequency> <monthly_contribution>\n"
            << "   Example:\n"
            << "     swr_calculator frequency 1871 2024 30 12 500\n\n"

            << "General Help:\n"
            << "  Use 'swr_calculator help' to display this help message.\n"
            << "  For detailed help on a specific command, provide incorrect arguments to trigger command-specific help.\n"
            << std::endl;
}

void print_fixed_help() {
    std::cout << "\nUsage:\n"
              << "  swr_calculator fixed <withdrawal_rate> <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [final_threshold]\n\n"
              << "Arguments:\n"
              << "  fixed               Mode for fixed withdrawal rate analysis.\n"
              << "  <withdrawal_rate>   Annual withdrawal rate percentage (e.g., 4 for 4%).\n"
              << "  <years>            Number of years for retirement duration (e.g., 30).\n"
              << "  <start_year>       Start year of historical analysis (e.g., 1871).\n"
              << "  <end_year>         End year of historical analysis (e.g., 2024).\n"
              << "  <portfolio>        Asset allocation in the format \"asset:percentage;\" (e.g., \"us_stocks:100;\").\n"
              << "  <inflation_data>   Inflation dataset to adjust for inflation (e.g., us_inflation).\n"
              << "  [fees]             (Optional) Total Expense Ratio (TER) as a percentage (default: 0%).\n"
              << "  [final_threshold]  (Optional) Final portfolio threshold as a percentage (default: 0%).\n\n"
              << "Example:\n"
              << "  swr_calculator fixed 4 30 1871 2024 \"us_stocks:100;\" us_inflation\n"
              << std::endl;
}

void print_swr_help() {
    std::cout << "\nUsage:\n"
              << "  swr_calculator swr <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [success_rate_limit]\n\n"
              << "Arguments:\n"
              << "  swr                 Mode for safe withdrawal rate (SWR) discovery.\n"
              << "  <years>            Number of years for retirement duration (e.g., 30).\n"
              << "  <start_year>       Start year of historical analysis (e.g., 1871).\n"
              << "  <end_year>         End year of historical analysis (e.g., 2024).\n"
              << "  <portfolio>        Asset allocation in the format \"asset:percentage;\" (e.g., \"us_stocks:100;\").\n"
              << "  <inflation_data>   Inflation dataset to adjust for inflation (e.g., us_inflation).\n"
              << "  [fees]             (Optional) Total Expense Ratio (TER) as a percentage (default: 0%).\n"
              << "  [success_rate_limit] (Optional) Desired success rate limit as a percentage (default: 95%).\n\n"
              << "Example:\n"
              << "  swr_calculator swr 30 1871 2024 \"us_stocks:100;\" us_inflation 0.1 95\n"
              << std::endl;
}

void print_multiple_wr_help() {
    std::cout << "\nUsage:\n"
              << "  swr_calculator multiple_wr <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> <rebalance_strategy>\n\n"
              << "Arguments:\n"
              << "  multiple_wr         Mode for analyzing multiple withdrawal rates with rebalancing strategies.\n"
              << "  <years>            Number of years for retirement duration (e.g., 30).\n"
              << "  <start_year>       Start year of historical analysis (e.g., 1871).\n"
              << "  <end_year>         End year of historical analysis (e.g., 2024).\n"
              << "  <portfolio>        Asset allocation in the format \"asset:percentage;\" (e.g., \"us_stocks:70;us_bonds:30;\").\n"
              << "  <inflation_data>   Inflation dataset to adjust for inflation (e.g., us_inflation).\n"
              << "  <rebalance_strategy> Strategy for rebalancing (e.g., 'annual', 'none').\n\n"
              << "Example:\n"
              << "  swr_calculator multiple_wr 30 1871 2024 \"us_stocks:70;us_bonds:30;\" us_inflation annual\n"
              << std::endl;
}

void print_withdraw_frequency_help() {
    std::cout
            << "\nUsage:\n"
            << "  swr_calculator withdraw_frequency <withdrawal_rate> <years> <start_year> <end_year> \"<portfolio>\" <inflation_data> [fees] [portfolio_adjustment]\n\n"
            << "Arguments:\n"
            << "  withdraw_frequency  Mode to analyze different withdrawal frequencies.\n"
            << "  <withdrawal_rate>   Annual withdrawal rate percentage (e.g., 4 for 4%).\n"
            << "  <years>            Number of years for retirement duration (e.g., 30).\n"
            << "  <start_year>       Start year of historical analysis (e.g., 1871).\n"
            << "  <end_year>         End year of historical analysis (e.g., 2024).\n"
            << "  <portfolio>        Asset allocation in the format \"asset:percentage;\" (e.g., \"us_stocks:70;us_bonds:30;\").\n"
            << "  <inflation_data>   Inflation dataset for adjusting withdrawals (e.g., us_inflation).\n"
            << "  [fees]             (Optional) Total Expense Ratio (TER) as a percentage (default: 0%).\n"
            << "  [portfolio_adjustment] (Optional) Adjustment factor for the portfolio in percentage (default: 20%).\n\n"
            << "Example:\n"
            << "  swr_calculator withdraw_frequency 4 30 1871 2024 \"us_stocks:70;us_bonds:30;\" us_inflation 0.1 25\n"
            << std::endl;
}

void print_frequency_help() {
    std::cout << "\nUsage:\n"
              << "  swr_calculator frequency <start_year> <end_year> <years> <withdraw_frequency> <monthly_contribution>\n\n"
              << "Arguments:\n"
              << "  frequency           Mode for analyzing different withdrawal frequencies with contributions.\n"
              << "  <start_year>       Start year of historical analysis (e.g., 1871).\n"
              << "  <end_year>         End year of historical analysis (e.g., 2024).\n"
              << "  <years>            Number of years for retirement duration (e.g., 30).\n"
              << "  <withdraw_frequency> Frequency of withdrawals (e.g., 1 for yearly, 12 for monthly).\n"
              << "  <monthly_contribution> Monthly contribution amount (e.g., 500).\n\n"
              << "Example:\n"
              << "  swr_calculator frequency 1871 2024 30 12 500\n"
              << std::endl;
}

int fixed_scenario(const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Error: Not enough arguments for the 'fixed' command." << std::endl;
        print_fixed_help();
        return 1;
    }

    swr::scenario scenario;

    scenario.wr         = atof(args[1].c_str());
    scenario.years      = atoi(args[2].c_str());
    scenario.start_year = atoi(args[3].c_str());
    scenario.end_year   = atoi(args[4].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[5], false);
    auto inflation      = args[6];

    if (args.size() > 7) {
        scenario.fees = atof(args[7].c_str()) / 100.0f;
    }

    if (args.size() > 8) {
        scenario.final_threshold = atof(args[8].c_str()) / 100.0f;
    }

    swr::normalize_portfolio(scenario.portfolio);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    std::cout << "Withdrawal Rate (WR): " << scenario.wr << "%\n"
              << "     Number of years: " << scenario.years << "\n"
              << "               Start: " << scenario.start_year << "\n"
              << "                 End: " << scenario.end_year << "\n"
              << "                 TER: " << 100.0f * scenario.fees << "%\n"
              << "           Inflation: " << inflation << "\n"
              << "           Portfolio: \n";

    for (auto& position : scenario.portfolio) {
        std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
    }

    if (!prepare_exchange_rates(scenario, "usd")) {
        std::cout << "Error with exchange rates" << std::endl;
        return 1;
    }

    scenario.strict_validation = false;

    auto printer = [scenario](const std::string& message, const auto& results) {
        std::cout << "     Success Rate (" << message << "): (" << results.successes << "/" << (results.failures + results.successes) << ") "
                  << results.success_rate << " [" << results.tv_average << ":" << results.tv_median << ":" << results.tv_minimum << ":" << results.tv_maximum
                  << "]" << std::endl;

        if (results.failures) {
            std::cout << "         Worst duration: " << results.worst_duration << " months (" << results.worst_starting_month << "/"
                      << results.worst_starting_year << ")" << std::endl;
        } else {
            std::cout << "         Worst duration: " << scenario.years * 12 << " months" << std::endl;
        }

        std::cout << "         Worst result: " << results.worst_tv << " (" << results.worst_tv_month << "/" << results.worst_tv_year << ")" << std::endl;
        std::cout << "          Best result: " << results.best_tv << " (" << results.best_tv_month << "/" << results.best_tv_year << ")" << std::endl;

        std::cout << "         Highest Eff. WR: " << results.highest_eff_wr << "% (" << results.highest_eff_wr_start_month << "/"
                  << results.highest_eff_wr_start_year << "->" << results.highest_eff_wr_year << ")" << std::endl;
        std::cout << "          Lowest Eff. WR: " << results.lowest_eff_wr << "% (" << results.lowest_eff_wr_start_month << "/"
                  << results.lowest_eff_wr_start_year << "->" << results.lowest_eff_wr_year << ")" << std::endl;
    };

    auto start = std::chrono::high_resolution_clock::now();

    scenario.withdraw_frequency = 12;
    std::cout << scenario << std::endl;
    auto yearly_results = swr::simulation(scenario);

    if (yearly_results.message.size()) {
        std::cout << yearly_results.message << std::endl;
    }

    if (yearly_results.error) {
        return 1;
    }

    printer("Yearly", yearly_results);

    scenario.withdraw_frequency = 1;
    auto monthly_results        = swr::simulation(scenario);

    printer("Monthly", monthly_results);

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration) << "/s)"
              << std::endl;

    return 0;
}

int single_swr_scenario(const std::vector<std::string>& args) {
    if (args.size() < 6) {
        std::cout << "Error: Not enough arguments for the 'swr' command." << std::endl;
        print_swr_help();
        return 1;
    }

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];

    if (args.size() > 6) {
        scenario.fees = atof(args[6].c_str()) / 100.0f;
    }

    float limit = 95.0f;
    if (args.size() > 7) {
        limit = atof(args[7].c_str());
    }

    swr::normalize_portfolio(scenario.portfolio);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    for (auto& position : scenario.portfolio) {
        std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
    }

    auto start = std::chrono::high_resolution_clock::now();

    scenario.withdraw_frequency = 1;

    float        best_wr = 0.0f;
    swr::results best_results;

    for (float wr = 6.0f; wr >= 2.0f; wr -= 0.01f) {
        scenario.wr = wr;

        auto results = swr::simulation(scenario);

        if (results.message.size()) {
            std::cout << results.message << std::endl;
        }

        if (results.error) {
            return 1;
        }

        if (results.success_rate > limit) {
            best_results = results;
            best_wr      = wr;
            break;
        }
    }

    std::cout << "WR: " << best_wr << "(" << best_results.success_rate << ")" << std::endl;

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration) << "/s)"
              << std::endl;

    return 0;
}

int multiple_swr_scenario(const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Error: Not enough arguments for the 'multiple_wr' command." << std::endl;
        print_multiple_wr_help();
        return 1;
    }

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    std::cout << "     Number of years: " << scenario.years << "\n"
              << "           Rebalance: " << scenario.rebalance << "\n"
              << "               Start: " << scenario.start_year << "\n"
              << "                 End: " << scenario.end_year << "\n";

    auto start = std::chrono::high_resolution_clock::now();

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += 5) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            multiple_wr(scenario);
        }
    } else {
        swr::normalize_portfolio(scenario.portfolio);
        multiple_wr(scenario);
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration) << "/s)"
              << std::endl;

    return 0;
}

int withdraw_frequency_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Error: Not enough arguments for the 'withdraw_frequency' command." << std::endl;
        print_withdraw_frequency_help();
        return 1;
    }

    const bool graph = command == "withdraw_frequency_graph";

    swr::scenario scenario;

    scenario.wr         = atof(args[1].c_str());
    scenario.years      = atoi(args[2].c_str());
    scenario.start_year = atoi(args[3].c_str());
    scenario.end_year   = atoi(args[4].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[5], true);
    auto inflation      = args[6];

    if (args.size() > 7) {
        scenario.fees = atof(args[7].c_str()) / 100.0f;
    }

    float portfolio_add = 20;
    if (args.size() > 8) {
        portfolio_add = atof(args[8].c_str());
    }

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    std::cout << "Withdrawal Rate (WR): " << scenario.wr << "%\n"
              << "     Number of years: " << scenario.years << "\n"
              << "               Start: " << scenario.start_year << "\n"
              << "                 End: " << scenario.end_year << "\n"
              << "                 TER: " << 100.0f * scenario.fees << "%\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    Graph g(graph);
    g.xtitle_ = "Withdrawal Frequency (months)";
    g.title_  = std::format("Withdrawal Frequency - {} Years - {}% Withdrawal Rate", scenario.years, args[1]);

    Graph duration_g(graph, "Worst Duration (months)");
    duration_g.xtitle_ = "Withdrawal Frequency (months)";
    duration_g.title_  = std::format("Withdrawal Frequency And Worst Duration - {} Years - {}% WR", scenario.years, args[1]);

    if (!g.enabled_) {
        std::cout << "portfolio;";
        for (size_t f = 1; f <= 24; ++f) {
            std::cout << f << ";";
        }
        std::cout << std::endl;
    }

    prepare_exchange_rates(scenario, "usd");

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            g.add_legend(portfolio_to_string(scenario, true));
            duration_g.add_legend(portfolio_to_string(scenario, true));

            std::map<float, float> g_results;
            std::map<float, float> duration_g_results;

            if (!g.enabled_) {
                for (auto& position : scenario.portfolio) {
                    if (position.allocation > 0) {
                        std::cout << position.allocation << "% " << position.asset << " ";
                    }
                }
            }

            for (size_t f = 1; f <= 24; ++f) {
                scenario.withdraw_frequency = f;

                auto results = swr::simulation(scenario);

                if (results.message.size()) {
                    std::cout << results.message << std::endl;
                }

                if (results.error) {
                    return 1;
                }

                if (g.enabled_) {
                    g_results[f]          = results.success_rate;
                    duration_g_results[f] = results.worst_duration;
                } else {
                    std::cout << ";" << results.success_rate;
                }
            }

            if (g.enabled_) {
                g.add_data(g_results);
                duration_g.add_data(duration_g_results);
            } else {
                std::cout << std::endl;
            }
        }
    } else {
        swr::normalize_portfolio(scenario.portfolio);

        if (!g.enabled_) {
            for (auto& position : scenario.portfolio) {
                if (position.allocation > 0) {
                    std::cout << position.allocation << "% " << position.asset << " ";
                }
            }
        }

        for (size_t f = 1; f <= 24; ++f) {
            scenario.withdraw_frequency = f;

            auto results = swr::simulation(scenario);

            if (results.message.size()) {
                std::cout << results.message << std::endl;
            }

            if (results.error) {
                return 1;
            }

            std::cout << ";" << results.success_rate;
        }

        std::cout << std::endl;
        std::cout << std::endl;

        for (float w = 3.0f; w <= 6.0f; w += 0.25f) {
            std::cout << w;

            scenario.wr = w;

            g.add_legend(std::to_string(w));
            std::map<float, float> g_results;

            for (size_t f = 1; f <= 24; ++f) {
                scenario.withdraw_frequency = f;

                auto results = swr::simulation(scenario);

                if (results.message.size()) {
                    std::cout << results.message << std::endl;
                }

                if (results.error) {
                    return 1;
                }

                if (g.enabled_) {
                    g_results[f] = results.success_rate;
                } else {
                    std::cout << ";" << results.success_rate;
                }
            }

            if (g.enabled_) {
                g.add_data(g_results);
            } else {
                std::cout << std::endl;
            }
        }
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration) << "/s)"
              << std::endl;

    return 0;
}

int frequency_scenario(const std::vector<std::string>& args) {
    if (args.size() < 6) {
        std::cout << "Error: Not enough arguments for the 'frequency' command." << std::endl;
        print_frequency_help();
        return 1;
    }

    size_t start_year  = atoi(args[1].c_str());
    size_t end_year    = atoi(args[2].c_str());
    size_t years       = atoi(args[3].c_str());
    size_t frequency   = atoi(args[4].c_str());
    size_t monthly_buy = atoi(args[5].c_str());

    auto portfolio = swr::parse_portfolio("us_stocks:100;", false);
    swr::normalize_portfolio(portfolio);

    auto values = swr::load_values(portfolio);

    const auto months = years * 12;

    std::vector<swr::data>::const_iterator returns;

    float  total       = 0;
    float  max         = 0;
    size_t simulations = 0;

    for (size_t current_year = start_year; current_year <= end_year - years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            size_t end_year  = current_year + (current_month - 1 + months - 1) / 12;
            size_t end_month = 1 + ((current_month - 1) + (months - 1) % 12) % 12;

            size_t months = 0;

            returns = swr::get_start(values[0], current_year, (current_month % 12) + 1);

            float net_worth = 0;

            for (size_t y = current_year; y <= end_year; ++y) {
                for (size_t m = (y == current_year ? current_month : 1); m <= (y == end_year ? end_month : 12); ++m, ++months) {
                    // Adjust the portfolio with the returns
                    net_worth *= returns->value;
                    ++returns;

                    if (months % frequency == frequency - 1) {
                        net_worth += frequency * monthly_buy;
                    }
                }
            }

            total += net_worth;
            ++simulations;

            max = std::max(net_worth, max);
        }
    }

    std::array<float, 6> worst_results;
    std::array<float, 6> best_results;
    worst_results.fill(0.0f);
    best_results.fill(0.0f);

    for (size_t current_year = start_year; current_year <= end_year - years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            size_t end_year  = current_year + (current_month - 1 + months - 1) / 12;
            size_t end_month = 1 + ((current_month - 1) + (months - 1) % 12) % 12;

            std::array<float, 6> results;

            for (size_t freq = 1; freq <= 6; ++freq) {
                size_t months = 0;

                returns = swr::get_start(values[0], current_year, (current_month % 12) + 1);

                float net_worth = 0;

                for (size_t y = current_year; y <= end_year; ++y) {
                    for (size_t m = (y == current_year ? current_month : 1); m <= (y == end_year ? end_month : 12); ++m, ++months) {
                        // Adjust the portfolio with the returns
                        net_worth *= returns->value;
                        ++returns;

                        if (months % freq == freq - 1) {
                            net_worth += freq * monthly_buy;
                        }
                    }
                }

                results[freq - 1] = net_worth;
            }

            for (size_t f = 1; f < 6; ++f) {
                worst_results[f] = std::max(worst_results[f], results[0] - results[f]);
                best_results[f]  = std::min(best_results[f], results[0] - results[f]);
            }
        }
    }

    std::cout << "Average: " << std::fixed << total / simulations << std::endl;
    std::cout << "Max: " << std::fixed << max << std::endl;
    std::cout << "Simulations: " << simulations << std::endl;

    for (size_t f = 1; f < 6; ++f) {
        std::cout << "Worst case " << f + 1 << " : " << worst_results[f] << std::endl;
    }

    for (size_t f = 1; f < 6; ++f) {
        std::cout << "Best case " << f + 1 << " : " << best_results[f] << std::endl;
    }

    return 0;
}

int analysis_scenario(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Not enough arguments for analysis" << std::endl;
        return 1;
    }

    size_t start_year = atoi(args[1].c_str());
    size_t end_year   = atoi(args[2].c_str());

    auto portfolio = swr::parse_portfolio("ch_stocks:10;us_stocks:50;us_bonds:50;gold:10;", false);

    auto values            = swr::load_values(portfolio);
    auto ch_inflation_data = swr::load_inflation(values, "ch_inflation");
    auto us_inflation_data = swr::load_inflation(values, "us_inflation");

    Graph yearly_inflation_graph(true, "Yearly Inflation");
    Graph yearly_stocks_graph(true, "Yearly Stock Returns");
    Graph gold_graph(true, "Historical Gold Price");

    yearly_inflation_graph.xtitle_ = "Years";
    yearly_stocks_graph.xtitle_    = "Years";
    gold_graph.xtitle_             = "Years";

    auto to_returns_graph = [start_year, end_year](auto& graph, const auto& data, std::string_view title) {
        std::map<float, float> yearly_results;
        for (auto& value : data) {
            if (value.year < start_year || value.year > end_year) {
                continue;
            }
            if (value.month == 1) {
                yearly_results[value.year] = 1.0f;
            }
            yearly_results[value.year] *= value.value;
            if (value.month == 12) {
                yearly_results[value.year] = 100.0f * (yearly_results[value.year] - 1.0f);
            }
        }
        graph.add_legend(title);
        graph.add_data(yearly_results);
    };

    auto to_price_graph = [start_year, end_year](auto& graph, const auto& data, std::string_view title) {
        std::map<float, float> yearly_results;
        for (auto& value : data) {
            if (value.year < start_year || value.year > end_year) {
                continue;
            }

            if (yearly_results.empty()) {
                yearly_results[value.year] = 100.0f;
            } else if (value.month == 1) {
                yearly_results[value.year] = yearly_results[value.year - 1];
            }
            yearly_results[value.year] *= value.value;
        }
        graph.add_legend(title);
        graph.add_data(yearly_results);
    };

    to_returns_graph(yearly_inflation_graph, ch_inflation_data, "Inflation CH");
    to_returns_graph(yearly_inflation_graph, us_inflation_data, "Inflation US");

    to_returns_graph(yearly_stocks_graph, values[0], "CH Stocks");
    to_returns_graph(yearly_stocks_graph, values[1], "US Stocks");
    to_price_graph(gold_graph, values[3], "Gold");

    auto analyzer = [&](auto& v, const std::string& name) {
        float monthly_average = 0.0f;

        float       worst_month = 1.0f;
        std::string worst_month_str;

        float       best_month = 0.0f;
        std::string best_month_str;

        size_t negative = 0;
        size_t total    = 0;

        auto yearly_returns = to_yearly_returns(v);

        for (auto value : v) {
            if (value.year >= start_year && value.year <= end_year) {
                if (value.value < worst_month) {
                    worst_month     = value.value;
                    worst_month_str = std::to_string(value.year) + "." + std::to_string(value.month);
                }

                if (value.value > best_month) {
                    best_month     = value.value;
                    best_month_str = std::to_string(value.year) + "." + std::to_string(value.month);
                }

                ++total;

                if (value.value < 1.0f) {
                    ++negative;
                }

                monthly_average += value.value;
            }
        }

        std::cout << name << " p40 yearly returns: " << 100.0f * (percentile(yearly_returns, 40) - 1.0f) << "%" << std::endl;
        std::cout << name << " p50 yearly returns: " << 100.0f * (percentile(yearly_returns, 50) - 1.0f) << "%" << std::endl;
        std::cout << name << " p60 yearly returns: " << 100.0f * (percentile(yearly_returns, 60) - 1.0f) << "%" << std::endl;

        std::cout << name << " average monthly returns: +" << 100.0f * ((monthly_average / total) - 1.0f) << "%" << std::endl;
        std::cout << name << " best monthly returns: +" << 100.0f * (best_month - 1.0f) << "% (" << best_month_str << ")" << std::endl;
        std::cout << name << " worst monthly returns: -" << 100.0f * (1.0f - worst_month) << "% (" << worst_month_str << ")" << std::endl;
        std::cout << name << " Negative months: " << negative << " (" << 100.0f * (negative / float(total)) << "%)" << std::endl;
    };

    analyzer(values[0], "CH Stocks");
    analyzer(values[1], "US Stocks");
    analyzer(values[2], "US Bonds");
    analyzer(us_inflation_data, "US Inflation");
    analyzer(ch_inflation_data, "CH Inflation");

    yearly_inflation_graph.flush();
    std::cout << "\n";
    yearly_stocks_graph.flush();
    std::cout << "\n";
    gold_graph.flush();
    std::cout << "\n";

    return 0;
}

int portfolio_analysis_scenario(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Not enough arguments for portfolio_analysis" << std::endl;
        return 1;
    }

    auto portfolio = swr::parse_portfolio(args[1], false);

    auto values = swr::load_values(portfolio);

    std::cout << "Number of assets: " << values.size() << std::endl;

    swr::data_vector merged = values[0];

    for (size_t n = 0; n < merged.size(); ++n) {
        merged[n].value *= portfolio[0].allocation / 100.0f;

        for (size_t i = 1; i < values.size(); ++i) {
            merged[n].value += (portfolio[i].allocation / 100.0f) * values[i][n].value;
        }
    }

    auto yearly_returns = to_yearly_returns(merged);

    std::cout << " p40 yearly returns: " << 100.0f * (percentile(yearly_returns, 40) - 1.0f) << "%" << std::endl;
    std::cout << " p50 yearly returns: " << 100.0f * (percentile(yearly_returns, 50) - 1.0f) << "%" << std::endl;
    std::cout << " p60 yearly returns: " << 100.0f * (percentile(yearly_returns, 60) - 1.0f) << "%" << std::endl;

    auto cagr_returns = to_cagr_returns(portfolio, 20);

    std::cout << " p30 20-year cagr returns: " << 100.0f * (percentile(cagr_returns, 30)) << "%" << std::endl;
    std::cout << " p40 20-year cagr returns: " << 100.0f * (percentile(cagr_returns, 40)) << "%" << std::endl;
    std::cout << " p50 20-year cagr returns: " << 100.0f * (percentile(cagr_returns, 50)) << "%" << std::endl;
    std::cout << " p60 20-year cagr returns: " << 100.0f * (percentile(cagr_returns, 60)) << "%" << std::endl;
    std::cout << " p70 20-year cagr returns: " << 100.0f * (percentile(cagr_returns, 70)) << "%" << std::endl;

    return 0;
}

int allocation_scenario() {
    Graph g_us(true, "Annualized Yearly Returns (%)", "bar-graph");
    g_us.title_  = "US Portfolio Allocation Annualized Returns";
    g_us.xtitle_ = "Portfolio";

    Graph g_ch(true, "Annualized Yearly Returns (%)", "bar-graph");
    g_ch.title_  = "CH Portfolio Allocation Annualized Returns";
    g_ch.xtitle_ = "Portfolio";

    Graph gv_us(true, "Volatility", "bar-graph");
    gv_us.title_  = "US Portfolio Volatility";
    gv_us.xtitle_ = "Portfolio";

    Graph gv_ch(true, "Volatility", "bar-graph");
    gv_ch.title_  = "CH Portfolio Volatility";
    gv_ch.xtitle_ = "Portfolio";

    constexpr bool geometric = true;

    auto analyzer = [](Graph& g, std::string_view name, std::string_view portfolio_view) {
        auto portfolio = swr::parse_portfolio(portfolio_view, false);
        auto values    = swr::load_values(portfolio);

        float  ar_average  = 0.0f;
        float  geo_average = 1.0f;
        float  temp        = 1.0f;
        size_t c           = 0;

        for (size_t i = 0; i < values[0].size(); ++i) {
            if (values[0][i].month == 1) {
                temp = 1.0f;
            }

            float compound = 0.0f;
            for (size_t p = 0; p < portfolio.size(); ++p) {
                compound += (portfolio[p].allocation / 100.0f) * values[p][i].value;
            }
            temp *= compound;

            if (values[0][i].month == 12) {
                ar_average += temp - 1.0f;
                geo_average *= temp;
                ++c;
            }
        }

        std::map<float, float> average_returns;
        if (geometric) {
            average_returns[1.0f] = 100.0f * (std::pow(geo_average, 1.0f / c) - 1.0f);
        } else {
            average_returns[1.0f] = 100.0f * (ar_average / c);
        }

        g.add_legend(name);
        g.add_data(average_returns);
    };

    auto v_analyzer = [](Graph& g, std::string_view name, std::string_view portfolio_view) {
        auto portfolio = swr::parse_portfolio(portfolio_view, false);
        auto values    = swr::load_values(portfolio);

        float mean = 0.0f;
        float diff = 0.0f;

        for (size_t i = 0; i < values[0].size(); ++i) {
            float compound = 0.0f;
            for (size_t p = 0; p < portfolio.size(); ++p) {
                compound += (portfolio[p].allocation / 100.0f) * values[p][i].value;
            }
            mean += compound - 1.0f;
        }

        mean = mean / values[0].size();

        for (size_t i = 0; i < values[0].size(); ++i) {
            float compound = 0.0f;
            for (size_t p = 0; p < portfolio.size(); ++p) {
                compound += (portfolio[p].allocation / 100.0f) * values[p][i].value;
            }
            diff += (compound - 1.0f - mean) * (compound - 1.0f - mean);
        }

        std::map<float, float> volatility;
        volatility[1.0f] = std::sqrt(100.0f * diff / values[0].size());

        g.add_legend(name);
        g.add_data(volatility);
    };

    analyzer(g_us, "100/0", "us_stocks:100;us_bonds:0;");
    analyzer(g_us, "90/10", "us_stocks:90;us_bonds:10;");
    analyzer(g_us, "80/20", "us_stocks:80;us_bonds:20;");
    analyzer(g_us, "70/30", "us_stocks:70;us_bonds:30;");
    analyzer(g_us, "60/40", "us_stocks:60;us_bonds:40;");
    analyzer(g_us, "50/50", "us_stocks:50;us_bonds:50;");
    analyzer(g_us, "40/60", "us_stocks:40;us_bonds:60;");
    analyzer(g_us, "30/70", "us_stocks:30;us_bonds:70;");
    analyzer(g_us, "20/80", "us_stocks:20;us_bonds:80;");
    analyzer(g_us, "10/90", "us_stocks:10;us_bonds:90;");
    analyzer(g_us, "0/100", "us_stocks:0;us_bonds:100;");

    analyzer(g_ch, "100/0", "ch_stocks:100;ch_bonds:0;");
    analyzer(g_ch, "90/10", "ch_stocks:90;ch_bonds:10;");
    analyzer(g_ch, "80/20", "ch_stocks:80;ch_bonds:20;");
    analyzer(g_ch, "70/30", "ch_stocks:70;ch_bonds:30;");
    analyzer(g_ch, "60/40", "ch_stocks:60;ch_bonds:40;");
    analyzer(g_ch, "50/50", "ch_stocks:50;ch_bonds:50;");
    analyzer(g_ch, "40/60", "ch_stocks:40;ch_bonds:60;");
    analyzer(g_ch, "30/70", "ch_stocks:30;ch_bonds:70;");
    analyzer(g_ch, "20/80", "ch_stocks:20;ch_bonds:80;");
    analyzer(g_ch, "10/90", "ch_stocks:10;ch_bonds:90;");
    analyzer(g_ch, "0/100", "ch_stocks:0;ch_bonds:100;");

    v_analyzer(gv_us, "100/0", "us_stocks:100;us_bonds:0;");
    v_analyzer(gv_us, "90/10", "us_stocks:90;us_bonds:10;");
    v_analyzer(gv_us, "80/20", "us_stocks:80;us_bonds:20;");
    v_analyzer(gv_us, "70/30", "us_stocks:70;us_bonds:30;");
    v_analyzer(gv_us, "60/40", "us_stocks:60;us_bonds:40;");
    v_analyzer(gv_us, "50/50", "us_stocks:50;us_bonds:50;");
    v_analyzer(gv_us, "40/60", "us_stocks:40;us_bonds:60;");
    v_analyzer(gv_us, "30/70", "us_stocks:30;us_bonds:70;");
    v_analyzer(gv_us, "20/80", "us_stocks:20;us_bonds:80;");
    v_analyzer(gv_us, "10/90", "us_stocks:10;us_bonds:90;");
    v_analyzer(gv_us, "0/100", "us_stocks:0;us_bonds:100;");

    v_analyzer(gv_ch, "100/0", "ch_stocks:100;ch_bonds:0;");
    v_analyzer(gv_ch, "90/10", "ch_stocks:90;ch_bonds:10;");
    v_analyzer(gv_ch, "80/20", "ch_stocks:80;ch_bonds:20;");
    v_analyzer(gv_ch, "70/30", "ch_stocks:70;ch_bonds:30;");
    v_analyzer(gv_ch, "60/40", "ch_stocks:60;ch_bonds:40;");
    v_analyzer(gv_ch, "50/50", "ch_stocks:50;ch_bonds:50;");
    v_analyzer(gv_ch, "40/60", "ch_stocks:40;ch_bonds:60;");
    v_analyzer(gv_ch, "30/70", "ch_stocks:30;ch_bonds:70;");
    v_analyzer(gv_ch, "20/80", "ch_stocks:20;ch_bonds:80;");
    v_analyzer(gv_ch, "10/90", "ch_stocks:10;ch_bonds:90;");
    v_analyzer(gv_ch, "0/100", "ch_stocks:0;ch_bonds:100;");

    g_us.flush();
    std::cout << "\n";

    g_ch.flush();
    std::cout << "\n";

    gv_us.flush();
    std::cout << "\n";

    gv_ch.flush();
    std::cout << "\n";

    return 0;
}

int term_scenario(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Not enough arguments for term" << std::endl;
        return 1;
    }

    const size_t min = atoi(args[1].c_str());
    const size_t max = atoi(args[2].c_str());

    Graph average_graph(true, "Average Returns (%)");
    Graph worst_graph(true, "Worst Returns (%)");
    Graph worst5_graph(true, "98th Percentile Worst Returns (%)");
    Graph best_graph(true, "Best Returns (%)");
    Graph chance_graph(true, "Likelihood of positive returns (%)");

    average_graph.xtitle_ = "Months";
    worst_graph.xtitle_   = "Months";
    worst5_graph.xtitle_  = "Months";
    best_graph.xtitle_    = "Months";
    chance_graph.xtitle_  = "Months";

    auto compute_multiple = [&](std::string_view asset) {
        average_graph.add_legend(asset_to_string(asset));
        best_graph.add_legend(asset_to_string(asset));
        worst_graph.add_legend(asset_to_string(asset));
        worst5_graph.add_legend(asset_to_string(asset));
        chance_graph.add_legend(asset_to_string(asset));

        std::map<float, float> average_results;
        std::map<float, float> best_results;
        std::map<float, float> worst_results;
        std::map<float, float> worst5_results;
        std::map<float, float> chance_results;

        auto compute = [&](size_t term, std::string_view asset) {
            auto portfolio = swr::parse_portfolio(std::string(asset) + ":100;", false);
            auto values    = swr::load_values(portfolio);

            auto start = values[0].begin();
            auto end   = values[0].end();

            float total = 0.0f;

            std::vector<float> results;
            results.reserve(1024);

            while (true) {
                auto it = start;

                float total_returns = 1.0f;

                for (size_t i = 0; i < term; ++i) {
                    total_returns *= it->value;
                    ++it;

                    if (it == end) {
                        break;
                    }
                }

                if (it == end) {
                    break;
                }

                results.push_back(total_returns);
                total += total_returns;

                ++start;
            }

            std::sort(results.begin(), results.end());

            size_t negative_returns = 0;
            for (size_t i = 0; i < results.size(); ++i) {
                if (results[i] >= 1.0f) {
                    negative_returns = i;
                    break;
                }
            }

            best_results[term]    = 100.0f * (results.back() - 1.0f);
            worst_results[term]   = 100.0f * (results.front() - 1.0f);
            worst5_results[term]  = 100.0f * (results[0.02f * results.size()] - 1.0f);
            average_results[term] = 100.0f * (total / results.size() - 1.0f);
            chance_results[term]  = 100.0f * (1.0f - float(negative_returns) / results.size());
        };

        for (size_t i = min; i <= max; ++i) {
            compute(i, asset);
        }

        average_graph.add_data(average_results);
        best_graph.add_data(best_results);
        worst_graph.add_data(worst_results);
        worst5_graph.add_data(worst5_results);
        chance_graph.add_data(chance_results);
    };

    compute_multiple("us_stocks");
    compute_multiple("us_bonds");
    compute_multiple("ex_us_stocks");
    compute_multiple("ch_stocks");
    compute_multiple("ch_bonds");
    compute_multiple("gold");

    average_graph.flush();
    std::cout << "\n";
    worst_graph.flush();
    std::cout << "\n";
    worst5_graph.flush();
    std::cout << "\n";
    chance_graph.flush();
    std::cout << "\n";
    best_graph.flush();
    std::cout << "\n";

    return 0;
}

int glidepath_scenario(std::string_view command, const std::vector<std::string>& args) {
    std::cout << "\n";
    swr::scenario scenario;

    const bool graph = command == "glidepath_graph" || command == "reverse_glidepath_graph";

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    float start_wr = 3.0f;
    if (args.size() > 7) {
        start_wr = atof(args[7].c_str());
    }

    float end_wr = 6.0f;
    if (args.size() > 8) {
        end_wr = atof(args[8].c_str());
    }

    float add_wr = 0.1f;
    if (args.size() > 9) {
        add_wr = atof(args[9].c_str());
    }

    swr::normalize_portfolio(scenario.portfolio);
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    std::stringstream failsafe_ss;

    auto success_only = [&](Graph& g, auto title) {
        if (g.enabled_) {
            multiple_wr_success_graph(g, title, false, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets(title, scenario, start_wr, end_wr, add_wr);
        }
    };

    auto failsafe_and_success = [&](Graph& g, auto title) {
        success_only(g, title);
        failsafe_swr(title, scenario, 6.0f, 0.0f, 0.01f, failsafe_ss);
    };

    if (command == "glidepath" || command == "glidepath_graph") {
        Graph g_80_100(graph);
        g_80_100.title_ = std::format("Equity Glidepaths - 80 - 100\% stocks - {} Years", scenario.years);

        Graph g_60_100(graph);
        g_60_100.title_ = std::format("Equity Glidepaths - 60 - 100\% stocks - {} Years", scenario.years);

        Graph g_60_80(graph);
        g_60_80.title_ = std::format("Equity Glidepaths - 60 - 80\% stocks - {} Years", scenario.years);

        Graph g_40_80(graph);
        g_40_80.title_ = std::format("Equity Glidepaths - 40 - 80\% stocks - {} Years", scenario.years);

        Graph g_40_100(graph);
        g_40_100.title_ = std::format("Equity Glidepaths - 40 - 100\% stocks - {} Years", scenario.years);

        scenario.glidepath               = false;
        scenario.portfolio[0].allocation = 40;
        scenario.portfolio[1].allocation = 60;
        failsafe_and_success(g_40_80, "Static 40%");
        success_only(g_40_100, "Static 40%");

        scenario.glidepath = true;
        scenario.gp_goal   = 80.0f;

        scenario.gp_pass = 0.2;
        failsafe_and_success(g_40_80, "40%-80% +0.2");

        scenario.gp_pass = 0.3;
        failsafe_and_success(g_40_80, "40%-80% +0.3");

        scenario.gp_pass = 0.4;
        failsafe_and_success(g_40_80, "40%-80% +0.4");

        scenario.gp_pass = 0.5;
        failsafe_and_success(g_40_80, "40%-80% +0.5");

        scenario.glidepath = true;
        scenario.gp_goal   = 100.0f;

        scenario.gp_pass = 0.2;
        failsafe_and_success(g_40_100, "40%-100% 0.2");

        scenario.gp_pass = 0.3;
        failsafe_and_success(g_40_100, "40%-100% +0.3");

        scenario.gp_pass = 0.4;
        failsafe_and_success(g_40_100, "40%-100% +0.4");

        scenario.gp_pass = 0.5;
        failsafe_and_success(g_40_100, "40%-100% +0.5");

        scenario.glidepath               = false;
        scenario.portfolio[0].allocation = 60;
        scenario.portfolio[1].allocation = 40;
        failsafe_and_success(g_60_100, "Static 60%");
        success_only(g_60_80, "Static 60%");
        success_only(g_40_100, "Static 60%");
        success_only(g_40_80, "Static 60%");

        scenario.glidepath = true;
        scenario.gp_goal   = 80.0f;

        scenario.gp_pass = 0.2;
        failsafe_and_success(g_60_80, "60%-80% +0.2");

        scenario.gp_pass = 0.3;
        failsafe_and_success(g_60_80, "60%-80% +0.3");

        scenario.gp_pass = 0.4;
        failsafe_and_success(g_60_80, "60%-80% +0.4");

        scenario.gp_pass = 0.5;
        failsafe_and_success(g_60_80, "60%-80% +0.5");

        scenario.glidepath = true;
        scenario.gp_goal   = 100.0f;

        scenario.gp_pass = 0.2;
        failsafe_and_success(g_60_100, "60%-100% +0.2");

        scenario.gp_pass = 0.3;
        failsafe_and_success(g_60_100, "60%-100% +0.3");

        scenario.gp_pass = 0.4;
        failsafe_and_success(g_60_100, "60%-100% +0.4");

        scenario.gp_pass = 0.5;
        failsafe_and_success(g_60_100, "60%-100% +0.5");

        scenario.glidepath               = false;
        scenario.portfolio[0].allocation = 80;
        scenario.portfolio[1].allocation = 20;
        failsafe_and_success(g_80_100, "Static 80%");
        success_only(g_60_100, "Static 80%");
        success_only(g_60_80, "Static 80%");
        success_only(g_40_80, "Static 80%");
        success_only(g_40_100, "Static 80%");

        scenario.glidepath = true;
        scenario.gp_goal   = 100.0f;

        scenario.gp_pass = 0.2;
        failsafe_and_success(g_80_100, "80%-100% +0.2");

        scenario.gp_pass = 0.3;
        failsafe_and_success(g_80_100, "80%-100% +0.3");

        scenario.gp_pass = 0.4;
        failsafe_and_success(g_80_100, "80%-100% +0.4");

        scenario.gp_pass = 0.5;
        failsafe_and_success(g_80_100, "80%-100% +0.5");

        scenario.glidepath               = false;
        scenario.portfolio[0].allocation = 100;
        scenario.portfolio[1].allocation = 0;
        failsafe_and_success(g_80_100, "Static 100%");
        success_only(g_60_100, "Static 100%");
        success_only(g_40_100, "Static 100%");
    } else {
        Graph r_g(graph);
        r_g.title_ = std::format("Reverse Equity Glidepaths - 100 - 80\% stocks - {} Years", scenario.years);

        scenario.glidepath               = false;
        scenario.portfolio[0].allocation = 80;
        scenario.portfolio[1].allocation = 20;
        failsafe_and_success(r_g, "Static 80%");

        scenario.portfolio[0].allocation = 100;
        scenario.portfolio[1].allocation = 0;
        failsafe_and_success(r_g, "Static 100%");

        scenario.glidepath = true;
        scenario.gp_goal   = 80.0f;

        scenario.gp_pass = -0.2;
        failsafe_and_success(r_g, "100%-80% -0.2");

        scenario.gp_pass = -0.3;
        failsafe_and_success(r_g, "100%-80% -0.3");

        scenario.gp_pass = -0.4;
        failsafe_and_success(r_g, "100%-80% -0.4");

        scenario.gp_pass = -0.5;
        failsafe_and_success(r_g, "100%-80% -0.5");
    }

    std::cout << std::endl;
    std::cout << "Portfolio;Failsafe;1%;5%;10%;25%\n";
    std::cout << failsafe_ss.str();

    return 0;
}

int failsafe_scenario(std::string_view command, const std::vector<std::string>& args) {
    const bool graph = command == "failsafe_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    float portfolio_add = 10;
    if (args.size() > 7) {
        portfolio_add = atof(args[7].c_str());
    }

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph g(graph, "Failsafe SWR (%)");
    g.title_  = std::format("Failsafe Withdrawal Rates - {} Years - {}-{}", scenario.years, scenario.start_year, scenario.end_year);
    g.xtitle_ = "Stocks Allocation (%)";

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        std::cout << "Portfolio;Failsafe;1%;5%;10%;25%\n";

        if (g.enabled_) {
            std::map<float, float> results;

            for (size_t i = 0; i <= 100; i += portfolio_add) {
                scenario.portfolio[0].allocation = float(i);
                scenario.portfolio[1].allocation = float(100 - i);

                failsafe_swr("", scenario, 6.0f, 0.0f, 0.01f, std::cout);
                results[i] = failsafe_swr_one(scenario, 6.0f, 0.0f, 0.01f, 0.0f);
            }

            g.add_legend("Failsafe SWR");
            g.add_data(results);
        } else {
            for (size_t i = 0; i <= 100; i += portfolio_add) {
                scenario.portfolio[0].allocation = float(i);
                scenario.portfolio[1].allocation = float(100 - i);

                failsafe_swr("", scenario, 6.0f, 0.0f, 0.01f, std::cout);
            }
        }
    } else {
        std::cout << "Portfolio;Failsafe;1%;5%;10%;25%\n";
        swr::normalize_portfolio(scenario.portfolio);
        failsafe_swr("", scenario, 6.0f, 0.0f, 0.01f, std::cout);
    }

    return 0;
}

int data_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        std::cout << "Not enough arguments for data_graph" << std::endl;
        return 1;
    }

    size_t start_year = atoi(args[1].c_str());
    size_t end_year   = atoi(args[2].c_str());
    auto   portfolio  = swr::parse_portfolio(args[3], false);
    auto   values     = swr::load_values(portfolio);

    Graph graph(true);

    for (size_t i = 0; i < portfolio.size(); ++i) {
        graph.add_legend(asset_to_string_percent(portfolio[i].asset));

        std::map<float, float> results;
        float                  acc_value = 1;

        for (auto& value : values[i]) {
            if (value.year >= start_year) {
                if (value.month == 12) {
                    results[value.year] = acc_value;
                }
                acc_value *= value.value;
            }
            if (value.year > end_year) {
                break;
            }
        }

        graph.add_data(results);
    }

    return 0;
}

int data_time_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 5) {
        std::cout << "Not enough arguments for data_time_graph" << std::endl;
        return 1;
    }

    size_t     start_year = atoi(args[1].c_str());
    size_t     end_year   = atoi(args[2].c_str());
    auto       portfolio  = swr::parse_portfolio(args[3], false);
    auto       values     = swr::load_values(portfolio);
    const bool log        = args[4] == "log";

    TimeGraph graph(true);

    for (size_t i = 0; i < portfolio.size(); ++i) {
        graph.add_legend(asset_to_string_percent(portfolio[i].asset));

        std::map<int64_t, float> results;

        float acc_value = 1000.0f;

        for (auto& value : values[i]) {
            if (value.year >= start_year) {
                const int64_t timestamp = (value.year - 1970) * 365 * 24 * 3600 + (value.month - 1) * 31 * 24 * 3600;
                results[timestamp]      = log ? logf(acc_value) : acc_value;
                acc_value *= value.value;
            }
            if (value.year > end_year) {
                break;
            }
        }

        graph.add_data(results);
    }

    return 0;
}

int trinity_success_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for trinity_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "trinity_success_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    float portfolio_add = 25;
    if (args.size() > 7) {
        portfolio_add = atof(args[7].c_str());
    }

    float start_wr = 3.0f;
    if (args.size() > 8) {
        start_wr = atof(args[8].c_str());
    }

    float end_wr = 6.0f;
    if (args.size() > 9) {
        end_wr = atof(args[9].c_str());
    }

    float add_wr = 0.1f;
    if (args.size() > 10) {
        add_wr = atof(args[10].c_str());
    }

    if (args.size() > 11) {
        scenario.fees = atof(args[11].c_str()) / 100.0f;
    }

    if (args.size() > 12) {
        scenario.final_threshold = atof(args[12].c_str()) / 100.0f;
    }

    if (args.size() > 13) {
        scenario.final_inflation = args[13] == "true";
    }

    configure_withdrawal_method(scenario, args, 14);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    if (args.size() > 15) {
        std::string country = args[15];

        if (country == "switzerland") {
            auto exchange_data = swr::load_exchange("usd_chf");

            scenario.exchange_rates.resize(scenario.values.size());

            for (size_t i = 0; i < scenario.portfolio.size(); ++i) {
                scenario.exchange_rates[i] = exchange_data;
                if (scenario.portfolio[i].asset == "us_stocks") {
                    scenario.exchange_rates[i] = exchange_data;
                } else {
                    scenario.exchange_rates[i] = exchange_data;

                    for (auto& v : scenario.exchange_rates[i]) {
                        v.value = 1;
                    }
                }
            }
        } else {
            std::cout << "No support for country: " << country << std::endl;
            return 1;
        }
    } else {
        prepare_exchange_rates(scenario, "usd");
    }

    Graph g(graph);
    g.title_ = std::format("Retirement Success Rate - {} Years - {}-{}", scenario.years, scenario.start_year, scenario.end_year);
    g.set_extra("\"legend_position\": \"bottom_left\",");

    if (!graph) {
        std::cout << "Portfolio";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }
    } else {
        swr::normalize_portfolio(scenario.portfolio);
        if (graph) {
            multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
        }
    }

    return 0;
}

int trinity_cash_graphs_scenario(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        std::cout << "Not enough arguments for trinity_cash_graphs" << std::endl;
        return 1;
    }

    swr::scenario base_scenario;

    base_scenario.years      = atoi(args[1].c_str());
    base_scenario.start_year = atoi(args[2].c_str());
    base_scenario.end_year   = atoi(args[3].c_str());

    float portfolio_add = 25;
    if (args.size() > 4) {
        portfolio_add = atof(args[4].c_str());
    }

    float start_wr = 3.0f;
    if (args.size() > 5) {
        start_wr = atof(args[5].c_str());
    }

    float end_wr = 6.0f;
    if (args.size() > 6) {
        end_wr = atof(args[6].c_str());
    }

    float add_wr = 0.1f;
    if (args.size() > 7) {
        add_wr = atof(args[7].c_str());
    }

    base_scenario.fees      = 0.1f / 100.0f;
    base_scenario.rebalance = swr::parse_rebalance("yearly");
    base_scenario.wmethod   = swr::WithdrawalMethod::STANDARD;

    Graph success_graph(true);
    success_graph.title_ = std::format("Trinity Study with Cash - {} Years - {}-{}", base_scenario.years, base_scenario.start_year, base_scenario.end_year);
    success_graph.set_extra("\"legend_position\": \"bottom_left\",");

    Graph tv_graph(true, "Average Terminal Value (USD)", "bar-graph");
    tv_graph.title_ = std::format("Terminal values with Cash - {} Years - {}-{}", base_scenario.years, base_scenario.start_year, base_scenario.end_year);
    tv_graph.set_extra("\"legend_position\": \"right\",");

    Graph duration_graph(true, "Worst Duration (months)", "line-graph");
    duration_graph.title_ = std::format("Worst duration with Cash - {} Years - {}-{}", base_scenario.years, base_scenario.start_year, base_scenario.end_year);
    duration_graph.set_extra("\"legend_position\": \"right\",");

    Graph quality_graph(true, "Quality (%)", "line-graph");
    quality_graph.title_ = std::format("Quality with Cash - {} Years - {}-{}", base_scenario.years, base_scenario.start_year, base_scenario.end_year);
    quality_graph.set_extra("\"legend_position\": \"right\",");

    {
        auto scenario_bonds           = base_scenario;
        scenario_bonds.portfolio      = swr::parse_portfolio("us_bonds:0;us_stocks:0;", true);
        scenario_bonds.values         = swr::load_values(scenario_bonds.portfolio);
        scenario_bonds.inflation_data = swr::load_inflation(scenario_bonds.values, "us_inflation");
        prepare_exchange_rates(scenario_bonds, "usd");

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario_bonds.portfolio[1].allocation = float(i);
            scenario_bonds.portfolio[0].allocation = float(100 - i);

            multiple_wr_success_graph(success_graph, "", true, scenario_bonds, start_wr, end_wr, add_wr);
            multiple_wr_avg_tv_graph(tv_graph, scenario_bonds, start_wr, end_wr, add_wr);
            multiple_wr_duration_graph(duration_graph, "", true, scenario_bonds, start_wr, end_wr, add_wr);
            multiple_wr_quality_graph(quality_graph, "", true, scenario_bonds, start_wr, end_wr, add_wr);
        }
    }

    {
        auto scenario_cash           = base_scenario;
        scenario_cash.portfolio      = swr::parse_portfolio("cash:0;us_stocks:0;", true);
        scenario_cash.values         = swr::load_values(scenario_cash.portfolio);
        scenario_cash.inflation_data = swr::load_inflation(scenario_cash.values, "us_inflation");
        prepare_exchange_rates(scenario_cash, "usd");

        for (size_t i = 0; i <= 100 - portfolio_add; i += portfolio_add) {
            scenario_cash.portfolio[1].allocation = float(i);
            scenario_cash.portfolio[0].allocation = float(100 - i);

            multiple_wr_success_graph(success_graph, "", true, scenario_cash, start_wr, end_wr, add_wr);
            multiple_wr_avg_tv_graph(tv_graph, scenario_cash, start_wr, end_wr, add_wr);
            multiple_wr_duration_graph(duration_graph, "", true, scenario_cash, start_wr, end_wr, add_wr);
            multiple_wr_quality_graph(quality_graph, "", true, scenario_cash, start_wr, end_wr, add_wr);
        }
    }

    return 0;
}

int trinity_duration_scenario(std::string_view command, const std::vector<std::string>& args) {
    bool graph = command == "trinity_duration_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    configure_withdrawal_method(scenario, args, 7);

    if (args.size() > 8) {
        scenario.fees = atof(args[8].c_str()) / 100.0f;
    }

    const float start_wr = 3.0f;
    const float end_wr   = 5.0f;
    const float add_wr   = 0.1f;

    const float portfolio_add = 20;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    if (!graph) {
        std::cout << "Portfolio";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    if (scenario.portfolio.size() != 2) {
        std::cout << "trinity_duration needs 2 assets in the portfolio" << std::endl;
        return 1;
    }

    {
        Graph g(graph, "Worst Duration (months)");
        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_duration_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_duration_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }
    }

    std::cout << std::endl;
    std::cout << std::endl;

    {
        Graph g(graph);
        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }
    }

    return 0;
}

int trinity_tv_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for trinity_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "trinity_tv_graph";

    Graph g(graph, "Value (USD)", "bar-graph");

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    configure_withdrawal_method(scenario, args, 7);

    if (args.size() > 8) {
        scenario.fees = atof(args[8].c_str()) / 100.0f;
    }

    const float start_wr = 3.0f;
    const float end_wr   = 5.0f;
    const float add_wr   = 0.25f;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    if (!graph) {
        std::cout << "Withdrawal Rate";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    swr::normalize_portfolio(scenario.portfolio);

    if (graph) {
        multiple_wr_tv_graph(g, scenario, start_wr, end_wr, add_wr);
    } else {
        multiple_wr_tv_sheets(scenario, start_wr, end_wr, add_wr);
    }

    return 0;
}

int trinity_spending_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for trinity_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "trinity_spending_graph";

    Graph g1(graph, "Average Spending (USD)", "bar-graph");
    Graph g2(graph, "Spending Trends Years", "bar-graph");

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    configure_withdrawal_method(scenario, args, 7);

    if (args.size() > 8) {
        scenario.fees = atof(args[8].c_str()) / 100.0f;
    }

    const float start_wr = 4.0f;
    const float end_wr   = 6.0f;
    const float add_wr   = 0.1f;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    if (!graph) {
        std::cout << "Withdrawal Rate";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    swr::normalize_portfolio(scenario.portfolio);

    if (graph) {
        multiple_wr_spending_graph(g1, scenario, start_wr, end_wr, add_wr);
        multiple_wr_spending_trend_graph(g2, scenario, start_wr, end_wr, add_wr);

        std::cout << "\n";
        g1.flush();
        std::cout << "\n";
        g2.flush();
    } else {
        multiple_wr_spending_sheets(scenario, start_wr, end_wr, add_wr);
    }

    return 0;
}

int income_scenario(const std::vector<std::string>& args) {
    if (args.size() < 10) {
        std::cout << "Not enough arguments for income_graph" << std::endl;
        return 1;
    }

    swr::scenario scenario;

    scenario.fees       = 0.001;
    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    if (total_allocation(scenario.portfolio) == 0.0f) {
        std::cout << "The Portfolio must be fixed" << std::endl;
        return 1;
    }

    float start_wr = atof(args[7].c_str());
    float end_wr   = atof(args[8].c_str());
    float add_wr   = atof(args[9].c_str());

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    scenario.extra_income = true;

    prepare_exchange_rates(scenario, "usd");

    Graph g(true);

    swr::normalize_portfolio(scenario.portfolio);

    auto run = [&](const std::string_view title, float coverage) {
        scenario.extra_income_coverage = coverage;

        multiple_wr_success_graph(g, title, false, scenario, start_wr, end_wr, add_wr);
    };

    run("0", 0.00f);
    run("5000", 0.05f);
    run("10000", 0.10f);
    run("20000", 0.20f);
    run("30000", 0.30f);
    run("40000", 0.40f);
    run("50000", 0.50f);

    return 0;
}

int social_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 11) {
        std::cout << "Not enough arguments for social_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "social_graph";

    swr::scenario scenario;

    scenario.fees       = 0.001;
    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    if (total_allocation(scenario.portfolio) == 0.0f) {
        std::cout << "The Portfolio must be fixed" << std::endl;
        return 1;
    }

    float start_wr = atof(args[7].c_str());
    float end_wr   = atof(args[8].c_str());
    float add_wr   = atof(args[9].c_str());

    scenario.social_security = true;
    scenario.social_delay    = atoi(args[10].c_str());

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph g(graph);

    if (!graph) {
        std::cout << "Coverage";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    swr::normalize_portfolio(scenario.portfolio);

    auto run = [&](const std::string_view title, float coverage) {
        scenario.social_coverage = coverage;
        if (graph) {
            multiple_wr_success_graph(g, title, false, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets(title, scenario, start_wr, end_wr, add_wr);
        }
    };

    run("0%", 0.00f);
    run("5%", 0.05f);
    run("10%", 0.10f);
    run("20%", 0.20f);
    run("30%", 0.30f);
    run("40%", 0.40f);
    run("50%", 0.50f);

    return 0;
}

int social_pf_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 12) {
        std::cout << "Not enough arguments for social_pf_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "social_pf_graph";

    swr::scenario scenario;

    scenario.fees       = 0.001;
    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    if (total_allocation(scenario.portfolio) != 0.0f) {
        std::cout << "The Portfolio must be open" << std::endl;
        return 1;
    }

    float start_wr = atof(args[7].c_str());
    float end_wr   = atof(args[8].c_str());
    float add_wr   = atof(args[9].c_str());

    scenario.social_security = true;
    scenario.social_delay    = atoi(args[10].c_str());
    auto base_coverage       = atof(args[11].c_str()) / 100.0f;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph g(graph);

    if (!graph) {
        std::cout << "Portfolio";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    for (size_t i = 0; i <= 100; i += 20) {
        scenario.portfolio[0].allocation = float(i);
        scenario.portfolio[1].allocation = float(100 - i);

        scenario.social_coverage = 0.0f;
        if (graph) {
            multiple_wr_success_graph(g, portfolio_to_string(scenario, false) + " - 0%", false, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets(portfolio_to_string(scenario, false) + " - 0%", scenario, start_wr, end_wr, add_wr);
        }
        scenario.social_coverage = base_coverage;
        if (graph) {
            multiple_wr_success_graph(g, portfolio_to_string(scenario, false) + " -" + args[11] + "%", false, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets(portfolio_to_string(scenario, false) + +" - " + args[11] + "%", scenario, start_wr, end_wr, add_wr);
        }
    }

    return 0;
}

int current_wr_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for current_wr" << std::endl;
        return 1;
    }

    const bool graph = command == "current_wr_graph";

    swr::scenario scenario;

    scenario.wmethod    = swr::WithdrawalMethod::CURRENT;
    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);
    scenario.fees       = 0.001; // TER = 0.1%

    float portfolio_add = 25;

    float start_wr = 3.0f;
    float end_wr   = 6.0f;
    float add_wr   = 0.1f;

    if (args.size() > 7) {
        portfolio_add = atof(args[7].c_str());
    }

    if (args.size() > 8) {
        start_wr = atof(args[8].c_str());
    }

    if (args.size() > 9) {
        end_wr = atof(args[9].c_str());
    }

    if (args.size() > 10) {
        add_wr = atof(args[10].c_str());
    }

    if (args.size() > 11) {
        scenario.minimum = atoi(args[11].c_str()) / 100.0f;
    }

    if (args.size() > 12 && args[12] == "standard") {
        scenario.wmethod = swr::WithdrawalMethod::STANDARD;
    }

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    if (!graph) {
        std::cout << "Portfolio";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    if (total_allocation(scenario.portfolio) == 0.0f) {
        Graph success_graph(graph);
        Graph withdrawn_graph(graph, "Money withdrawn per year");
        Graph duration_graph(graph, "Worst Duration (months)");

        if (scenario.minimum == 0.0f) {
            duration_graph.title_ = withdrawn_graph.title_ = success_graph.title_ = std::format("Withdraw from current portfolio - {} Years", scenario.years);
        } else {
            duration_graph.title_ = withdrawn_graph.title_ = success_graph.title_ =
                    std::format("Withdraw from current portfolio (Min: {}%) - {} Years", args[11], scenario.years);
        }

        success_graph.set_extra("\"legend_position\": \"bottom_left\",");
        withdrawn_graph.set_extra("\"legend_position\": \"bottom_right\",");
        duration_graph.set_extra("\"legend_position\": \"bottom_left\",");

        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_success_graph(success_graph, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }

        std::cout << '\n';

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_withdrawn_graph(withdrawn_graph, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_withdrawn_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }

        std::cout << '\n';

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            if (graph) {
                multiple_wr_duration_graph(duration_graph, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_duration_sheets("", scenario, start_wr, end_wr, add_wr);
            }
        }
    } else {
        Graph g(graph);
        swr::normalize_portfolio(scenario.portfolio);

        if (graph) {
            multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
        }
    }
    std::cout << "\n";

    return 0;
}

int rebalance_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 6) {
        std::cout << "Not enough arguments for rebalance_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "rebalance_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];

    const float start_wr = 3.0f;
    const float end_wr   = 6.0f;
    const float add_wr   = 0.1f;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph g(graph);
    g.title_ = portfolio_to_blog_string(scenario, false) + " - " + std::to_string(scenario.years) + " Years - Rebalance method";
    g.set_extra("\"legend_position\": \"bottom_left\",");

    if (!graph) {
        std::cout << "Rebalance";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    auto start = std::chrono::high_resolution_clock::now();

    swr::normalize_portfolio(scenario.portfolio);

    if (graph) {
        scenario.rebalance = swr::Rebalancing::NONE;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.rebalance = swr::Rebalancing::MONTHLY;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.rebalance = swr::Rebalancing::YEARLY;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);
    } else {
        scenario.rebalance = swr::Rebalancing::NONE;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.rebalance = swr::Rebalancing::MONTHLY;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.rebalance = swr::Rebalancing::YEARLY;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\nComputed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration)
              << "/s) \n\n";

    return 0;
}

int threshold_rebalance_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 6) {
        std::cout << "Not enough arguments for threshold_rebalance_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "threshold_rebalance_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], false);
    auto inflation      = args[5];

    const float start_wr = 3.0f;
    const float end_wr   = 6.0f;
    const float add_wr   = 0.1f;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph g(graph);
    g.title_ = portfolio_to_blog_string(scenario, false) + " - " + std::to_string(scenario.years) + " Years - Rebalance threshold";
    g.set_extra("\"legend_position\": \"bottom_left\",");

    if (!graph) {
        std::cout << "Rebalance";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    auto start = std::chrono::high_resolution_clock::now();

    swr::normalize_portfolio(scenario.portfolio);

    scenario.rebalance = swr::Rebalancing::THRESHOLD;

    if (graph) {
        scenario.threshold = 0.01;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.02;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.05;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.10;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.25;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.50;
        multiple_rebalance_graph(g, scenario, start_wr, end_wr, add_wr);
    } else {
        scenario.threshold = 0.01;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.02;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.05;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.10;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.25;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

        scenario.threshold = 0.50;
        multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\nComputed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration)
              << "/s)\n\n";

    return 0;
}

int trinity_low_yield_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 8) {
        std::cout << "Not enough arguments for trinity_low_yield_sheets" << std::endl;
        return 1;
    }

    const bool graph = command == "trinity_low_yield_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);
    float yield_adjust  = atof(args[7].c_str());

    const float start_wr = 3.0f;
    const float end_wr   = 5.0f;
    const float add_wr   = 0.1f;

    const float portfolio_add = 10;

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    auto real_scenario = scenario;

    Graph g(graph);
    Graph gp(graph && yield_adjust < 1.0f);

    g.set_extra("\"ymax\": 100, \"legend_position\": \"bottom_left\",");
    gp.set_extra("\"ymax\": 100, \"legend_position\": \"bottom_left\",");

    if (args[7] == "1.0") {
        g.title_ = "Success Rates with Historical Yields";
    } else {
        g.title_  = std::format("Success Rates with {}% of the Historical Yields", static_cast<uint32_t>(yield_adjust * 100));
        gp.title_ = std::format("Success Rates with {}% of the Historical Yields - Portfolios", static_cast<uint32_t>(yield_adjust * 100));
    }

    if (!graph) {
        std::cout << "Portfolio";
        for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
            std::cout << ";" << wr << "%";
        }
        std::cout << "\n";
    }

    if (yield_adjust < 1.0f) {
        for (size_t i = 0; i < scenario.portfolio.size(); ++i) {
            if (scenario.portfolio[i].asset == "us_bonds") {
                for (auto& value : scenario.values[i]) {
                    // We must adjust only the part above 1.0f
                    value.value = 1.0f + (value.value - 1.0f) * yield_adjust;
                }

                break;
            }
        }
    }

    prepare_exchange_rates(scenario, "usd");
    prepare_exchange_rates(real_scenario, "usd");

    auto start = std::chrono::high_resolution_clock::now();

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            real_scenario.portfolio[0].allocation = float(i);
            real_scenario.portfolio[1].allocation = float(100 - i);

            if (g.enabled_) {
                multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
            } else {
                multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
            }

            if (gp.enabled_) {
                if (i == 60 || i == 40) {
                    multiple_wr_success_graph(gp,
                                              std::format("{} ({}%)", portfolio_to_string(scenario, true), static_cast<uint32_t>(yield_adjust * 100)),
                                              true,
                                              scenario,
                                              start_wr,
                                              end_wr,
                                              add_wr);
                    multiple_wr_success_graph(
                            gp, std::format("{} ({}%)", portfolio_to_string(scenario, true), 100), true, real_scenario, start_wr, end_wr, add_wr);
                }
            }
        }

    } else {
        swr::normalize_portfolio(scenario.portfolio);

        if (g.enabled_) {
            multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
        } else {
            multiple_wr_success_sheets("", scenario, start_wr, end_wr, add_wr);
        }
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\nComputed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration)
              << "/s)\n\n";

    if (gp.enabled_) {
        g.flush();
        std::cout << "\n\n";
    }

    return 0;
}

int flexibility_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 12) {
        std::cout << "Not enough arguments for flexibility_graph" << std::endl;
        return 1;
    }

    swr::scenario scenario;

    scenario.years       = atoi(args[1].c_str());
    scenario.start_year  = atoi(args[2].c_str());
    scenario.end_year    = atoi(args[3].c_str());
    scenario.portfolio   = swr::parse_portfolio(args[4], true);
    auto inflation       = args[5];
    scenario.rebalance   = swr::parse_rebalance(args[6]);
    scenario.flexibility = swr::Flexibility::NONE;

    if (args[7] == "market") {
        scenario.flexibility = swr::Flexibility::MARKET;
    } else if (args[7] == "portfolio") {
        scenario.flexibility = swr::Flexibility::PORTFOLIO;
    } else {
        std::cout << "Invalid flexibility parameter" << std::endl;
        return 1;
    }

    scenario.flexibility_threshold_1 = atof(args[8].c_str()) / 100.0f;
    scenario.flexibility_change_1    = atof(args[9].c_str()) / 100.0f;
    scenario.flexibility_threshold_2 = atof(args[10].c_str()) / 100.0f;
    scenario.flexibility_change_2    = atof(args[11].c_str()) / 100.0f;

    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    const float portfolio_add = 20;
    const float start_wr      = 3.0f;
    const float end_wr        = 6.0f;
    const float add_wr        = 0.1f;

    Graph g(true);

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = float(i);
            scenario.portfolio[1].allocation = float(100 - i);

            multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
        }
    } else {
        swr::normalize_portfolio(scenario.portfolio);
        multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
    }

    return 0;
}

int flexibility_auto_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 8) {
        std::cout << "Not enough arguments for flexibility_auto_graph" << std::endl;
        return 1;
    }

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);
    auto flexibility    = swr::Flexibility::NONE;

    if (args[7] == "market") {
        flexibility = swr::Flexibility::MARKET;
    } else if (args[7] == "portfolio") {
        flexibility = swr::Flexibility::PORTFOLIO;
    } else {
        std::cout << "Invalid flexibility parameter" << std::endl;
        return 1;
    }

    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    const float success_start_wr   = 3.5f;
    const float success_end_wr     = 5.5f;
    const float withdrawn_start_wr = 3.5f;
    const float withdrawn_end_wr   = 4.5f;
    const float errors_start_wr    = 3.5f;
    const float errors_end_wr      = 5.5f;

    const float add_wr = 0.1f;

    Graph successGraph(true);
    Graph withdrawnGraph(true, "Withdrawn per year (CHF)");
    Graph errorsGraph(true, "Error Rate (%)");

    swr::normalize_portfolio(scenario.portfolio);

    scenario.flexibility    = swr::Flexibility::NONE;
    const auto base_results = multiple_wr_success_graph_save(successGraph, "Zero", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "Zero", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);

    scenario.flexibility = flexibility;

    scenario.flexibility_threshold_1 = 0.90f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.80f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "90/5 80/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "90/5 80/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "90/5 80/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.90f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.80f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "90/10 80/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "90/10 80/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "90/10 80/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.95f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.90f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "95/5 90/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "95/5 90/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "95/5 90/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.95f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.90f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "95/10 90/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "95/10 90/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "95/10 90/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility = flexibility;

    scenario.flexibility_threshold_1 = 0.80f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.60f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "80/5 60/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "80/5 60/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "80/5 60/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.80f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.60f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "80/10 60/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    multiple_wr_withdrawn_graph(withdrawnGraph, "80/10 60/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    multiple_wr_errors_graph(errorsGraph, "80/10 60/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    successGraph.flush();
    std::cout << "\n\n";

    withdrawnGraph.flush();
    std::cout << "\n\n";

    errorsGraph.flush();
    std::cout << "\n\n";

    return 0;
}

int times_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 8) {
        std::cout << "Not enough arguments for times_graph" << std::endl;
        return 1;
    }

    swr::scenario scenario;

    scenario.years          = atoi(args[1].c_str());
    scenario.start_year     = atoi(args[2].c_str());
    scenario.end_year       = atoi(args[3].c_str());
    scenario.portfolio      = swr::parse_portfolio(args[4], true);
    auto inflation          = args[5];
    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);
    scenario.rebalance      = swr::parse_rebalance(args[6]);
    scenario.wr             = 4.0f;
    const bool normalize    = args[7] == "true";
    const bool log          = args.size() > 8 ? (args[8] == "log") : false;
    const bool worst        = args.size() > 9 ? (args[9] == "worst") : false;

    std::cout << "Normalize: " << normalize << "\n";
    std::cout << "Log scale: " << log << "\n";
    std::cout << "Worst: " << worst << "\n";
    std::cout << "Portfolio: \n";

    for (auto& position : scenario.portfolio) {
        std::cout << " " << position.asset << ": " << position.allocation << "%\n";
    }

    std::cout << scenario << std::endl;

    if (!prepare_exchange_rates(scenario, "usd")) {
        std::cout << "Error with exchange rates" << std::endl;
        return 1;
    }

    swr::normalize_portfolio(scenario.portfolio);

    auto res = swr::simulation(scenario);

    if (res.error) {
        std::cout << "Simulation error: " << res.message << std::endl;

        return 1;
    }

    std::cout << "Success rate: " << res.success_rate << std::endl;

    std::vector<std::pair<int64_t, float>> raw_data;
    std::map<int64_t, float>               data;

    size_t i = 0;
    for (size_t current_year = scenario.start_year; current_year <= scenario.end_year - scenario.years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            const auto    tv        = res.terminal_values[i++];
            const int64_t timestamp = (current_year - 1970) * 365 * 24 * 3600 + (current_month - 1) * 31 * 24 * 3600;

            if (worst) {
                if (tv == 0.0f) {
                    data[timestamp] = log ? 13.0f : 20'000;
                } else {
                    data[timestamp] = 0.0f;
                }
            } else {
                raw_data.emplace_back(timestamp, tv);
            }
        }
    }

    if (!worst) {
        std::ranges::sort(raw_data, [](auto left, auto right) { return left.second > right.second; });
        raw_data.resize(raw_data.size() * 0.10);
        std::ranges::sort(raw_data, [](auto left, auto right) { return left.first < right.first; });

        auto first_timestamp = raw_data.front().first;
        auto last_timestamp  = raw_data.back().first;

        for (size_t current_year = scenario.start_year; current_year <= scenario.end_year - scenario.years; ++current_year) {
            for (size_t current_month = 1; current_month <= 12; ++current_month) {
                const int64_t timestamp = (current_year - 1970) * 365 * 24 * 3600 + (current_month - 1) * 31 * 24 * 3600;

                if (!normalize || (timestamp > first_timestamp && timestamp < last_timestamp)) {
                    if (std::ranges::find_if(raw_data, [timestamp](auto value) { return value.first == timestamp; }) == raw_data.end()) {
                        raw_data.emplace_back(timestamp, 0);
                    }
                }
            }
        }

        std::ranges::sort(raw_data, [](auto left, auto right) { return left.first < right.first; });

        for (const auto& [time, tv] : raw_data) {
            data[time] = log ? (tv == 0.0f ? 0.0f : logf(tv)) : tv;
        }
    }

    TimeGraph graph(true, "Terminal Value (USD)", "line-graph");
    graph.add_data(data);

    return 0;
}

int selection_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for selection_graph" << std::endl;
        return 1;
    }

    swr::scenario scenario;

    scenario.years          = atoi(args[1].c_str());
    scenario.start_year     = atoi(args[2].c_str());
    scenario.end_year       = atoi(args[3].c_str());
    scenario.portfolio      = swr::parse_portfolio(args[4], true);
    auto inflation          = args[5];
    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    std::string test = args[6];
    if (args[6] == "none") {
        scenario.rebalance = swr::parse_rebalance(args[6]);
    } else if (args[6] != "auto" && args[6] != "gp") {
        std::cout << "Invalid arguments for selection_graph" << std::endl;
        return 1;
    }

    std::cout << "Portfolio: \n";

    for (auto& position : scenario.portfolio) {
        std::cout << " " << position.asset << ": " << position.allocation << "%\n";
    }

    if (!prepare_exchange_rates(scenario, "usd")) {
        std::cout << "Error with exchange rates" << std::endl;
        return 1;
    }

    swr::normalize_portfolio(scenario.portfolio);

    const float success_start_wr = 3.5f;
    const float success_end_wr   = 5.5f;
    const float add_wr           = 0.1f;

    Graph success_graph(true);

    if (test == "none") {
        success_graph.title_ =
                std::format("Sell stocks or bonds - {} Years - {}-{}", scenario.years, scenario.portfolio[0].allocation, scenario.portfolio[1].allocation);

        scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
        multiple_wr_success_graph(success_graph, "Alloc", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.wselection = swr::WithdrawalSelection::BONDS;
        multiple_wr_success_graph(success_graph, "Bonds", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.wselection = swr::WithdrawalSelection::STOCKS;
        multiple_wr_success_graph(success_graph, "Stocks", true, scenario, success_start_wr, success_end_wr, add_wr);
    } else if (test == "auto") {
        success_graph.title_ =
                std::format("Rebalance or not - {} Years - {}-{}", scenario.years, scenario.portfolio[0].allocation, scenario.portfolio[1].allocation);

        scenario.rebalance  = swr::Rebalancing::NONE;
        scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
        multiple_wr_success_graph(success_graph, "Alloc/None", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.rebalance  = swr::Rebalancing::YEARLY;
        scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
        multiple_wr_success_graph(success_graph, "Alloc/Yearly", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.rebalance  = swr::Rebalancing::NONE;
        scenario.wselection = swr::WithdrawalSelection::BONDS;
        multiple_wr_success_graph(success_graph, "Bonds/None", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.rebalance  = swr::Rebalancing::YEARLY;
        scenario.wselection = swr::WithdrawalSelection::BONDS;
        multiple_wr_success_graph(success_graph, "Bonds/Yearly", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.rebalance  = swr::Rebalancing::NONE;
        scenario.wselection = swr::WithdrawalSelection::STOCKS;
        multiple_wr_success_graph(success_graph, "Stocks/None", true, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.rebalance  = swr::Rebalancing::YEARLY;
        scenario.wselection = swr::WithdrawalSelection::STOCKS;
        multiple_wr_success_graph(success_graph, "Stocks/Yearly", true, scenario, success_start_wr, success_end_wr, add_wr);
    } else if (test == "gp") {
        success_graph.title_ =
                std::format("Which glidepath - {} Years - {}-{}", scenario.years, scenario.portfolio[0].allocation, scenario.portfolio[1].allocation);

        scenario.rebalance = swr::parse_rebalance("none");

        scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
        multiple_wr_success_graph(success_graph, "Alloc", false, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.wselection = swr::WithdrawalSelection::BONDS;
        multiple_wr_success_graph(success_graph, "Bonds", false, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.glidepath = true;
        scenario.gp_goal   = 100.0f;

        scenario.gp_pass = 0.2;
        multiple_wr_success_graph(
                success_graph, std::format("{}%-100% +0.2", scenario.portfolio[0].allocation), false, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.gp_pass = 0.3;
        multiple_wr_success_graph(
                success_graph, std::format("{}%-100% +0.3", scenario.portfolio[0].allocation), false, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.gp_pass = 0.4;
        multiple_wr_success_graph(
                success_graph, std::format("{}%-100% +0.4", scenario.portfolio[0].allocation), false, scenario, success_start_wr, success_end_wr, add_wr);

        scenario.gp_pass = 0.5;
        multiple_wr_success_graph(
                success_graph, std::format("{}%-100% +0.5", scenario.portfolio[0].allocation), false, scenario, success_start_wr, success_end_wr, add_wr);
    }

    success_graph.flush();
    std::cout << "\n\n";

    return 0;
}

int trinity_cash_graph_scenario(std::string_view command, const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::cout << "Not enough arguments for trinity_cash" << std::endl;
        return 1;
    }

    const bool graph = command == "trinity_cash_graph";

    swr::scenario scenario;

    scenario.years      = atoi(args[1].c_str());
    scenario.start_year = atoi(args[2].c_str());
    scenario.end_year   = atoi(args[3].c_str());
    scenario.portfolio  = swr::parse_portfolio(args[4], true);
    auto inflation      = args[5];
    scenario.rebalance  = swr::parse_rebalance(args[6]);

    float portfolio_add = 25;
    if (args.size() > 7) {
        portfolio_add = atof(args[7].c_str());
    }

    float wr = 4.0f;
    if (args.size() > 8) {
        wr = atof(args[8].c_str());
    }

    scenario.cash_simple = true;
    if (args.size() > 9) {
        scenario.cash_simple = args[9] == "true";
    }

    bool compare = false;
    if (args.size() > 10) {
        compare = args[10] == "true";
    }

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    prepare_exchange_rates(scenario, "usd");

    Graph success_graph(graph);
    success_graph.xtitle_ = "Months of cash";
    if (compare) {
        success_graph.title_ = std::format("Cash Cushion vs Lower WR - {} Years - {}-{}", scenario.years, scenario.start_year, scenario.end_year);
    } else if (scenario.cash_simple) {
        success_graph.title_ = std::format("Simple Cash Cushion - {} Years - {}-{}", scenario.years, scenario.start_year, scenario.end_year);
    } else {
        success_graph.title_ = std::format("Smart Cash Cushion - {} Years - {}-{}", scenario.years, scenario.start_year, scenario.end_year);
    }

    auto start = std::chrono::high_resolution_clock::now();

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!" << std::endl;
            return 1;
        }

        if (!graph) {
            std::cout << "Portfolio";

            for (size_t i = 0; i <= 100; i += portfolio_add) {
                scenario.portfolio[0].allocation = float(i);
                scenario.portfolio[1].allocation = float(100 - i);

                std::cout << ";";

                for (auto& position : scenario.portfolio) {
                    if (position.allocation > 0) {
                        std::cout << position.allocation << "% " << position.asset << " ";
                    }
                }
            }

            if (compare) {
                for (size_t i = 0; i <= 100; i += portfolio_add) {
                    scenario.portfolio[0].allocation = float(i);
                    scenario.portfolio[1].allocation = float(100 - i);

                    std::cout << ";";

                    for (auto& position : scenario.portfolio) {
                        if (position.allocation > 0) {
                            std::cout << position.allocation << "% " << position.asset << " ";
                        }
                    }
                }
            }

            std::cout << "\n";
        }

        const float withdrawal = (wr / 100.0f) * scenario.initial_value;

        std::array<std::vector<swr::results>, 61> all_results;
        std::array<std::vector<swr::results>, 61> all_compare_results;

        cpp::default_thread_pool pool(std::thread::hardware_concurrency());

        for (size_t M = 0; M <= 60; ++M) {
            pool.do_task(
                    [&](size_t m) {
                        auto my_scenario = scenario;

                        my_scenario.wr           = wr;
                        my_scenario.initial_cash = m * ((scenario.initial_value * (my_scenario.wr / 100.0f)) / 12);

                        for (size_t i = 0; i <= 100; i += portfolio_add) {
                            my_scenario.portfolio[0].allocation = float(i);
                            my_scenario.portfolio[1].allocation = float(100 - i);

                            all_results[m].push_back(swr::simulation(my_scenario));
                        }

                        if (compare) {
                            float total              = scenario.initial_value + m * (withdrawal / 12.0f);
                            my_scenario.wr           = 100.0f * (withdrawal / total);
                            my_scenario.initial_cash = 0;

                            for (size_t i = 0; i <= 100; i += portfolio_add) {
                                my_scenario.portfolio[0].allocation = float(i);
                                my_scenario.portfolio[1].allocation = float(100 - i);

                                all_compare_results[m].push_back(swr::simulation(my_scenario));
                            }
                        }
                    },
                    M);
        }

        pool.wait();

        if (graph) {
            for (size_t i = 0, j = 0; i <= 100; i += portfolio_add, j++) {
                auto my_scenario = scenario;

                my_scenario.portfolio[0].allocation = float(i);
                my_scenario.portfolio[1].allocation = float(100 - i);

                if (compare) {
                    success_graph.add_legend(portfolio_to_string(my_scenario, true) + " CC");
                } else {
                    success_graph.add_legend(portfolio_to_string(my_scenario, true));
                }

                std::map<float, float> results;

                for (size_t m = 0; m <= 60; ++m) {
                    results[m] = all_results.at(m).at(j).success_rate;
                }

                success_graph.add_data(results);
            }

            if (compare) {
                for (size_t i = 0, j = 0; i <= 100; i += portfolio_add, j++) {
                    auto my_scenario = scenario;

                    my_scenario.portfolio[0].allocation = float(i);
                    my_scenario.portfolio[1].allocation = float(100 - i);

                    success_graph.add_legend(portfolio_to_string(my_scenario, true) + " WR");

                    std::map<float, float> results;

                    for (size_t m = 0; m <= 60; ++m) {
                        results[m] = all_compare_results.at(m).at(j).success_rate;
                    }

                    success_graph.add_data(results);
                }
            }
        } else {
            for (size_t m = 0; m <= 60; ++m) {
                std::cout << m;

                for (auto& results : all_results[m]) {
                    std::cout << ';' << results.success_rate;
                }

                if (compare) {
                    for (auto& results : all_compare_results[m]) {
                        std::cout << ';' << results.success_rate;
                    }
                }

                std::cout << "\n";
            }
        }

    } else {
        swr::normalize_portfolio(scenario.portfolio);

        std::cout << "Portfolio; ";

        for (auto& position : scenario.portfolio) {
            if (position.allocation > 0) {
                std::cout << position.allocation << "% " << position.asset << " ";
            }
        }

        std::cout << "\n";

        for (size_t m = 0; m <= 60; ++m) {
            scenario.initial_cash = m * ((scenario.initial_value * (scenario.wr / 100.0f)) / 12);
            auto results          = swr::simulation(scenario);
            std::cout << m << ';' << results.success_rate;
            std::cout << "\n";
        }
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms (" << 1000 * (swr::simulations_ran() / duration) << "/s)"
              << std::endl;

    return 0;
}

int main(int argc, const char* argv[]) {
    auto args = parse_args(argc, argv);

    if (args.empty()) {
        std::cout << "Error: Not enough arguments." << std::endl;
        print_general_help();
        return 1;
    } else {
        const auto& command = args[0];

        if (command == "fixed") {
            return fixed_scenario(args);
        } else if (command == "swr") {
            return single_swr_scenario(args);
        } else if (command == "multiple_wr") {
            return multiple_swr_scenario(args);
        } else if (command == "withdraw_frequency" || command == "withdraw_frequency_graph") {
            return withdraw_frequency_scenario(command, args);
        } else if (command == "frequency") {
            return frequency_scenario(args);
        } else if (command == "analysis") {
            return analysis_scenario(args);
        } else if (command == "portfolio_analysis") {
            return portfolio_analysis_scenario(args);
        } else if (command == "allocation") {
            return allocation_scenario();
        } else if (command == "term") {
            return term_scenario(args);
        } else if (command == "glidepath" || command == "glidepath_graph" || command == "reverse_glidepath" || command == "reverse_glidepath_graph") {
            return glidepath_scenario(command, args);
        } else if (command == "failsafe" || command == "failsafe_graph") {
            return failsafe_scenario(command, args);
        } else if (command == "data_graph") {
            return data_graph_scenario(args);
        } else if (command == "data_time_graph") {
            return data_time_graph_scenario(args);
        } else if (command == "trinity_success_sheets" || command == "trinity_success_graph") {
            return trinity_success_scenario(command, args);
        } else if (command == "trinity_cash_graphs") {
            return trinity_cash_graphs_scenario(args);
        } else if (command == "trinity_duration_sheets" || command == "trinity_duration_graph") {
            return trinity_duration_scenario(command, args);
        } else if (command == "trinity_tv_sheets" || command == "trinity_tv_graph") {
            return trinity_tv_scenario(command, args);
        } else if (command == "trinity_spending_sheets" || command == "trinity_spending_graph") {
            return trinity_spending_scenario(command, args);
        } else if (command == "social_sheets" || command == "social_graph") {
            return social_scenario(command, args);
        } else if (command == "social_pf_sheets" || command == "social_pf_graph") {
            return social_pf_scenario(command, args);
        } else if (command == "income_graph") {
            return income_scenario(args);
        } else if (command == "current_wr" || command == "current_wr_graph") {
            return current_wr_scenario(command, args);
        } else if (command == "rebalance_sheets" || command == "rebalance_graph") {
            return rebalance_scenario(command, args);
        } else if (command == "threshold_rebalance_sheets" || command == "threshold_rebalance_graph") {
            return threshold_rebalance_scenario(command, args);
        } else if (command == "trinity_low_yield_sheets" || command == "trinity_low_yield_graph") {
            return trinity_low_yield_scenario(command, args);
        } else if (command == "flexibility_graph") {
            return flexibility_graph_scenario(args);
        } else if (command == "flexibility_auto_graph") {
            return flexibility_auto_graph_scenario(args);
        } else if (command == "selection_graph") {
            return selection_graph_scenario(args);
        } else if (command == "trinity_cash" || command == "trinity_cash_graph") {
            return trinity_cash_graph_scenario(command, args);
        } else if (command == "times_graph") {
            return times_graph_scenario(args);
        } else if (command == "server") {
            if (args.size() < 3) {
                std::cout << "Not enough arguments for server" << std::endl;
                return 1;
            }

            std::string listen = args[1];
            auto        port   = atoi(args[2].c_str());

            httplib::Server server;

            server.Get("/api/simple", &server_simple_api);
            server.Get("/api/retirement", &server_retirement_api);
            server.Get("/api/fi_planner", &server_fi_planner_api);

            install_signal_handler();

            server_ptr = &server;
            std::cout << "Server is starting to listen on " << listen << ":" << port << std::endl;
            server.listen(listen.c_str(), port);
            std::cout << "Server has exited" << std::endl;
        } else {
            std::cout << "Unhandled command \"" << command << "\"" << std::endl;
            return 1;
        }
    }

    return 0;
}
