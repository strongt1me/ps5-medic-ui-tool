// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/i18n.hpp"

#include <nlohmann/json.hpp>

namespace medic::ui {

Strings::Strings(const char* json_text) {
    auto j = nlohmann::json::parse(json_text);
    for (auto& [k, v] : j.items())
        if (v.is_string()) map_[k] = v.get<std::string>();
}

const char* lang_code(Lang l) {
    switch (l) {
        case Lang::De: return "de";
        case Lang::En: return "en";
        case Lang::Fr: return "fr";
        case Lang::It: return "it";
        case Lang::Es: return "es";
    }
    return "de";
}

bool lang_from_code(std::string_view code, Lang& out) {
    for (Lang l : kAllLangs)
        if (code == lang_code(l)) {
            out = l;
            return true;
        }
    return false;
}

Lang lang_from_system_id(int id) {
    switch (id) {
        case 4: return Lang::De;
        case 2:
        case 22: return Lang::Fr;
        case 5: return Lang::It;
        case 3:
        case 20: return Lang::Es;
        default: return Lang::En;
    }
}

const Strings& Strings::get(Lang l) {
    static const Strings de(kDeJson), en(kEnJson), fr(kFrJson), it(kItJson), es(kEsJson);
    switch (l) {
        case Lang::En: return en;
        case Lang::Fr: return fr;
        case Lang::It: return it;
        case Lang::Es: return es;
        case Lang::De: break;
    }
    return de;
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

namespace {
std::string join_key(std::string_view a, std::string_view b, std::string_view c) {
    std::string k;
    k.reserve(a.size() + b.size() + c.size() + 2);
    k.append(a).push_back('\x1f');
    k.append(b).push_back('\x1f');
    k.append(c);
    return k;
}
}  // namespace

Knowledge::Knowledge(const char* json_text) {
    if (!json_text) return;
    const auto j = nlohmann::json::parse(json_text, nullptr, false);
    if (!j.is_object()) return;
    for (const auto& [section, entries] : j.items()) {
        if (!entries.is_object()) continue;
        for (const auto& [id, fields] : entries.items()) {
            if (!fields.is_object()) continue;
            for (const auto& [field, v] : fields.items()) {
                if (v.is_string()) {
                    texts_[join_key(section, id, field)] = v.get<std::string>();
                } else if (v.is_array()) {
                    std::vector<std::string> list;
                    for (const auto& x : v)
                        if (x.is_string()) list.push_back(x.get<std::string>());
                    lists_[join_key(section, id, field)] = std::move(list);
                }
            }
        }
    }
}

const Knowledge& Knowledge::get(Lang l) {
    static const Knowledge none(nullptr), fr(kFrKnowledge), it(kItKnowledge), es(kEsKnowledge);
    switch (l) {
        case Lang::Fr: return fr;
        case Lang::It: return it;
        case Lang::Es: return es;
        case Lang::De:
        case Lang::En: break;
    }
    return none;
}

const std::string* Knowledge::text(std::string_view section, std::string_view id, std::string_view field) const {
    const auto it = texts_.find(join_key(section, id, field));
    return it == texts_.end() ? nullptr : &it->second;
}

const std::vector<std::string>* Knowledge::list(std::string_view section, std::string_view id, std::string_view field) const {
    const auto it = lists_.find(join_key(section, id, field));
    return it == lists_.end() ? nullptr : &it->second;
}

}  // namespace medic::ui
