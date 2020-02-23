#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>

#include "data.hpp"

namespace {

swr::data_vector load_data(const std::string& path) {
  swr::data_vector points;

  std::ifstream file(path);

  if (!file) {
      std::cout << "Impossible to load data " << path << std::endl;
      return {};
  }

  std::string line;
  while (std::getline(file, line)) {
      auto index1 = line.find(',');
      auto index2 = line.find(',', index1 + 1);

      std::string month(line.begin(), line.begin() + index1);
      std::string year(line.begin() + index1 + 1, line.begin() + index2);
      std::string value(line.begin() + index2 + 1, line.end());

      if (value[0] == '\"') {
          value = value.substr(1, value.size() - 3);
      }

      value.erase(std::remove(value.begin(), value.end(), ','), value.end());

      swr::data data;
      data.month = atoi(month.c_str());
      data.year  = atoi(year.c_str());
      data.value = atof(value.c_str());

      points.push_back(data);
  }

  return points;
}

// Make sure that the data ends with a full year
void fix_end(swr::data_vector & values) {
    while (values.back().month != 12) {
        values.pop_back();
    }
}

// Make sure that the data starts with a full year
void fix_start(swr::data_vector & values) {
    while (values.front().month != 1) {
        values.erase(values.begin());
    }
}

void normalize_data(swr::data_vector & values) {
    fix_end(values);
    fix_start(values);

    if (values.front().value == 1.0f) {
        return;
    }

    auto previous_value = values[0].value;
    values[0].value = 1.0f;

    for (size_t i = 1; i < values.size(); ++i) {
        auto value      = values[i].value;
        values[i].value = values[i - 1].value * (value / previous_value);
        previous_value  = value;
    }
}

void transform_to_returns(swr::data_vector & values) {
    // Should already be normalized
    float previous_value = values[0].value;

    for (size_t i = 1; i < values.size(); ++i) {
        auto new_value  = values[i].value / previous_value;
        previous_value  = values[i].value;
        values[i].value = new_value;
    }
}

} // end of anonymous namespace

std::vector<swr::data_vector> swr::load_values(const std::vector<swr::allocation>& portfolio) {
    std::vector<swr::data_vector> values;

    for (auto& asset : portfolio) {
        const auto & asset_name = asset.asset;

        bool x2 = std::string(asset_name.end() - 3, asset_name.end()) == "_x2";

        std::string filename = x2 ? std::string(asset_name.begin(), asset_name.end() - 3) : asset_name;

        auto data = load_data("stock-data/" + filename + ".csv");

        if (data.empty()) {
            std::cout << "Impossible to load data for asset " << asset_name << std::endl;
            return {};
        }

        normalize_data(data);
        transform_to_returns(data);

        if (x2) {
            auto copy = data;
            std::copy(copy.begin(), copy.end(), std::back_inserter(data));

            for (size_t i = 0; i < copy.size(); ++i) {
                size_t j         = copy.size() - 1 - i;
                auto& curr       = data[j];
                const auto& prev = data[j + 1];

                if (prev.month == 1) {
                    curr.month = 12;
                    curr.year  = prev.year - 1;
                } else {
                    curr.month = prev.month - 1;
                    curr.year  = prev.year;
                }
            }
        }

        values.emplace_back(std::move(data));
    }

    return values;
}

swr::data_vector swr::load_inflation(const std::vector<swr::data_vector> & values, const std::string& inflation) {
    swr::data_vector inflation_data;

    if (inflation == "no_inflation") {
        inflation_data = values.front();

        for (auto& value : inflation_data) {
            value.value = 1;
        }
    } else {
        inflation_data = load_data("stock-data/" + inflation + ".csv");

        if (inflation_data.empty()) {
            std::cout << "Impossible to load inflation data for asset " << inflation << std::endl;
            return {};
        }

        normalize_data(inflation_data);
        transform_to_returns(inflation_data);
    }

    return inflation_data;
}

swr::data_vector swr::load_exchange(const std::string& exchange) {
    auto exchange_data = load_data("stock-data/" + exchange + ".csv");

    if (exchange_data.empty()) {
        std::cout << "Impossible to load exchange data for " << exchange << std::endl;
        return {};
    }

    return exchange_data;
}

float swr::get_value(const swr::data_vector& values, size_t year, size_t month) {
    for (auto & data : values) {
        if (data.year == year && data.month == month) {
            return data.value;
        }
    }

    std::cout << "This should not happen" << std::endl;

    return 0.0f;
}

swr::data_vector::const_iterator swr::get_start(const swr::data_vector& values, size_t year, size_t month) {
    auto it  = values.begin();
    auto end = values.end();

    while (it != end) {
        if (it->year == year && it->month == month) {
            return it;
        }

        ++it;
    }

    std::cout << "This should not happen" << std::endl;

    return values.begin();
}
