#pragma once

#include <string>
#include <vector>

namespace swr {

struct allocation {
    std::string asset;
    float allocation;
};

std::vector<allocation> parse_portfolio(std::string portfolio_str);
void normalize_portfolio(std::vector<allocation> & portfolio);
float total_allocation(std::vector<allocation> & portfolio);

} // namespace swr
