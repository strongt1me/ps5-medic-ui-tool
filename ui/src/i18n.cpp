// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/i18n.hpp"

#include <nlohmann/json.hpp>

namespace medic::ui {

Strings::Strings(const char* json_text) {
    auto j = nlohmann::json::parse(json_text);
    for (auto& [k, v] : j.items())
        if (v.is_string()) map_[k] = v.get<std::string>();
}

const Strings& Strings::get(Lang l) {
    static const Strings de(kDeJson), en(kEnJson);
    return l == Lang::En ? en : de;
}

std::vector<std::string>& Strings::missing() {
    static std::vector<std::string> v;
    return v;
}

std::string Strings::operator()(std::string_view key) const {
    auto it = map_.find(key);
    if (it != map_.end()) return it->second;
    missing().emplace_back(key);
    return "!" + std::string(key) + "!";
}

bool Strings::has(std::string_view key) const { return map_.find(key) != map_.end(); }

std::vector<std::string> Strings::keys() const {
    std::vector<std::string> v;
    for (auto& [k, _] : map_) v.push_back(k);
    return v;
}

std::string Strings::fmt(std::string_view key, std::initializer_list<std::pair<std::string, std::string>> args) const {
    std::string s = (*this)(key);
    for (auto& [name, value] : args) {
        const std::string token = "{" + name + "}";
        for (std::size_t pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + value.size()))
            s.replace(pos, token.size(), value);
    }
    return s;
}

}  // namespace medic::ui
