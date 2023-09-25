#include <algorithm>

#include "portfolio.hpp"

std::vector<swr::allocation> swr::parse_portfolio(std::string portfolio_str) {
    std::vector<allocation> portfolio;

    size_t positions = std::ranges::count(portfolio_str, ';');

    for (size_t i = 0; i < positions; ++i) {
        std::string position(portfolio_str.begin(), portfolio_str.begin() + portfolio_str.find(';'));
        portfolio_str.erase(portfolio_str.begin(), portfolio_str.begin() + position.size() + 1);

        std::string pos_alloc(position.begin() + position.find(':') + 1, position.end());

        allocation alloc;
        alloc.asset = std::string(position.begin(), position.begin() + position.find(':'));
        alloc.allocation = atof(pos_alloc.c_str());

        portfolio.emplace_back(alloc);
    }

    return portfolio;
}

void swr::normalize_portfolio(std::vector<allocation> & portfolio){
    float total = total_allocation(portfolio);

    if (total != 100.0f) {
        for (auto& position : portfolio) {
            position.allocation *= 100.0f / total;
        }
    }
}

float swr::total_allocation(std::vector<allocation> & portfolio){
    float total = 0;

    for (auto & position : portfolio) {
        total += position.allocation;
    }

    return total;
}
