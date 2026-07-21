//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <vector>
#include <string>

#include "simulation.hpp"
#include "graph.hpp"

namespace swr {

int flexibility_graph_scenario(const std::vector<std::string>& args);
int flexibility_auto_graph_scenario(const std::vector<std::string>& args);

/* The utility functions */

void multiple_wr(const swr::scenario& scenario);

void multiple_rebalance_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr);
void multiple_rebalance_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr);

void multiple_wr_duration_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_duration_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);

void multiple_wr_success_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);
std::map<float, swr::results> multiple_wr_success_graph_save(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_success_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);

void multiple_wr_withdrawn_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_withdrawn_sheets(std::string_view title, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);

void multiple_wr_errors_graph(swr::Graph&                          graph,
                              std::string_view                     title,
                              bool                                 shortForm,
                              const swr::scenario&                 scenario,
                              float                                start_wr,
                              float                                end_wr,
                              float                                add_wr,
                              const std::map<float, swr::results>& base_results);

void multiple_wr_spending_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_spending_trend_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_spending_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr);

float failsafe_swr_one(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal);
void  failsafe_swr(swr::scenario& scenario, float start_wr, float end_wr, float step, float goal, std::ostream& out);
void  failsafe_swr(std::string_view title, swr::scenario& scenario, float start_wr, float end_wr, float step, std::ostream& out);

void multiple_wr_avg_tv_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr);

void multiple_wr_quality_graph(
        swr::Graph& graph, std::string_view title, bool shortForm, const swr::scenario& scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_tv_graph(swr::Graph& graph, swr::scenario scenario, float start_wr, float end_wr, float add_wr);
void multiple_wr_tv_sheets(swr::scenario scenario, float start_wr, float end_wr, float add_wr);

std::string portfolio_to_blog_string(const swr::scenario& scenario, bool shortForm);
std::string portfolio_to_string(const swr::scenario& scenario, bool shortForm);

std::string      asset_to_string(std::string_view asset);
std::string      asset_to_string_percent(std::string_view asset);
std::string_view asset_to_blog_string(std::string_view asset);

void configure_withdrawal_method(swr::scenario& scenario, std::vector<std::string> args, size_t n);

} // namespace swr
