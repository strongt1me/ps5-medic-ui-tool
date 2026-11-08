// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/facts.hpp"

#include <sstream>
#include <vector>

namespace medic {

static std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '.')) parts.push_back(seg);
    return parts;
}

void Facts::set(const std::string& path, json value) {
    json* cur = &root_;
    auto parts = split_path(path);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!cur->is_object()) *cur = json::object();
        cur = &(*cur)[parts[i]];
    }
    *cur = std::move(value);
}

const Facts::json* Facts::get(const std::string& path) const {
    const json* cur = &root_;
    for (const auto& part : split_path(path)) {
        if (!cur->is_object()) return nullptr;
        auto it = cur->find(part);
        if (it == cur->end()) return nullptr;
        cur = &*it;
    }
    return cur;
}

}  // namespace medic
