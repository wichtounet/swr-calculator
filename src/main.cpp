#include <string>
#include <iostream>
#include <tuple>
#include <chrono>
#include <sstream>
#include <iomanip>

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

void multiple_wr(swr::scenario scenario){
    std::cout << "           Portfolio: \n";
    for (auto & position : scenario.portfolio) {
        std::cout << "             " << position.asset << ": " << position.allocation << "%\n";
    }

    std::cout << "\n";

    for (float wr = 3.0; wr < 5.1f; wr += 0.25f) {
        scenario.wr         = wr;
        scenario.monthly_wr = false;

        auto yearly_results = swr::simulation(scenario);
        std::cout << wr << "% Success Rate (Yearly): (" << yearly_results.successes << "/" << (yearly_results.failures + yearly_results.successes) << ") " << yearly_results.success_rate << "%"
                  << " [" << yearly_results.tv_average << ":" << yearly_results.tv_median << ":" << yearly_results.tv_minimum << ":" << yearly_results.tv_maximum << "]" << std::endl;

        if (yearly_results.error) {
            std::cout << "Error in simulation: " << yearly_results.message << std::endl;
            return;
        }

        scenario.monthly_wr = true;
        auto monthly_results = swr::simulation(scenario);
        std::cout << wr << "% Success Rate (Monthly): (" << monthly_results.successes << "/" << (monthly_results.failures + monthly_results.successes) << ") " << monthly_results.success_rate << "%"
                  << " [" << monthly_results.tv_average << ":" << monthly_results.tv_median << ":" << monthly_results.tv_minimum << ":" << monthly_results.tv_maximum << "]" << std::endl;

        if (monthly_results.error) {
            std::cout << "Error in simulation: " << monthly_results.message << std::endl;
            return;
        }
    }
}

void multiple_wr_success_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr){
    for (auto& position : scenario.portfolio) {
        if (position.allocation > 0) {
            std::cout << position.allocation << "% " << position.asset << " ";
        }
    }

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;
        auto monthly_results = swr::simulation(scenario);
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

void multiple_wr_tv_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr){
    std::vector<float> min_tv;
    std::vector<float> max_tv;
    std::vector<float> avg_tv;
    std::vector<float> med_tv;

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;
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

void multiple_rebalance_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr){
    if (scenario.rebalance == swr::Rebalancing::THRESHOLD) {
        std::cout << scenario.threshold << " ";
    } else {
        std::cout << scenario.rebalance << " ";
    }

    for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
        scenario.wr = wr;
        auto monthly_results = swr::simulation(scenario);
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

    swr::scenario scenario;

    // Parse the parameters
    auto portfolio_base = req.get_param_value("portfolio");
    scenario.portfolio  = swr::parse_portfolio(portfolio_base);
    auto inflation      = req.get_param_value("inflation");
    scenario.wr         = atof(req.get_param_value("wr").c_str());
    scenario.years      = atoi(req.get_param_value("years").c_str());
    scenario.start_year = atoi(req.get_param_value("start").c_str());
    scenario.end_year   = atoi(req.get_param_value("end").c_str());

    std::cout
        << "DEBUG: Request port=" << portfolio_base
        << " inf=" << inflation
        << " wr=" << scenario.wr
        << " years=" << scenario.years
        << " start_year=" << scenario.start_year
        << " end_year=" << scenario.end_year
        << std::endl;

    // For now cannot be configured
    scenario.monthly_wr = false;
    scenario.rebalance  = swr::Rebalancing::NONE;
    scenario.threshold = 0.0f;

    swr::normalize_portfolio(scenario.portfolio);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    if (scenario.values.empty()) {
        res.set_content(std::string("Error: Invalid portfolio: ") + portfolio_base, "text/plain");
        return;
    }

    if (scenario.inflation_data.empty()) {
        res.set_content("Error: Invalid inflation", "text/plain");
        return;
    }

    auto results = simulation(scenario);

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

void server_retirement_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(req, res, {"expenses", "income", "wr", "sr", "nw"})) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    swr::scenario scenario;

    // Parse the parameters
    scenario.wr       = atof(req.get_param_value("wr").c_str());
    float sr       = atof(req.get_param_value("sr").c_str());
    float income   = atoi(req.get_param_value("income").c_str());
    float expenses = atoi(req.get_param_value("expenses").c_str());
    float nw       = atoi(req.get_param_value("nw").c_str());

    float returns   = 7.0f;

    std::cout
        << "DEBUG: Retirement Request wr=" << scenario.wr
        << " sr=" << sr
        << " nw=" << nw
        << " income=" << income
        << " expenses=" << expenses
        << std::endl;

    float fi_number = expenses * (100.0f / scenario.wr);

    size_t months = 0;
    while (nw < fi_number) {
        nw *= 1.0f + (returns / 100.0f) / 12.0f;
        nw += (income * sr / 100.0f) / 12.0f;
        ++months;
    }

    // For now cannot be configured
    scenario.monthly_wr = false;
    scenario.rebalance  = swr::Rebalancing::YEARLY;
    scenario.threshold  = 0.0f;
    scenario.years      = 30;
    scenario.start_year = 1871;
    scenario.end_year   = 2018;

    auto portfolio100   = swr::parse_portfolio("us_stocks:100;");
    auto values100      = swr::load_values(portfolio100);
    auto portfolio60   = swr::parse_portfolio("us_stocks:60;us_bonds:40;");
    auto values60      = swr::load_values(portfolio60);
    auto portfolio40   = swr::parse_portfolio("us_stocks:40;us_bonds:60;");
    auto values40      = swr::load_values(portfolio40);

    scenario.inflation_data = swr::load_inflation(values100, "us_inflation");

    scenario.portfolio = portfolio100;
    scenario.values = values100;
    auto results100 = simulation(scenario);

    scenario.portfolio = portfolio60;
    scenario.values = values60;
    auto results60  = simulation(scenario);

    scenario.portfolio = portfolio40;
    scenario.values = values40;
    auto results40  = simulation(scenario);

    std::stringstream ss;

    ss << "{ \"results\": {\n"
       << "  \"message\": \"\",\n"
       << "  \"error\": false,\n"
       << "  \"fi_number\": " << std::setprecision(2) << std::fixed << fi_number << ",\n"
       << "  \"years\": " << months / 12 << ",\n"
       << "  \"months\": " << months % 12 << ",\n"
       << "  \"success_rate_100\": " << results100.success_rate << ",\n"
       << "  \"success_rate_60\": "  << results60.success_rate << ",\n"
       << "  \"success_rate_40\": "  << results40.success_rate << "\n"
       << "}}";

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

            swr::scenario scenario;

            scenario.wr         = atof(args[1].c_str());
            scenario.years      = atoi(args[2].c_str());
            scenario.start_year = atoi(args[3].c_str());
            scenario.end_year   = atoi(args[4].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[5]);
            auto inflation      = args[6];

            if (args.size() > 7) {
                scenario.fees = atof(args[7].c_str()) / 100.0f;
            }

            swr::normalize_portfolio(scenario.portfolio);

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            std::cout << "Withdrawal Rate (WR): " << scenario.wr << "%\n"
                      << "     Number of years: " << scenario.years << "\n"
                      << "               Start: " << scenario.start_year << "\n"
                      << "                 End: " << scenario.end_year << "\n"
                      << "                 TER: " << 100.0f * scenario.fees << "%\n"
                      << "           Portfolio: \n";

            for (auto & position : scenario.portfolio) {
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

            scenario.monthly_wr = false;
            auto yearly_results = swr::simulation(scenario);

            if (yearly_results.message.size()) {
                std::cout << yearly_results.message << std::endl;
            }

            if (yearly_results.error) {
                return 1;
            }

            printer("Yearly", yearly_results);

            scenario.monthly_wr = true;
            auto monthly_results = swr::simulation(scenario);

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

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[4]);
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

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "analysis") {
            if (args.size() < 2) {
                std::cout << "Not enough arguments for analysis" << std::endl;
                return 1;
            }

            size_t start_year = atoi(args[1].c_str());
            size_t end_year   = atoi(args[2].c_str());

            auto portfolio = swr::parse_portfolio("us_stocks:50;us_bonds:50;");

            auto values         = swr::load_values(portfolio);
            auto inflation_data = swr::load_inflation(values, "us_inflation");

            auto analyzer = [&](auto & v, const std::string & name) {
                float average = 0.0f;

                float worst_month = 1.0f;
                std::string worst_month_str;

                float best_month = 0.0f;
                std::string best_month_str;

                size_t negative = 0;
                size_t total = 0;

                for (auto value : v) {
                    if (value.year >= start_year && value.year <= end_year) {
                        if (value.value < worst_month) {
                            worst_month = value.value;
                            worst_month_str = std::to_string(value.year) + "." + std::to_string(value.month);
                        }

                        if (value.value > best_month) {
                            best_month = value.value;
                            best_month_str = std::to_string(value.year) + "." + std::to_string(value.month);
                        }

                        ++total;

                        if (value.value < 1.0f) {
                            ++negative;
                        }

                        average += value.value;
                    }
                }

                std::cout << name << " average returns: +" << 100.0f * ((average / total) - 1.0f) << "%" << std::endl;
                std::cout << name << " best returns: +" << 100.0f * (best_month - 1.0f) << "% (" << best_month_str << ")" << std::endl;
                std::cout << name << " worst returns: -" << 100.0f * (1.0f - worst_month) << "% (" << worst_month_str << ")" << std::endl;
                std::cout << name << " Negative months: " << negative << " (" << 100.0f * (negative / float(total)) << "%)" << std::endl;
            };

            analyzer(values[0], "Stocks");
            analyzer(values[1], "Bonds");
            analyzer(inflation_data, "Inflation");
        } else if (command == "trinity_success_sheets") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for trinity_sheets" << std::endl;
                return 1;
            }

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[4]);
            auto inflation      = args[5];
            scenario.rebalance  = swr::parse_rebalance(args[6]);

            float portfolio_add = 25;

            float start_wr = 3.0f;
            float end_wr   = 6.0f;
            float add_wr   = 0.1f;

            if (args.size() > 7){
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

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            if (args.size() > 11) {
                std::string country = args[11];

                if (country == "switzerland") {
                    auto exchange_data = swr::load_exchange("usd_chf");

                    scenario.exchanges.resize(scenario.values.size());

                    for (size_t i = 0; i < scenario.portfolio.size(); ++i) {
                        scenario.exchanges[i] = exchange_data;
                        if (scenario.portfolio[i].asset == "us_stocks") {
                            scenario.exchanges[i] = exchange_data;
                        } else {
                            scenario.exchanges[i] = exchange_data;

                            for (auto& v : scenario.exchanges[i]) {
                                v.value = 1;
                            }
                        }
                    }
                } else {
                    std::cout << "No support for country: " << country << std::endl;
                    return 1;
                }
            }

            std::cout << "Portfolio";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            if (total_allocation(scenario.portfolio) == 0.0f) {
                if (scenario.portfolio.size() != 2) {
                    std::cout << "Portfolio allocation cannot be zero!" << std::endl;
                    return 1;
                }

                for (size_t i = 0; i <= 100; i += portfolio_add) {
                    scenario.portfolio[0].allocation = float(i);
                    scenario.portfolio[1].allocation = float(100 - i);

                    multiple_wr_success_sheets(scenario, start_wr, end_wr, add_wr);
                }
            } else {
                swr::normalize_portfolio(scenario.portfolio);
                multiple_wr_success_sheets(scenario, start_wr, end_wr, add_wr);
            }
        } else if (command == "trinity_tv_sheets") {
            if (args.size() < 7) {
                std::cout << "Not enough arguments for trinity_sheets" << std::endl;
                return 1;
            }

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[4]);
            auto inflation      = args[5];
            scenario.rebalance  = swr::parse_rebalance(args[6]);

            const float start_wr = 3.0f;
            const float end_wr   = 5.0f;
            const float add_wr   = 0.25f;

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            std::cout << "Withdrawal Rate";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            swr::normalize_portfolio(scenario.portfolio);
            multiple_wr_tv_sheets(scenario, start_wr, end_wr, add_wr);
        } else if (command == "rebalance_sheets") {
            if (args.size() < 6) {
                std::cout << "Not enough arguments for rebalance_sheets" << std::endl;
                return 1;
            }

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio    = swr::parse_portfolio(args[4]);
            auto inflation        = args[5];

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            std::cout << "Rebalance";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            auto start = std::chrono::high_resolution_clock::now();

            swr::normalize_portfolio(scenario.portfolio);

            scenario.rebalance = swr::Rebalancing::NONE;
            multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

            scenario.rebalance = swr::Rebalancing::MONTHLY;
            multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

            scenario.rebalance = swr::Rebalancing::YEARLY;
            multiple_rebalance_sheets(scenario, start_wr, end_wr, add_wr);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "threshold_rebalance_sheets") {
            if (args.size() < 6) {
                std::cout << "Not enough arguments for threshold_rebalance_sheets" << std::endl;
                return 1;
            }

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[4]);
            auto inflation      = args[5];

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            std::cout << "Rebalance";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            auto start = std::chrono::high_resolution_clock::now();

            swr::normalize_portfolio(scenario.portfolio);

            scenario.rebalance = swr::Rebalancing::THRESHOLD;

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

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "Computed " << swr::simulations_ran() << " withdrawal rates in " << duration << "ms ("
                      << 1000 * (swr::simulations_ran() / duration) << "/s)" << std::endl;
        } else if (command == "trinity_low_yield_sheets") {
            if (args.size() < 8) {
                std::cout << "Not enough arguments for trinity_low_yield_sheets" << std::endl;
                return 1;
            }

            swr::scenario scenario;

            scenario.years      = atoi(args[1].c_str());
            scenario.start_year = atoi(args[2].c_str());
            scenario.end_year   = atoi(args[3].c_str());
            scenario.portfolio  = swr::parse_portfolio(args[4]);
            auto inflation      = args[5];
            scenario.rebalance  = swr::parse_rebalance(args[6]);
            float yield_adjust  = atof(args[7].c_str());

            const float start_wr = 3.0f;
            const float end_wr   = 6.0f;
            const float add_wr   = 0.1f;

            const float portfolio_add = 10;

            scenario.values         = swr::load_values(scenario.portfolio);
            scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

            std::cout << "Portfolio";
            for (float wr = start_wr; wr < end_wr + add_wr / 2.0f; wr += add_wr) {
                std::cout << ";" << wr << "%";
            }
            std::cout << "\n";

            for (size_t i = 0; i < scenario.portfolio.size(); ++i) {
                if (scenario.portfolio[i].asset == "us_bonds") {
                    for (auto & value : scenario.values[i]) {
                        value.value = value.value - ((value.value - 1.0f) * yield_adjust);
                    }

                    break;
                }
            }

            auto start = std::chrono::high_resolution_clock::now();

            if (total_allocation(scenario.portfolio) == 0.0f) {
                if (scenario.portfolio.size() != 2) {
                    std::cout << "Portfolio allocation cannot be zero!" << std::endl;
                    return 1;
                }

                for (size_t i = 0; i <= 100; i += portfolio_add) {
                    scenario.portfolio[0].allocation = float(i);
                    scenario.portfolio[1].allocation = float(100 - i);

                    multiple_wr_success_sheets(scenario, start_wr, end_wr, add_wr);
                }
            } else {
                swr::normalize_portfolio(scenario.portfolio);
                multiple_wr_success_sheets(scenario, start_wr, end_wr, add_wr);
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
            server.Get("/api/retirement", &server_retirement_api);

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
