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

struct data {
    size_t month;
    size_t year;
    float value;
};

struct data_vector {
    using vector_type    = std::vector<swr::data>;
    using const_iterator = vector_type::const_iterator;
    using value_type     = vector_type::value_type;

    std::string name;
    vector_type data;

    auto begin() {
        return data.begin();
    }
    auto begin() const {
        return data.begin();
    }
    auto end() {
        return data.end();
    }
    auto end() const {
        return data.end();
    }

    decltype(auto) front() const {
        return data.front();
    }

    decltype(auto) back() const {
        return data.back();
    }

    decltype(auto) operator[] (size_t i) {
        return data[i];
    }
    decltype(auto) operator[] (size_t i) const {
        return data[i];
    }

    size_t size() const {
        return data.size();
    }

    bool empty() const {
        return data.empty();
    }
};

std::vector<data_vector> load_values(const std::vector<swr::allocation>& portfolio);
data_vector load_inflation(const std::vector<data_vector> & values, const std::string& inflation);
data_vector load_exchange(const std::string& inflation);
data_vector load_exchange_inv(const std::string& inflation);

float get_value(const data_vector& values, size_t year, size_t month);
data_vector::const_iterator get_start(const data_vector& values, size_t year, size_t month);

bool is_start_valid(const data_vector& values, size_t year, size_t month);

} // namespace swr
