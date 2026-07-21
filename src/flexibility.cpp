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

#include "scenarios.hpp"
#include "simulation.hpp"
#include "data.hpp"
#include "utils.hpp"
#include "graph.hpp"

int swr::flexibility_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 12) {
        std::cout << "Not enough arguments for flexibility_graph\n";
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
        std::cout << "Invalid flexibility parameter\n";
        return 1;
    }

    scenario.flexibility_threshold_1 = atof(args[8].c_str()) / 100.0f;
    scenario.flexibility_change_1    = atof(args[9].c_str()) / 100.0f;
    scenario.flexibility_threshold_2 = atof(args[10].c_str()) / 100.0f;
    scenario.flexibility_change_2    = atof(args[11].c_str()) / 100.0f;

    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    swr::prepare_exchange_rates(scenario, "usd");

    const float portfolio_add = 20;
    const float start_wr      = 3.0f;
    const float end_wr        = 6.0f;
    const float add_wr        = 0.1f;

    swr::Graph g(true);

    if (total_allocation(scenario.portfolio) == 0.0f) {
        if (scenario.portfolio.size() != 2) {
            std::cout << "Portfolio allocation cannot be zero!\n";
            return 1;
        }

        for (size_t i = 0; i <= 100; i += portfolio_add) {
            scenario.portfolio[0].allocation = static_cast<float>(i);
            scenario.portfolio[1].allocation = static_cast<float>(100 - i);

            multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
        }
    } else {
        swr::normalize_portfolio(scenario.portfolio);
        multiple_wr_success_graph(g, "", true, scenario, start_wr, end_wr, add_wr);
    }

    return 0;
}

int swr::flexibility_auto_graph_scenario(const std::vector<std::string>& args) {
    if (args.size() < 8) {
        std::cout << "Not enough arguments for flexibility_auto_graph\n";
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
        std::cout << "Invalid flexibility parameter\n";
        return 1;
    }

    scenario.wmethod        = swr::WithdrawalMethod::STANDARD;
    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    swr::prepare_exchange_rates(scenario, "usd");

    const float success_start_wr   = 3.5f;
    const float success_end_wr     = 5.5f;
    const float withdrawn_start_wr = 3.5f;
    const float withdrawn_end_wr   = 4.5f;
    const float errors_start_wr    = 3.5f;
    const float errors_end_wr      = 5.5f;

    const float add_wr = 0.1f;

    swr::Graph successGraph(true);
    Graph      withdrawnGraph(true, "Withdrawn per year (CHF)");
    Graph      errorsGraph(true, "Error Rate (%)");

    swr::normalize_portfolio(scenario.portfolio);

    scenario.flexibility    = swr::Flexibility::NONE;
    const auto base_results = swr::multiple_wr_success_graph_save(successGraph, "Zero", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "Zero", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);

    scenario.flexibility = flexibility;

    scenario.flexibility_threshold_1 = 0.90f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.80f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "90/5 80/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "90/5 80/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "90/5 80/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.90f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.80f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "90/10 80/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "90/10 80/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "90/10 80/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.95f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.90f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "95/5 90/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "95/5 90/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "95/5 90/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.95f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.90f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "95/10 90/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "95/10 90/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "95/10 90/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility = flexibility;

    scenario.flexibility_threshold_1 = 0.80f;
    scenario.flexibility_change_1    = 0.95f;
    scenario.flexibility_threshold_2 = 0.60f;
    scenario.flexibility_change_2    = 0.90f;

    multiple_wr_success_graph(successGraph, "80/5 60/10", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "80/5 60/10", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "80/5 60/10", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    scenario.flexibility_threshold_1 = 0.80f;
    scenario.flexibility_change_1    = 0.90f;
    scenario.flexibility_threshold_2 = 0.60f;
    scenario.flexibility_change_2    = 0.80f;

    multiple_wr_success_graph(successGraph, "80/10 60/20", true, scenario, success_start_wr, success_end_wr, add_wr);
    swr::multiple_wr_withdrawn_graph(withdrawnGraph, "80/10 60/20", true, scenario, withdrawn_start_wr, withdrawn_end_wr, add_wr);
    swr::multiple_wr_errors_graph(errorsGraph, "80/10 60/20", true, scenario, errors_start_wr, errors_end_wr, add_wr, base_results);

    successGraph.flush();
    std::cout << "\n\n";

    withdrawnGraph.flush();
    std::cout << "\n\n";

    errorsGraph.flush();
    std::cout << "\n\n";

    return 0;
}
