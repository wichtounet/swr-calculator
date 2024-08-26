//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <string>
#include <vector>

namespace swr {

struct allocation {
    std::string asset;
    float allocation;

    // Can be changed by the system
    float allocation_;
};

std::vector<allocation> parse_portfolio(std::string_view portfolio_str, bool allow_zero);
void normalize_portfolio(std::vector<allocation> & portfolio);
float total_allocation(std::vector<allocation> & portfolio);

std::ostream & operator<<(std::ostream& out, const std::vector<allocation> & portfolio);

} // namespace swr
