//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <vector>
#include <map>
#include <string_view>
#include "cpp_utils/assert.hpp"

#include "portfolio.hpp"
#include "data.hpp"

namespace swr {

struct GraphBase {
    explicit GraphBase(bool enabled, std::string_view ytitle, std::string_view graph) : enabled_(enabled), graph_(graph), yitle_(ytitle) {}

    template <typename T1, typename T2>
    void dump_labels(const std::vector<std::map<T1, T2>>& data) {
        cpp_assert(data.size(), "data cannot be empty");

        std::string sep = "";
        for (auto& [key, value] : data.front()) {
            std::cout << sep << key;
            sep = ",";
        }
        std::cout << "|,\"series\":|";

        std::string serie_sep;

        for (auto& serie : data) {
            std::cout << serie_sep << "|";
            sep = "";
            for (auto& [key, value] : serie) {
                std::cout << sep << value;
                sep = ",";
            }
            std::cout << "|";
            serie_sep = ",";
        }
    }

    template <typename T1, typename T2>
    void dump_graph(const std::vector<std::map<T1, T2>>& data) {
        if (enabled_ && !flushed_) {
            std::cout << "[" << graph_ << " title=\"" << title_ << "\" ytitle=\"" << yitle_ << "\" xtitle=\"" << xtitle_ << "\"";

            if (legends_.empty()) {
                std::cout << "]";

                extra_ += R"("legend_position":"none", )";
            } else {
                std::stringstream legends;
                std::string       sep;
                for (auto& legend : legends_) {
                    legends << sep << legend;
                    sep = ",";
                }
                std::cout << " legends=\"" << legends.str() << "\"]";
            }
            std::cout << "{" << extra_ << "\"labels\":|";
            dump_labels(data);
            std::cout << "|}[/" << graph_ << "]";
            std::cout << '\n';

            flushed_ = true;
        }
    }

    void add_legend(std::string_view title) {
        legends_.emplace_back(title);
    }

    void set_extra(std::string_view extra) {
        extra_ = extra;
    }

    const bool               enabled_;
    const std::string        graph_;
    const std::string        yitle_;
    std::string              xtitle_ = "Withdrawal Rate (%)";
    std::string              title_  = "TODO";
    std::string              extra_;
    std::vector<std::string> legends_;
    bool                     flushed_ = false;
};

struct Graph : GraphBase {
    explicit Graph(bool enabled, std::string_view ytitle = "Success Rate (%)", std::string_view graph = "line-graph") : GraphBase(enabled, ytitle, graph) {}

    ~Graph() {
        flush();
    }

    // We don't use JSON, but a form of retarded JSON for WPML to handle
    void flush() {
        dump_graph(data_);
    }

    void add_data(const std::map<float, float>& data) {
        data_.emplace_back(data);
    }

    std::vector<std::map<float, float>> data_;
};

struct TimeGraph : GraphBase {
    explicit TimeGraph(bool enabled, std::string_view ytitle = "Success Rate (%)", std::string_view graph = "line-graph") : GraphBase(enabled, ytitle, graph) {}

    ~TimeGraph() {
        flush();
    }

    // We don't use JSON, but a form of retarded JSON for WPML to handle
    void flush() {
        extra_ += R"("x_data_type":"time", )";
        dump_graph(data_);
    }

    void add_data(const std::map<int64_t, float>& data) {
        data_.emplace_back(data);
    }

    std::vector<std::map<int64_t, float>> data_;
};

} // namespace swr
