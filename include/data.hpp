#pragma once

#include <string>
#include <vector>

#include "portfolio.hpp"

namespace swr {

struct data {
    size_t month;
    size_t year;
    float value;
};

using data_vector = std::vector<swr::data>;

std::vector<data_vector> load_values(const std::vector<swr::allocation>& portfolio);
data_vector load_inflation(const std::vector<data_vector> & values, const std::string& inflation);
data_vector load_exchange(const std::string& inflation);

float get_value(const data_vector& values, size_t year, size_t month);
data_vector::const_iterator get_start(const data_vector& values, size_t year, size_t month);

} // namespace swr
