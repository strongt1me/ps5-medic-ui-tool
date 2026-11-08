#include "medic/diff.hpp"

#include <algorithm>

namespace medic {

namespace {

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v;
    std::size_t pos = 0;
    while (pos < s.size()) {
        auto nl = s.find('\n', pos);
        std::string l = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(std::move(l));
        pos = nl == std::string::npos ? s.size() : nl + 1;
    }
    return v;
}

}  // namespace

bool diff_lines(const std::string& old_text, const std::string& new_text, std::vector<DiffLine>& out, std::size_t max_lines) {
    out.clear();
    auto a = split_lines(old_text), b = split_lines(new_text);
    if (a.size() > max_lines || b.size() > max_lines) return false;
    std::vector<std::vector<int>> L(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = a.size(); i-- > 0;)
        for (std::size_t j = b.size(); j-- > 0;) L[i][j] = a[i] == b[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && a[i] == b[j]) {
            out.push_back({' ', a[i]});
            ++i;
            ++j;
        } else if (i < a.size() && (j == b.size() || L[i + 1][j] >= L[i][j + 1])) {
            out.push_back({'-', a[i++]});
        } else {
            out.push_back({'+', b[j++]});
        }
    }
    return true;
}

}  // namespace medic
