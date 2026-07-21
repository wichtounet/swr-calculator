//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <string>
#include <vector>

#include "portfolio.hpp"

namespace swr {

std::vector<std::string> parse_args(int argc, const char* argv[]);

bool prepare_exchange_rates(swr::scenario& scenario, const std::string& currency);

float percentile(const std::vector<float>& v, size_t p);

std::vector<float> to_yearly_returns(const swr::data_vector& v);

std::vector<float> to_cagr_returns(const std::vector<swr::allocation>& portfolio, size_t rolling);

} // namespace swr
