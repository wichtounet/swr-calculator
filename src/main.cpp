#include <string>
#include <iostream>
#include <tuple>
#include <chrono>
#include <sstream>

#include "data.hpp"
#include "portfolio.hpp"
#include "simulation.hpp"

#include "httplib.h"

namespace {

std::vector<std::string> parse_args(int argc, const char* argv[]){
    std::vector<std::string> args;

    for(int i = 0; i < argc - 1; ++i){
        args.emplace_back(argv[i+1]);
    }

    return args;
}

void multiple_wr(const std::vector<swr::allocation>& portfolio, const std::vector<swr::data>& inflation_data, const std::vector<std::vector<swr::data>>& values, size_t years, size_t start_year, size_t end_year, swr::Rebalancing rebalance){
    std::cout << "           Portfolio: \n";
    for (auto & position : portfolio) {
        std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
    }

    std::cout << "\n";

    for (float wr = 3.0; wr < 5.1f; wr += 0.25f) {
        auto yearly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, false, rebalance);
        std::cout << wr << "% Success Rate (Yearly): (" << yearly_results.successes << "/" << (yearly_results.failures + yearly_results.successes) << ") " << yearly_results.success_rate << "%"
                  << " [" << yearly_results.tv_average << ":" << yearly_results.tv_median << ":" << yearly_results.tv_minimum << ":" << yearly_results.tv_maximum << "]" << std::endl;

        auto monthly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, true, rebalance);
        std::cout << wr << "% Success Rate (Monthly): (" << monthly_results.successes << "/" << (monthly_results.failures + monthly_results.successes) << ") " << monthly_results.success_rate << "%"
                  << " [" << monthly_results.tv_average << ":" << monthly_results.tv_median << ":" << monthly_results.tv_minimum << ":" << monthly_results.tv_maximum << "]" << std::endl;
    }
}

void multiple_wr_success_sheets(const std::vector<swr::allocation>& portfolio, const std::vector<swr::data>& inflation_data, const std::vector<std::vector<swr::data>>& values, size_t years, size_t start_year, size_t end_year, float start_wr, float end_wr, float add_wr, swr::Rebalancing rebalance){
    for (auto& position : portfolio) {
        if (position.allocation > 0) {
            std::cout << position.allocation << "% " << position.asset << " ";
        }
    }

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        auto monthly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, true, rebalance);
        std::cout << ';' << monthly_results.success_rate;
    }

    std::cout << "\n";
}

template <typename T>
void csv_print(const std::string& header, const std::vector<T> & values) {
    std::cout << header;
    for (auto & v : values) {
        std::cout << ";" << v;
    }
    std::cout << "\n";
}

void multiple_wr_tv_sheets(const std::vector<swr::allocation>& portfolio, const std::vector<swr::data>& inflation_data, const std::vector<std::vector<swr::data>>& values, size_t years, size_t start_year, size_t end_year, float start_wr, float end_wr, float add_wr, swr::Rebalancing rebalance){
    std::vector<float> min_tv;
    std::vector<float> max_tv;
    std::vector<float> avg_tv;
    std::vector<float> med_tv;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        auto monthly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, true, rebalance);
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

void multiple_rebalance_sheets(const std::vector<swr::allocation>& portfolio, const std::vector<swr::data>& inflation_data, const std::vector<std::vector<swr::data>>& values, size_t years, size_t start_year, size_t end_year, float start_wr, float end_wr, float add_wr, swr::Rebalancing rebalance, float threshold = 0.0f){
    if (rebalance == swr::Rebalancing::THRESHOLD) {
        std::cout << threshold << " ";
    } else {
        std::cout << rebalance << " ";
    }

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        auto monthly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, true, rebalance, threshold);
        std::cout << ';' << monthly_results.success_rate;
    }

    std::cout << "\n";
}

httplib::Server * server_ptr = nullptr;

void server_signal_handler(int signum) {
    std::cout << "Received signal (" << signum << ")" << std::endl;

    if (server_ptr) {
        server_ptr->stop();
    }
}

void install_signal_handler() {
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = server_signal_handler;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    std::cout << "Installed the signal handler" << std::endl;
}

bool check_parameters(const httplib::Request& req, httplib::Response& res, std::vector<const char*> parameters) {
    for (auto& param : parameters) {
        if (!req.has_param(param)) {
            res.set_content("Error: Missing parameter " + std::string(param), "text/plain");
            return false;
        }
    }

    return true;
}

void server_simple_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(req, res, {"portfolio", "inflation", "years", "wr", "start", "end"})) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    // Parse the parameters
    auto portfolio_base = req.get_param_value("portfolio");
    auto portfolio      = swr::parse_portfolio(portfolio_base);
    auto inflation      = req.get_param_value("inflation");
    float wr            = atof(req.get_param_value("wr").c_str());
    float years         = atoi(req.get_param_value("years").c_str());
    float start_year    = atoi(req.get_param_value("start").c_str());
    float end_year      = atoi(req.get_param_value("end").c_str());

    std::cout
        << "DEBUG: Request port=" << portfolio_base
        << " inf=" << inflation
        << " wr=" << wr
        << " years=" << years
        << " start_year=" << start_year
        << " end_year=" << end_year
        << std::endl;

    // For now cannot be configured
    bool monthly_wr = false;
    auto rebalance  = swr::Rebalancing::NONE;
    float threshold = 0.0f;

    swr::normalize_portfolio(portfolio);

    auto values         = swr::load_values(portfolio);
    auto inflation_data = swr::load_inflation(values, inflation);

    if (values.empty()) {
        res.set_content(std::string("Error: Invalid portfolio: ") + portfolio_base, "text/plain");
        return;
    }

    if (inflation_data.empty()) {
        res.set_content("Error: Invalid inflation", "text/plain");
        return;
    }

    auto results = simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, monthly_wr, rebalance, threshold);

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
    ss << "  \"message\": \"" << results.message << "\",\n";
    ss << "  \"error\": " << (results.error ? "true" : "false") << "\n";
    ss << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms" << std::endl;
}

} // namespace

int main(int argc, const char* argv[]) {
    auto args = parse_args(argc, argv);

    if (args.empty()) {
        std::cout << "Not enough arguments" << std::endl;
        return 0;
    } else {
        const auto & command = args[0];

        if (command == "fixed") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for fixed" << std::endl;
                return 1;
            }

            float wr          = atof(args[1].c_str());
            size_t years      = atoi(args[2].c_str());
            size_t start_year = atoi(args[3].c_str());
            size_t end_year   = atoi(args[4].c_str());
            auto portfolio    = swr::parse_portfolio(args[5]);
            auto inflation    = args[6];

            swr::normalize_portfolio(portfolio);

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Withdrawal Rate (WR): " << wr << "%\n"
                      << "     Number of years: " << years << "\n"
                      << "               Start: " << start_year << "\n"
                      << "                 End: " << end_year << "\n"
                      << "           Portfolio: \n";

            for (auto & position : portfolio) {
                std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
            }

            auto printer = [](const std::string& message, const auto & results) {
                std::cout << "     Success Rate (" << message << "): (" << results.successes << "/" << (results.failures + results.successes) << ") " << results.success_rate
                          << " [" << results.tv_average << ":" << results.tv_median << ":" << results.tv_minimum << ":" << results.tv_maximum << "]" << std::endl;

                if (results.failures) {
                    std::cout << "         Worst duration: " << results.worst_duration << " months (" << results.worst_starting_month << "/" << results.worst_starting_year << std::endl;
                }

                std::cout << "         Highest Eff. WR: " << results.highest_eff_wr << "% ("
                          << results.highest_eff_wr_start_month << "/" << results.highest_eff_wr_start_year
                          << "->" << results.highest_eff_wr_year << ")" << std::endl;
                std::cout << "          Lowest Eff. WR: " << results.lowest_eff_wr << "% ("
                          << results.lowest_eff_wr_start_month << "/" << results.lowest_eff_wr_start_year
                          << "->" << results.lowest_eff_wr_year << ")" << std::endl;

            };

            auto start = std::chrono::high_resolution_clock::now();

            auto yearly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, false);

            if (yearly_results.message.size()) {
                std::cout << yearly_results.message << std::endl;
            }

            if (yearly_results.error) {
                return 1;
            }

            printer("Yearly", yearly_results);

            auto monthly_results = swr::simulation(portfolio, inflation_data, values, years, wr, start_year, end_year, true);

            printer("Monthly", yearly_results);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "multiple_wr") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for multiple_wr" << std::endl;
                return 1;
            }

            size_t years      = atoi(args[1].c_str());
            size_t start_year = atoi(args[2].c_str());
            size_t end_year   = atoi(args[3].c_str());
            auto portfolio    = swr::parse_portfolio(args[4]);
            auto inflation    = args[5];
            auto rebalance    = swr::parse_rebalance(args[6]);

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "     Number of years: " << years << "\n"
                      << "           Rebalance: " << rebalance << "\n"
                      << "               Start: " << start_year << "\n"
                      << "                 End: " << end_year << "\n";

            auto start = std::chrono::high_resolution_clock::now();

            if (total_allocation(portfolio) == 0.0f) {
                if (portfolio.size() != 2) {
                    std::cout << "Portfolio allocation cannot be zero!" << std::endl;
                    return 1;
                }

                for (size_t i = 0; i <= 100; i += 5) {
                    portfolio[0].allocation = float(i);
                    portfolio[1].allocation = float(100 - i);

                    multiple_wr(portfolio, inflation_data, values, years, start_year, end_year, rebalance);
                }
            } else {
                swr::normalize_portfolio(portfolio);
                multiple_wr(portfolio, inflation_data, values, years, start_year, end_year, rebalance);
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "trinity_success_sheets") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for trinity_sheets" << std::endl;
                return 1;
            }

            size_t years      = atoi(args[1].c_str());
            size_t start_year = atoi(args[2].c_str());
            size_t end_year   = atoi(args[3].c_str());
            auto portfolio    = swr::parse_portfolio(args[4]);
            auto inflation    = args[5];
            auto rebalance    = swr::parse_rebalance(args[6]);

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            const float portfolio_add = 25;

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Portfolio";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            if (total_allocation(portfolio) == 0.0f) {
                if (portfolio.size() != 2) {
                    std::cout << "Portfolio allocation cannot be zero!" << std::endl;
                    return 1;
                }

                for (size_t i = 0; i <= 100; i += portfolio_add) {
                    portfolio[0].allocation = float(i);
                    portfolio[1].allocation = float(100 - i);

                    multiple_wr_success_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, rebalance);
                }
            } else {
                swr::normalize_portfolio(portfolio);
                multiple_wr_success_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, rebalance);
            }
        } else if (command == "trinity_tv_sheets") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for trinity_sheets" << std::endl;
                return 1;
            }

            size_t years      = atoi(args[1].c_str());
            size_t start_year = atoi(args[2].c_str());
            size_t end_year   = atoi(args[3].c_str());
            auto portfolio    = swr::parse_portfolio(args[4]);
            auto inflation    = args[5];
            auto rebalance    = swr::parse_rebalance(args[6]);

            const float start_wr = 3.0f;
            const float end_wr   = 5.0f;
            const float add_wr   = 0.25f;

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Withdrawal Rate";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            swr::normalize_portfolio(portfolio);
            multiple_wr_tv_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, rebalance);
        } else if (command == "rebalance_sheets") {
            if (args.size() < 6) {
                std::cout << "Not enough arguments for rebalance_sheets" << std::endl;
                return 1;
            }

            size_t years      = atoi(args[1].c_str());
            size_t start_year = atoi(args[2].c_str());
            size_t end_year   = atoi(args[3].c_str());
            auto portfolio    = swr::parse_portfolio(args[4]);
            auto inflation    = args[5];

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Rebalance";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            auto start = std::chrono::high_resolution_clock::now();

            swr::normalize_portfolio(portfolio);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::NONE);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::MONTHLY);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::YEARLY);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "threshold_rebalance_sheets") {
            if (args.size() < 6) {
                std::cout << "Not enough arguments for threshold_rebalance_sheets" << std::endl;
                return 1;
            }

            size_t years      = atoi(args[1].c_str());
            size_t start_year = atoi(args[2].c_str());
            size_t end_year   = atoi(args[3].c_str());
            auto portfolio    = swr::parse_portfolio(args[4]);
            auto inflation    = args[5];

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Rebalance";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            auto start = std::chrono::high_resolution_clock::now();

            swr::normalize_portfolio(portfolio);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.01);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.02);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.05);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.10);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.25);
            multiple_rebalance_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, swr::Rebalancing::THRESHOLD, 0.50);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "trinity_low_yield_sheets") {
            if (args.size() < 8) {
                std::cout << "Not enough arguments for trinity_low_yield_sheets" << std::endl;
                return 1;
            }

            size_t years       = atoi(args[1].c_str());
            size_t start_year  = atoi(args[2].c_str());
            size_t end_year    = atoi(args[3].c_str());
            auto portfolio     = swr::parse_portfolio(args[4]);
            auto inflation     = args[5];
            auto rebalance     = swr::parse_rebalance(args[6]);
            float yield_adjust = atof(args[7].c_str());

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            const float portfolio_add = 10;

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, inflation);

            std::cout << "Portfolio";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            for (size_t i = 0; i < portfolio.size(); ++i) {
                if (portfolio[i].asset == "us_bonds") {
                    for (auto & value : values[i]) {
                        value.value = value.value - ((value.value - 1.0f) * yield_adjust);
                    }

                    break;
                }
            }

            auto start = std::chrono::high_resolution_clock::now();

            if (total_allocation(portfolio) == 0.0f) {
                if (portfolio.size() != 2) {
                    std::cout << "Portfolio allocation cannot be zero!" << std::endl;
                    return 1;
                }

                for (size_t i = 0; i <= 100; i += portfolio_add) {
                    portfolio[0].allocation = float(i);
                    portfolio[1].allocation = float(100 - i);

                    multiple_wr_success_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, rebalance);
                }
            } else {
                swr::normalize_portfolio(portfolio);
                multiple_wr_success_sheets(portfolio, inflation_data, values, years, start_year, end_year, start_wr, end_wr, add_wr, rebalance);
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "server") {
            if (args.size() < 3) {
                std::cout << "Not enough arguments for server" << std::endl;
                return 1;
            }

            std::string listen = args[1];
            auto port          = atoi(args[2].c_str());

            httplib::Server server;

            server.Get("/api/simple", &server_simple_api);

            install_signal_handler();

            server_ptr = &server;
            std::cout << "Server is starting to listen on localhost:8080" << std::endl;
            server.listen(listen.c_str(), port);
            std::cout << "Server has exited" << std::endl;
        } else {
            std::cout << "Unhandled command \"" << command << "\"" << std::endl;
            return 1;
        }
    }

    return 0;
}
