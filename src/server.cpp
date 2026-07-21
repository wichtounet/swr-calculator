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

#include "cpp_utils/parallel.hpp"
#include "cpp_utils/thread_pool.hpp"

#include <httplib.h>

namespace {

httplib::Server* server_ptr = nullptr;

void server_signal_handler(int signum) {
    std::cout << "Received signal (" << signum << ")\n";

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

    std::cout << "Installed the signal handler\n";
}

bool check_parameters(const httplib::Request& req, httplib::Response& res, std::vector<const char*> parameters) {
    using namespace std::string_literals;
    for (auto& param : parameters) {
        if (!req.has_param(param)) {
            res.set_content(R"({"results":{"message": "Missing parameter )"s + param + R"(","error": true,}})", "text/json");
            return false;
        }
    }

    return true;
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

    if (req.has_param("withdraw_selection")) {
        auto selection = req.get_param_value("withdraw_selection");

        if (selection == "stocks") {
            scenario.wselection = swr::WithdrawalSelection::STOCKS;
        } else if (selection == "bonds") {
            scenario.wselection = swr::WithdrawalSelection::BONDS;
        } else {
            scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
        }
    } else {
        scenario.wselection = swr::WithdrawalSelection::ALLOCATION;
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

    std::cout << "DEBUG: Request " << scenario << "\n";

    swr::normalize_portfolio(scenario.portfolio);

    scenario.values         = swr::load_values(scenario.portfolio);
    scenario.inflation_data = swr::load_inflation(scenario.values, inflation);

    if (scenario.values.empty()) {
        res.set_content(R"({"results": {"message":"Error: Invalid portfolio", "error": true}})", "text/json");
        return;
    }

    if (scenario.inflation_data.empty()) {
        res.set_content(R"({"results": {"message":"Error: Invalid inflation", "error": true}})", "text/json");
        return;
    }

    if (!prepare_exchange_rates(scenario, currency)) {
        res.set_content(R"({"results": {"message":"Error: Invalid exchange data", "error": true}})", "text/json");
        return;
    }

    auto results = simulation(scenario);

    std::cout << "DEBUG: Response"
              << " error=" << results.error << " message=" << results.message << " success_rate=" << results.success_rate << "\n";

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
    ss << R"(  "message": ")" << results.message << "\",\n";
    ss << "  \"error\": " << (results.error ? "true" : "false") << "\n";
    ss << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop     = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms\n";
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
    scenario.wr          = atof(req.get_param_value("wr").c_str());
    const float sr       = atof(req.get_param_value("sr").c_str());
    const float income   = atoi(req.get_param_value("income").c_str());
    const float expenses = atoi(req.get_param_value("expenses").c_str());
    float       nw       = atoi(req.get_param_value("nw").c_str());

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

    const float returns = 7.0f;

    std::cout << "DEBUG: Retirement Request wr=" << scenario.wr << " sr=" << sr << " nw=" << nw << " income=" << income << " expenses=" << expenses
              << " rebalance=" << scenario.rebalance << "\n";

    const float fi_number = expenses * (100.0f / scenario.wr);

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
        std::cout << "ERROR: Simulation error: " << message << "\n";
    }

    std::stringstream ss;

    ss << "{ \"results\": {\n"
       << R"(  "message": ")" << message << "\",\n"
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
    std::cout << "DEBUG: Simulated in " << duration << "ms\n";
}

std::string params_to_string(const httplib::Request& req) {
    std::stringstream debug;
    debug << "[";
    std::string separator;
    for (const auto& [key, value] : req.params) {
        debug << separator << key << "=" << value;
        separator = ",";
    }
    debug << "]";
    return debug.str();
}

std::string vector_to_json(const std::vector<float>& values) {
    std::stringstream ss;

    ss << "[";

    std::string separator;
    for (const auto& value : values) {
        ss << separator << value;
        separator = ",";
    }

    ss << "]";

    return ss.str();
}

void server_fi_planner_api(const httplib::Request& req, httplib::Response& res) {
    if (!check_parameters(req,
                          res,
                          {"birth_year_1",
                           "life_expectancy",
                           "expenses",
                           "income_1",
                           "wr",
                           "sr",
                           "nw",
                           "portfolio",
                           "social_age",
                           "social_amount_1",
                           "extra_amount",
                           "returns"})) {
        return;
    }

    if (!check_parameters(req,
                          res,
                          {"situation",
                           "income_2",
                           "birth_year_2",
                           "social_amount_2",
                           "second_pillar_1_amount",
                           "second_pillar_1_age",
                           "second_pillar_1_rate",
                           "second_pillar_2_amount",
                           "second_pillar_2_age",
                           "second_pillar_2_rate",
                           "third_pillar_1_1_amount",
                           "third_pillar_1_1_age",
                           "third_pillar_2_1_amount",
                           "third_pillar_2_1_age"})) {
        return;
    }

    if (req.get_param_value("situation") != "single" && req.get_param_value("situation") != "couple") {
        res.set_content(R"({"results":{"message": "There is something wrong with the situation parameter","error": true,}})", "text/json");
        return;
    }

    std::cout << "DEBUG: FI Planner Request " << params_to_string(req) << "\n";

    auto start = std::chrono::high_resolution_clock::now();

    const std::chrono::time_point     now{std::chrono::system_clock::now()};
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(now)};

    const unsigned start_year = static_cast<unsigned>(static_cast<int>(ymd.year()));

    // Prepare the outputs
    bool        error = false;
    std::string message;

    // Parse the global parameters
    const float    wr                 = atof(req.get_param_value("wr").c_str());
    const unsigned life_expectancy    = atoi(req.get_param_value("life_expectancy").c_str());
    const float    sr                 = atof(req.get_param_value("sr").c_str());
    const float    expenses           = atof(req.get_param_value("expenses").c_str());
    const float    fi_net_worth       = atof(req.get_param_value("nw").c_str());
    const auto     portfolio_str      = req.get_param_value("portfolio");
    const auto     portfolio          = swr::parse_portfolio(portfolio_str, false);
    const float    returns_percentile = atof(req.get_param_value("returns").c_str());
    const unsigned social_age         = atoi(req.get_param_value("social_age").c_str());
    const float    extra_amount       = atof(req.get_param_value("extra_amount").c_str());

    // Parse the parameters per person
    float          income_1      = atof(req.get_param_value("income_1").c_str());
    float          income_2      = atof(req.get_param_value("income_2").c_str());
    const unsigned birth_year_1  = atoi(req.get_param_value("birth_year_1").c_str());
    const unsigned birth_year_2  = atoi(req.get_param_value("birth_year_2").c_str());
    const float    income_1_rate = atof(req.get_param_value("income_1_rate").c_str());
    const float    income_2_rate = atof(req.get_param_value("income_2_rate").c_str());

    const float social_amount_1 = atof(req.get_param_value("social_amount_1").c_str());
    float       social_amount_2 = atof(req.get_param_value("social_amount_2").c_str());

    const std::string situation = req.get_param_value("situation");

    if (birth_year_1 >= start_year) {
        res.set_content("{\"results\":{\"message\": \"There is something wrong with a birth year (too low)\",\"error\": true,}}", "text/json");
        return;
    }

    if (situation == "couple" && birth_year_2 >= start_year) {
        res.set_content("{\"results\":{\"message\": \"There is something wrong with a birth year (too low)\",\"error\": true,}}", "text/json");
        return;
    }

    const unsigned age_1 = start_year - birth_year_1;
    const unsigned age_2 = start_year - birth_year_2;

    const unsigned death_year_1 = start_year + (life_expectancy - age_1);
    const unsigned death_year_2 = start_year + (life_expectancy - age_2);

    const unsigned social_year_1 = social_age > age_1 ? start_year + (social_age - age_1) : start_year;
    const unsigned social_year_2 = social_age > age_2 ? start_year + (social_age - age_2) : start_year;

    if (age_1 >= life_expectancy) {
        res.set_content("{\"results\":{\"message\": \"There is something wrong with a birth year (age too high)\",\"error\": true,}}", "text/json");
        return;
    }

    if (situation == "couple" && age_2 >= life_expectancy) {
        res.set_content("{\"results\":{\"message\": \"There is something wrong with a birth year (age too high)\",\"error\": true,}}", "text/json");
        return;
    }

    float          second_pillar_1_amount = atof(req.get_param_value("second_pillar_1_amount").c_str());
    const unsigned second_pillar_1_age    = atoi(req.get_param_value("second_pillar_1_age").c_str());
    const float    second_pillar_1_rate   = atof(req.get_param_value("second_pillar_1_rate").c_str());
    const unsigned second_pillar_1_year   = second_pillar_1_age > age_1 ? start_year + (second_pillar_1_age - age_1) : start_year;

    float          second_pillar_2_amount = atof(req.get_param_value("second_pillar_2_amount").c_str());
    const unsigned second_pillar_2_age    = atoi(req.get_param_value("second_pillar_2_age").c_str());
    const float    second_pillar_2_rate   = atof(req.get_param_value("second_pillar_2_rate").c_str());
    const unsigned second_pillar_2_year   = second_pillar_2_age > age_2 ? start_year + (second_pillar_2_age - age_2) : start_year;

    float          third_pillar_1_1_amount = atof(req.get_param_value("third_pillar_1_1_amount").c_str());
    const unsigned third_pillar_1_1_age    = atoi(req.get_param_value("third_pillar_1_1_age").c_str());
    const unsigned third_pillar_1_1_year   = third_pillar_1_1_age > age_1 ? start_year + (third_pillar_1_1_age - age_1) : start_year;

    float          third_pillar_2_1_amount = atof(req.get_param_value("third_pillar_2_1_amount").c_str());
    const unsigned third_pillar_2_1_age    = atoi(req.get_param_value("third_pillar_2_1_age").c_str());
    const unsigned third_pillar_2_1_year   = third_pillar_2_1_age > age_2 ? start_year + (third_pillar_2_1_age - age_2) : start_year;

    // Compute yearly and monthly returns based on CAGR returns
    auto        cagr_returns        = swr::to_cagr_returns(portfolio, 20);
    const float factor              = 75.0f;
    const float returns             = factor * swr::percentile(cagr_returns, returns_percentile);
    const float monthly_returns     = std::powf(1.0f + returns / 100.0f, 1.0f / 12.0f) - 1.0f; // Geometric computation of the monthly returns
    const float monthly_returns_mut = 1.0f + monthly_returns;

    const float fi_number = expenses * (100.0f / wr);

    // Estimate of the number of months until retirement
    size_t months = 0;

    std::vector<float> liquidity;
    std::vector<float> net_worth;

    // When single, we need to zero out some values
    if (situation == "single") {
        income_2                = 0;
        social_amount_2         = 0;
        second_pillar_2_amount  = 0;
        third_pillar_2_1_amount = 0;
    }

    float liquid   = fi_net_worth;
    float illiquid = second_pillar_1_amount + second_pillar_2_amount;

    float current_nw                = illiquid + liquid;
    float current_withdrawal_amount = expenses;

    const bool fi_already = current_nw >= fi_number;
    bool       fi         = fi_already;

    float contribution_3a = 7258;

    auto update_second_eom = [&](size_t year, size_t month, bool fi, float& amount, float rate, unsigned withdraw_year, unsigned income, size_t age) {
        if (amount) {
            if (year >= withdraw_year) {
                // Transfer second pillar to liquid net worth
                liquid += amount;
                amount = 0;
            } else {
                if (fi) {
                    // Once we reach FI, the second pillar is transferred to a vested benefits account
                    // So it grows normally but is still illiquid
                    amount *= monthly_returns_mut;
                } else {
                    // Traditionally, second pillars only get interest once a year
                    if (year != start_year && month == 1) {
                        amount *= (100.0f + rate) / 100.0f;
                    }

                    // Monthly contribution based on income

                    const size_t current_age = age + year - start_year;

                    if (current_age < 34) {
                        amount += (income / 24.0f) * 0.07f;
                    } else if (current_age < 44) {
                        amount += (income / 24.0f) * 0.10f;
                    } else if (current_age < 54) {
                        amount += (income / 24.0f) * 0.15f;
                    } else {
                        amount += (income / 24.0f) * 0.18f;
                    }
                }

                illiquid += amount;
            }
        }
    };

    auto update_third_eom = [&](size_t year, size_t month, bool fi, float& amount, unsigned withdraw_year, unsigned income) {
        if (amount) {
            if (year >= withdraw_year) {
                // Transfer third pillar to liquid net worth
                liquid += amount;
                amount = 0;
            } else {
                // We assume that the 3a is invested the same as the portfolio
                amount *= monthly_returns_mut;

                // Annual contribution to the 3a at the beginning of the year
                if (!fi && year != start_year && month == 1) {
                    // TODO Handle multiple 3a
                    if (income > contribution_3a) {
                        amount += contribution_3a;
                    }
                }

                illiquid += amount;
            }
        }
    };

    size_t end_year = start_year + (life_expectancy - age_1);

    if (situation == "couple") {
        end_year = start_year + (life_expectancy - std::min(age_1, age_2));
    }

    for (size_t year = start_year; year < end_year; ++year) {
        liquidity.emplace_back(liquid);
        net_worth.emplace_back(current_nw);

        // Compute the liquid net worth

        for (size_t month = 0; month < 12; ++month) {
            if (!fi && current_nw < fi_number) {
                if (income_1 > contribution_3a) {
                    liquid += ((income_1 - contribution_3a) * (sr / 100.0f)) / 12.0f;
                } else {
                    liquid += (income_1 * (sr / 100.0f)) / 12.0f;
                }

                if (income_2 > contribution_3a) {
                    liquid += ((income_2 - contribution_3a) * (sr / 100.0f)) / 12.0f;
                } else {
                    liquid += (income_2 * (sr / 100.0f)) / 12.0f;
                }

                ++months; // One more month to reach FI
            } else {
                fi = true;

                // There are two cases based on social security

                auto withdrawal = current_withdrawal_amount / 12.0f;
                if (year >= social_year_1 && year <= death_year_1) {
                    withdrawal -= social_amount_1;
                }
                if (year >= social_year_2 && year <= death_year_2) {
                    withdrawal -= social_amount_2;
                }

                withdrawal -= extra_amount;

                liquid -= withdrawal;
            }

            liquid *= monthly_returns_mut;

            illiquid = 0;
            update_second_eom(year, month, fi, second_pillar_1_amount, second_pillar_1_rate, second_pillar_1_year, income_1, age_1);
            update_second_eom(year, month, fi, second_pillar_2_amount, second_pillar_2_rate, second_pillar_2_year, income_2, age_2);
            update_third_eom(year, month, fi, third_pillar_1_1_amount, third_pillar_1_1_year, income_1);
            update_third_eom(year, month, fi, third_pillar_2_1_amount, third_pillar_2_1_year, income_2);

            current_nw = liquid + illiquid;
        }

        current_withdrawal_amount *= 1.01; // Adjust for inflation

        // Grow the income
        income_1 *= 1.0f + income_1_rate / 100.0f;
        income_2 *= 1.0f + income_2_rate / 100.0f;

        if (year > start_year && year % 2 == 1) {
            contribution_3a += 120;
        }
    }

    const unsigned retirement_year  = start_year + months / 12;
    const unsigned retirement_age_1 = retirement_year - birth_year_1;
    unsigned       retirement_years = life_expectancy - retirement_age_1;

    unsigned retirement_age_2 = 0;

    if (situation == "couple") {
        retirement_age_2 = retirement_year - birth_year_2;
        retirement_years = std::max(life_expectancy - retirement_age_1, life_expectancy - retirement_age_2);
    }

    // Run the scenario through historical data to assess success rate

    float success_rate = 0.0f;

    {
        swr::scenario scenario;

        // Don't run for too long
        scenario.timeout_msecs = 200;

        scenario.wr = wr;

        // Important to configure the initial value for social security and extra income to make sense
        scenario.initial_value = std::max(fi_net_worth, fi_number);

        // Enable social security if configured (simulation expects yearly, API expects monthly)
        if (social_amount_1 > 0.0f) {
            scenario.social_security = true;
            scenario.social_delay    = retirement_year < social_year_1 ? social_year_1 - retirement_year : 0;
            scenario.social_amount   = 12.0f * social_amount_1;
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

        if (results.error) {
            error   = true;
            message = results.message;
        }

        if (error) {
            std::cout << "ERROR: Simulation error: " << message << "\n";
        }

        success_rate = results.success_rate;
    }

    std::stringstream ss;

    ss << "{ \"results\": {\n"
       << R"(  "message": ")" << message << "\",\n"
       << "  \"error\": " << (error ? "true" : "false") << ",\n"
       << "  \"separated\": true,\n"
       << "  \"fi\": " << (fi_already ? "true" : "false") << ",\n"
       << "  \"fi_number\": " << std::setprecision(2) << std::fixed << fi_number << ",\n"
       << "  \"years\": " << months / 12 << ",\n"
       << "  \"months\": " << months % 12 << ",\n"
       << "  \"retirement_year\": " << retirement_year << ",\n"
       << "  \"retirement_age\": " << retirement_age_1 << ",\n" // TODO Remove later
       << "  \"retirement_age_1\": " << retirement_age_1 << ",\n"
       << "  \"retirement_age_2\": " << retirement_age_2 << ",\n"
       << "  \"retirement_years\": " << retirement_years << ",\n"
       << "  \"success_rate\": " << success_rate << ",\n"
       << "  \"returns\": " << returns << ",\n"
       << "  \"liquidity\": " << vector_to_json(liquidity) << ",\n"
       << "  \"net_worth\": " << vector_to_json(net_worth) << "\n"
       << "}}";

    res.set_content(ss.str(), "text/json");

    auto stop     = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    std::cout << "DEBUG: Simulated in " << duration << "ms\n";
}

} // namespace

int swr::server(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Not enough arguments for server\n";
        return 1;
    }

    const std::string listen = args[1];
    const auto        port   = atoi(args[2].c_str());

    httplib::Server server;

    server.Get("/api/simple", &server_simple_api);
    server.Get("/api/retirement", &server_retirement_api);
    server.Get("/api/fi_planner", &server_fi_planner_api);

    install_signal_handler();

    server_ptr = &server;
    std::cout << "Server is starting to listen on " << listen << ":" << port << "\n";
    server.listen(listen.c_str(), port);
    std::cout << "Server has exited\n";

    return 0;
}
