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

std::vector<data> load_data(const std::string& path);
void normalize_data(std::vector<data> & data);
void transform_to_returns(std::vector<data> & data);

std::vector<std::vector<swr::data>> load_values(const std::vector<swr::allocation>& portfolio);
std::vector<swr::data> load_inflation(const std::vector<std::vector<swr::data>> & values, const std::string& inflation);

float get_value(const std::vector<swr::data>& values, size_t year, size_t month);
std::vector<swr::data>::const_iterator get_start(const std::vector<swr::data>& values, size_t year, size_t month);

} // namespace swr
