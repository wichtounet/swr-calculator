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

std::vector<allocation> parse_portfolio(std::string_view portfolio_str);
void normalize_portfolio(std::vector<allocation> & portfolio);
float total_allocation(std::vector<allocation> & portfolio);

std::ostream & operator<<(std::ostream& out, const std::vector<allocation> & portfolio);

} // namespace swr
